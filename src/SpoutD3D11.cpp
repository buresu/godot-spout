#include "SpoutD3D11.hpp"

#include "SpoutCommon.hpp"

#include <map>
#include <mutex>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d11sdklayers.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <dxgi1_4.h>

using namespace godot;

namespace {

std::mutex s_lock;
std::map<ID3D12Device *, std::weak_ptr<SpoutD3D11Device>> s_on12_devices;
std::map<uint64_t, std::weak_ptr<SpoutD3D11Device>> s_luid_devices;

uint64_t luid_to_u64(const LUID &p_luid) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(p_luid.HighPart)) << 32) |
         p_luid.LowPart;
}

IDXGIAdapter1 *adapter_for_luid(uint64_t p_luid) {
  IDXGIFactory4 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return nullptr;
  }
  LUID luid;
  luid.LowPart = static_cast<DWORD>(p_luid & 0xffffffffu);
  luid.HighPart = static_cast<LONG>(p_luid >> 32);
  IDXGIAdapter1 *adapter = nullptr;
  if (FAILED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) {
    adapter = nullptr;
  }
  factory->Release();
  return adapter;
}

String describe_adapter(IDXGIAdapter1 *p_adapter) {
  DXGI_ADAPTER_DESC1 desc = {};
  if (!p_adapter || FAILED(p_adapter->GetDesc1(&desc))) {
    return String();
  }
  return String(reinterpret_cast<const char16_t *>(desc.Description));
}

// Retries without the debug layer if it isn't installed.
template <typename F> HRESULT create_device(F &&p_create) {
  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  if (spout_env_flag("GODOT_SPOUT_D3D12_DEBUG")) {
    flags |= D3D11_CREATE_DEVICE_DEBUG;
  }
  HRESULT hr = p_create(flags);
  if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
    spout_warn("D3D11 debug layer unavailable; continuing without it.");
    hr = p_create(flags & ~D3D11_CREATE_DEVICE_DEBUG);
  }
  return hr;
}

String hresult(HRESULT p_hr) {
  return "0x" + String::num_int64(static_cast<uint32_t>(p_hr), 16);
}

static_assert(int(D3D11_MESSAGE_SEVERITY_WARNING) ==
                  int(D3D12_MESSAGE_SEVERITY_WARNING),
              "severity values differ");

template <typename Message, typename Queue>
void drain(Queue *p_queue, const char *p_api, const char *p_where) {
  if (!p_queue) {
    return;
  }
  static const char *const names[] = {"CORRUPTION", "ERROR", "WARNING"};
  const UINT64 n = p_queue->GetNumStoredMessages();
  for (UINT64 i = 0; i < n; i++) {
    SIZE_T len = 0;
    p_queue->GetMessage(i, nullptr, &len);
    auto *msg = static_cast<Message *>(malloc(len));
    if (msg && SUCCEEDED(p_queue->GetMessage(i, msg, &len))) {
      const int severity = static_cast<int>(msg->Severity);
      if (severity <= int(D3D11_MESSAGE_SEVERITY_WARNING)) {
        spout_warn(String(p_api) + " " + names[severity] + " [" + p_where +
                   "]: " + String(msg->pDescription));
      }
    }
    free(msg);
  }
  p_queue->ClearStoredMessages();
}

} // namespace

SpoutD3D11Device::~SpoutD3D11Device() {
  if (_context) {
    _context->ClearState();
    _context->Flush();
  }
  spout_release(_info_queue11);
  spout_release(_info_queue12);
  spout_release(_on12);
  spout_release(_context);
  spout_release(_device);
  spout_log("Released D3D11 device on " + _adapter_name);
}

std::shared_ptr<SpoutD3D11Device>
SpoutD3D11Device::get_on12(ID3D12Device *p_device, IUnknown *p_queue) {
  if (!p_device || !p_queue) {
    return nullptr;
  }
  std::lock_guard<std::mutex> guard(s_lock);
  if (auto existing = s_on12_devices[p_device].lock()) {
    return existing;
  }

  // On Godot's queue, so each Flush() is ordered between Godot's frames.
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  const HRESULT hr = create_device([&](UINT p_flags) {
    return D3D11On12CreateDevice(p_device, p_flags, nullptr, 0, &p_queue, 1, 0,
                                 &device, &context, nullptr);
  });
  if (FAILED(hr)) {
    spout_error("D3D11On12CreateDevice failed (" + hresult(hr) + ").");
    return nullptr;
  }

  ID3D11On12Device *on12 = nullptr;
  if (FAILED(device->QueryInterface(IID_PPV_ARGS(&on12)))) {
    spout_error("ID3D11On12Device not available.");
    context->Release();
    device->Release();
    return nullptr;
  }

  std::shared_ptr<SpoutD3D11Device> result(new SpoutD3D11Device());
  result->_device = device;
  result->_context = context;
  result->_on12 = on12;
  IDXGIAdapter1 *adapter =
      adapter_for_luid(luid_to_u64(p_device->GetAdapterLuid()));
  result->_adapter_name = describe_adapter(adapter);
  spout_release(adapter);
  result->init_debug(p_device);
  s_on12_devices[p_device] = result;
  spout_info("Sharing textures through D3D11On12 on " + result->_adapter_name +
             " (D3D12).");
  return result;
}

