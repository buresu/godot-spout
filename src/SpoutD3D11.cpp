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

// Note: <windows.h> maps GetMessage to GetMessageW. The info-queue headers
// were parsed with that macro too, so q->GetMessage() below stays consistent.

using namespace godot;

namespace {

std::mutex s_lock;
std::map<ID3D12Device *, std::weak_ptr<SpoutD3D11Device>> s_on12_devices;
std::map<uint64_t, std::weak_ptr<SpoutD3D11Device>> s_luid_devices;

uint64_t luid_to_u64(const LUID &p_luid) {
  return (static_cast<uint64_t>(static_cast<uint32_t>(p_luid.HighPart)) << 32) |
         p_luid.LowPart;
}

String adapter_name_for_luid(uint64_t p_luid) {
  IDXGIFactory4 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    return String();
  }
  LUID luid;
  luid.LowPart = static_cast<DWORD>(p_luid & 0xffffffffu);
  luid.HighPart = static_cast<LONG>(p_luid >> 32);
  String name;
  IDXGIAdapter1 *adapter = nullptr;
  if (SUCCEEDED(factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter)))) {
    DXGI_ADAPTER_DESC1 desc = {};
    if (SUCCEEDED(adapter->GetDesc1(&desc))) {
      name = String(reinterpret_cast<const char16_t *>(desc.Description));
    }
    adapter->Release();
  }
  factory->Release();
  return name;
}

template <typename T> void safe_release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

const char *severity_name11(D3D11_MESSAGE_SEVERITY p) {
  switch (p) {
  case D3D11_MESSAGE_SEVERITY_CORRUPTION:
    return "CORRUPTION";
  case D3D11_MESSAGE_SEVERITY_ERROR:
    return "ERROR";
  case D3D11_MESSAGE_SEVERITY_WARNING:
    return "WARNING";
  default:
    return "INFO";
  }
}

const char *severity_name12(D3D12_MESSAGE_SEVERITY p) {
  switch (p) {
  case D3D12_MESSAGE_SEVERITY_CORRUPTION:
    return "CORRUPTION";
  case D3D12_MESSAGE_SEVERITY_ERROR:
    return "ERROR";
  case D3D12_MESSAGE_SEVERITY_WARNING:
    return "WARNING";
  default:
    return "INFO";
  }
}

} // namespace

SpoutD3D11Device::~SpoutD3D11Device() {
  if (_context) {
    // Submit anything still recorded before the device goes away. The D3D11
    // runtime (and D3D11On12) keeps resources alive until the GPU is done.
    _context->ClearState();
    _context->Flush();
  }
  safe_release(_info_queue11);
  safe_release(_info_queue12);
  safe_release(_on12);
  safe_release(_context);
  safe_release(_device);
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

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  if (spout_env_flag("GODOT_SPOUT_D3D12_DEBUG")) {
    flags |= D3D11_CREATE_DEVICE_DEBUG;
  }

  // Passing Godot's queue makes D3D11On12 submit its command lists to that
  // queue. Everything we Flush() is therefore ordered after the Godot frame
  // submitted before it and before the frame submitted after it.
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  HRESULT hr = D3D11On12CreateDevice(p_device, flags, nullptr, 0, &p_queue, 1,
                                     0, &device, &context, nullptr);
  if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
    spout_warn("D3D11 debug layer unavailable; continuing without it.");
    flags &= ~D3D11_CREATE_DEVICE_DEBUG;
    hr = D3D11On12CreateDevice(p_device, flags, nullptr, 0, &p_queue, 1, 0,
                               &device, &context, nullptr);
  }
  if (FAILED(hr)) {
    spout_error("D3D11On12CreateDevice failed (0x" +
                String::num_int64(static_cast<uint32_t>(hr), 16) + ").");
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
  result->_device12 = p_device;
  result->_adapter_name =
      adapter_name_for_luid(luid_to_u64(p_device->GetAdapterLuid()));
  result->init_debug(p_device);
  s_on12_devices[p_device] = result;
  spout_log("Created D3D11On12 device on " + result->_adapter_name);
  return result;
}