std::shared_ptr<SpoutD3D11Device>
SpoutD3D11Device::get_for_luid(uint64_t p_luid) {
  std::lock_guard<std::mutex> guard(s_lock);
  if (auto existing = s_luid_devices[p_luid].lock()) {
    return existing;
  }

  IDXGIAdapter1 *adapter = adapter_for_luid(p_luid);
  if (!adapter) {
    spout_error("No DXGI adapter matches the Vulkan device (LUID " +
                String::num_int64(p_luid, 16) + ").");
    return nullptr;
  }
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                      D3D_FEATURE_LEVEL_11_0};
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  const HRESULT hr = create_device([&](UINT p_flags) {
    return D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, p_flags,
                             levels, 2, D3D11_SDK_VERSION, &device, nullptr,
                             &context);
  });
  const String name = describe_adapter(adapter);
  adapter->Release();
  if (FAILED(hr)) {
    spout_error("D3D11CreateDevice failed (" + hresult(hr) + ").");
    return nullptr;
  }

  std::shared_ptr<SpoutD3D11Device> result(new SpoutD3D11Device());
  result->_device = device;
  result->_context = context;
  result->_adapter_name = name;
  result->init_debug(nullptr);
  s_luid_devices[p_luid] = result;
  spout_info("Sharing textures through D3D11 on " + result->_adapter_name +
             " (Vulkan interop).");
  return result;
}

bool SpoutD3D11Device::create_shared_texture(uint32_t p_width,
                                             uint32_t p_height,
                                             uint32_t p_dxgi_format,
                                             ID3D11Texture2D **r_texture,
                                             uint64_t *r_handle) const {
  D3D11_TEXTURE2D_DESC desc = {};
  desc.Width = p_width;
  desc.Height = p_height;
  desc.MipLevels = 1;
  desc.ArraySize = 1;
  // Typed: receivers don't handle TYPELESS.
  desc.Format = static_cast<DXGI_FORMAT>(p_dxgi_format);
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  // What DX9/DX11 receivers expect.
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  ID3D11Texture2D *texture = nullptr;
  const HRESULT hr = _device->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr)) {
    spout_error("Could not create shared texture " +
                String::num_int64(p_width) + "x" + String::num_int64(p_height) +
                " " + spout_dxgi_format_name(p_dxgi_format) + " (" +
                hresult(hr) + ").");
    return false;
  }
  IDXGIResource *resource = nullptr;
  HANDLE handle = nullptr;
  if (SUCCEEDED(texture->QueryInterface(IID_PPV_ARGS(&resource)))) {
    resource->GetSharedHandle(&handle);
    resource->Release();
  }
  if (!handle) {
    spout_error("Shared texture has no share handle.");
    texture->Release();
    return false;
  }
  *r_texture = texture;
  *r_handle = reinterpret_cast<uint64_t>(handle);
  return true;
}

bool SpoutD3D11Device::open_shared_texture(
    uint64_t p_handle, ID3D11Texture2D **r_texture, uint32_t &r_width,
    uint32_t &r_height, uint32_t &r_dxgi_format, String &r_error) const {
  ID3D11Texture2D *texture = nullptr;
  const HRESULT hr = _device->OpenSharedResource(
      reinterpret_cast<HANDLE>(p_handle), IID_PPV_ARGS(&texture));
  if (FAILED(hr) || !texture) {
    // Adapter mismatch and a closed sender both give E_INVALIDARG.
    r_error = "could not open the sender's texture (" + hresult(hr) +
              "). The sender is probably running on a different GPU than "
              "Godot (" +
              _adapter_name + "), or it just closed.";
    return false;
  }
  D3D11_TEXTURE2D_DESC desc = {};
  texture->GetDesc(&desc);
  const uint32_t format = spout_dxgi_normalize(desc.Format);
  if (format == SPOUT_DXGI_UNKNOWN) {
    r_error =
        "unsupported sender format " + spout_dxgi_format_name(desc.Format);
    texture->Release();
    return false;
  }
  *r_texture = texture;
  r_width = desc.Width;
  r_height = desc.Height;
  r_dxgi_format = format;
  return true;
}

void SpoutD3D11Device::init_debug(ID3D12Device *p_device12) {
  if (!spout_env_flag("GODOT_SPOUT_D3D12_DEBUG")) {
    return;
  }
  if (FAILED(_device->QueryInterface(IID_PPV_ARGS(&_info_queue11)))) {
    _info_queue11 = nullptr;
  }
  if (p_device12 &&
      FAILED(p_device12->QueryInterface(IID_PPV_ARGS(&_info_queue12)))) {
    _info_queue12 = nullptr;
  }
  String message = String("GODOT_SPOUT_D3D12_DEBUG: D3D11 debug layer ") +
                   (_info_queue11 ? "active" : "NOT active");
  if (p_device12) {
    message += String(", D3D12 debug layer ") + (_info_queue12
                                                     ? "active"
                                                     : "NOT active (run with "
                                                       "--gpu-validation)");
  }
  spout_warn(message + ".");
}

void SpoutD3D11Device::drain_debug_messages(const char *p_where) const {
  drain<D3D11_MESSAGE>(_info_queue11, "D3D11", p_where);
  drain<D3D12_MESSAGE>(_info_queue12, "D3D12", p_where);
}