std::shared_ptr<SpoutD3D11Device>
SpoutD3D11Device::get_for_luid(uint64_t p_luid) {
  std::lock_guard<std::mutex> guard(s_lock);
  if (auto existing = s_luid_devices[p_luid].lock()) {
    return existing;
  }

  IDXGIFactory4 *factory = nullptr;
  if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
    spout_error("CreateDXGIFactory1 failed.");
    return nullptr;
  }
  LUID luid;
  luid.LowPart = static_cast<DWORD>(p_luid & 0xffffffffu);
  luid.HighPart = static_cast<LONG>(p_luid >> 32);
  IDXGIAdapter1 *adapter = nullptr;
  HRESULT hr = factory->EnumAdapterByLuid(luid, IID_PPV_ARGS(&adapter));
  factory->Release();
  if (FAILED(hr)) {
    spout_error("No DXGI adapter matches the Vulkan device (LUID " +
                String::num_int64(p_luid, 16) + ").");
    return nullptr;
  }

  UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
  if (spout_env_flag("GODOT_SPOUT_D3D12_DEBUG")) {
    flags |= D3D11_CREATE_DEVICE_DEBUG;
  }
  const D3D_FEATURE_LEVEL levels[] = {D3D_FEATURE_LEVEL_11_1,
                                      D3D_FEATURE_LEVEL_11_0};
  ID3D11Device *device = nullptr;
  ID3D11DeviceContext *context = nullptr;
  // D3D_DRIVER_TYPE_UNKNOWN is required when an explicit adapter is passed.
  hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                         levels, 2, D3D11_SDK_VERSION, &device, nullptr,
                         &context);
  if (FAILED(hr) && (flags & D3D11_CREATE_DEVICE_DEBUG)) {
    flags &= ~D3D11_CREATE_DEVICE_DEBUG;
    hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, flags,
                           levels, 2, D3D11_SDK_VERSION, &device, nullptr,
                           &context);
  }
  DXGI_ADAPTER_DESC1 desc = {};
  adapter->GetDesc1(&desc);
  adapter->Release();
  if (FAILED(hr)) {
    spout_error("D3D11CreateDevice failed (0x" +
                String::num_int64(static_cast<uint32_t>(hr), 16) + ").");
    return nullptr;
  }

  std::shared_ptr<SpoutD3D11Device> result(new SpoutD3D11Device());
  result->_device = device;
  result->_context = context;
  result->_adapter_name =
      String(reinterpret_cast<const char16_t *>(desc.Description));
  result->init_debug(nullptr);
  s_luid_devices[p_luid] = result;
  spout_log("Created D3D11 device on " + result->_adapter_name);
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
  // Always a typed format. Godot's D3D12 render targets are TYPELESS, and the
  // old code copied that format into the shared texture, so receivers were
  // offered e.g. R8G8B8A8_TYPELESS (plan bug B9).
  desc.Format = static_cast<DXGI_FORMAT>(p_dxgi_format);
  desc.SampleDesc.Count = 1;
  desc.Usage = D3D11_USAGE_DEFAULT;
  desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
  desc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;

  ID3D11Texture2D *texture = nullptr;
  HRESULT hr = _device->CreateTexture2D(&desc, nullptr, &texture);
  if (FAILED(hr)) {
    spout_error("Could not create shared texture " +
                String::num_int64(p_width) + "x" + String::num_int64(p_height) +
                " " + spout_dxgi_format_name(p_dxgi_format) + " (0x" +
                String::num_int64(static_cast<uint32_t>(hr), 16) + ").");
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

bool SpoutD3D11Device::open_shared_texture(uint64_t p_handle,
                                           ID3D11Texture2D **r_texture,
                                           String &r_error) const {
  ID3D11Texture2D *texture = nullptr;
  HRESULT hr = _device->OpenSharedResource(reinterpret_cast<HANDLE>(p_handle),
                                           IID_PPV_ARGS(&texture));
  if (FAILED(hr) || !texture) {
    // E_INVALIDARG is what an adapter mismatch looks like; a handle from a
    // sender that just closed gives the same result.
    r_error = "could not open the sender's texture (0x" +
              String::num_int64(static_cast<uint32_t>(hr), 16) +
              "). The sender is probably running on a different GPU than "
              "Godot (" +
              _adapter_name + "), or it just closed.";
    return false;
  }
  *r_texture = texture;
  return true;
}

void SpoutD3D11Device::init_debug(ID3D12Device *p_device12) {
  if (!spout_env_flag("GODOT_SPOUT_D3D12_DEBUG")) {
    return;
  }
  ID3D11InfoQueue *q11 = nullptr;
  if (SUCCEEDED(_device->QueryInterface(IID_PPV_ARGS(&q11)))) {
    _info_queue11 = q11;
  }
  if (p_device12) {
    ID3D12InfoQueue *q12 = nullptr;
    if (SUCCEEDED(p_device12->QueryInterface(IID_PPV_ARGS(&q12)))) {
      _info_queue12 = q12;
    }
  }
  spout_warn(String("GODOT_SPOUT_D3D12_DEBUG: D3D11 debug layer ") +
             (_info_queue11 ? "active" : "NOT active") +
             (p_device12 ? String(", D3D12 debug layer ") +
                               (_info_queue12 ? "active"
                                              : "NOT active (run "
                                                "with "
                                                "--gpu-validation)")
                         : String()) +
             ".");
}

void SpoutD3D11Device::drain_debug_messages(const char *p_where) const {
  if (_info_queue11) {
    auto *q = static_cast<ID3D11InfoQueue *>(_info_queue11);
    const UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; i++) {
      SIZE_T len = 0;
      q->GetMessage(i, nullptr, &len);
      auto *msg = static_cast<D3D11_MESSAGE *>(malloc(len));
      if (msg && SUCCEEDED(q->GetMessage(i, msg, &len)) &&
          msg->Severity <= D3D11_MESSAGE_SEVERITY_WARNING) {
        spout_warn(String("D3D11 ") + severity_name11(msg->Severity) + " [" +
                   p_where + "]: " + String(msg->pDescription));
      }
      free(msg);
    }
    q->ClearStoredMessages();
  }
  if (_info_queue12) {
    auto *q = static_cast<ID3D12InfoQueue *>(_info_queue12);
    const UINT64 n = q->GetNumStoredMessages();
    for (UINT64 i = 0; i < n; i++) {
      SIZE_T len = 0;
      q->GetMessage(i, nullptr, &len);
      auto *msg = static_cast<D3D12_MESSAGE *>(malloc(len));
      if (msg && SUCCEEDED(q->GetMessage(i, msg, &len)) &&
          msg->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
        spout_warn(String("D3D12 ") + severity_name12(msg->Severity) + " [" +
                   p_where + "]: " + String(msg->pDescription));
      }
      free(msg);
    }
    q->ClearStoredMessages();
  }
}
