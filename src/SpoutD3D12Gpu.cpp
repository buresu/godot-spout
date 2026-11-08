#include "SpoutD3D12Gpu.hpp"

#include "SpoutD3D11.hpp"
#include "SpoutShare.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>
#include <d3d11on12.h>
#include <d3d12.h>

using namespace godot;

// Sync model (D3D12)
// ==================
//
// Sender, frame N (frame_post_draw, after Godot submitted frame N):
//   1. publish(): the copy into _target that Godot recorded during frame N-1's
//      post-draw was part of frame N's submission. D3D11On12 now copies
//      _target -> shared texture and Flush()es. That work goes onto Godot's
//      queue *after* frame N, so it sees the finished copy.
//   2. The core records the next texture_copy (or compute pass) from the
//      source into _target. RD records it into frame N+1, i.e. after the
//      D3D11On12 copy above on the same queue.
// So there is exactly one frame of latency and no CPU wait anywhere.
//
// Receiver, frame N (frame_pre_draw, before Godot records frame N):
//   acquire(): D3D11On12 copies shared texture -> _source and Flush()es before
//   Godot submits frame N, which then reads _source. No latency.
//
// Resource states: Godot's D3D12 driver tracks legacy resource states per
// texture. D3D11On12's CreateWrappedResource takes the state the resource is
// in when we acquire it (InState) and the state to leave it in when we release
// it (OutState). We pass the *same* state for both, namely the state Godot's
// last operation on the texture left it in, so Godot's bookkeeping stays true.
// Only texture_copy and our compute pass ever touch _target / _source on the
// Godot side, so that state is one of two values per texture (see
// state_for()). One wrapped resource is kept per state because the states are
// fixed at wrap time.
//
// Cross-process sync with receivers/senders is Spout's: the access mutex
// around the copy, and DXGI's implicit synchronisation of legacy shared
// resources at Flush(). That is exactly what spoutDX does.

namespace {

// State Godot leaves a texture in after the last access we recorded.
// Validated with GODOT_SPOUT_D3D12_DEBUG=1 + --gpu-validation (see
// docs/EXECUTION_TRACKER.md, P0-5 / P0-7).
D3D12_RESOURCE_STATES state_for(SpoutGodotAccess p_access, bool p_written) {
  switch (p_access) {
  case SPOUT_ACCESS_COPY:
    return p_written ? D3D12_RESOURCE_STATE_COPY_DEST
                     : D3D12_RESOURCE_STATE_COPY_SOURCE;
  case SPOUT_ACCESS_SHADER:
    return p_written ? D3D12_RESOURCE_STATE_UNORDERED_ACCESS
                     : D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
  default:
    return D3D12_RESOURCE_STATE_COMMON;
  }
}

ID3D11Resource *wrap(SpoutD3D11Device &p_d3d11, ID3D12Resource *p_resource,
                     D3D12_RESOURCE_STATES p_state) {
  D3D11_RESOURCE_FLAGS flags = {}; // inferred from the D3D12 resource
  ID3D11Resource *wrapped = nullptr;
  HRESULT hr = p_d3d11.on12()->CreateWrappedResource(
      p_resource, &flags, p_state, p_state, IID_PPV_ARGS(&wrapped));
  if (FAILED(hr)) {
    spout_error("CreateWrappedResource failed (0x" +
                String::num_int64(static_cast<uint32_t>(hr), 16) + ").");
    return nullptr;
  }
  return wrapped;
}

template <typename T> void safe_release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

// One copy through D3D11On12. p_wrapped is acquired for the duration of the
// copy, which emits the D3D12 barriers into the 11-on-12 command list.
void copy_on12(SpoutD3D11Device &p_d3d11, ID3D11Resource *p_dst,
               ID3D11Resource *p_src, ID3D11Resource *p_wrapped) {
  p_d3d11.on12()->AcquireWrappedResources(&p_wrapped, 1);
  p_d3d11.context()->CopyResource(p_dst, p_src);
  p_d3d11.on12()->ReleaseWrappedResources(&p_wrapped, 1);
  // Submit to Godot's queue now; also required for legacy shared resources so
  // the other process sees the update.
  p_d3d11.context()->Flush();
}

// Access mutex timeout. Receivers hold the mutex only while they submit a
// copy (microseconds); never stall Godot's frame on a stuck receiver.
constexpr uint32_t MUTEX_TIMEOUT_MS = 4;

} // namespace

std::shared_ptr<SpoutD3D11Device>
godot::spout_d3d12_get_device(RenderingDevice *p_rd) {
  auto device = reinterpret_cast<ID3D12Device *>(p_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0));
  auto queue = reinterpret_cast<IUnknown *>(p_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, RID(), 0));
  return SpoutD3D11Device::get_on12(device, queue);
}

// ---------------------------------------------------------------------------
// Sender

SpoutD3D12Sender::SpoutD3D12Sender(RenderingDevice *p_rd)
    : _rd(p_rd), _d3d11(spout_d3d12_get_device(p_rd)) {}

SpoutD3D12Sender::~SpoutD3D12Sender() { release(); }

String SpoutD3D12Sender::get_adapter_name() const {
  return _d3d11 ? _d3d11->adapter_name() : String();
}

bool SpoutD3D12Sender::ensure(uint32_t p_width, uint32_t p_height,
                              SpoutSendFormat p_format) {
  if (!_d3d11) {
    return false;
  }
  if (_target.is_valid() && p_width == _width && p_height == _height &&
      p_format == _format) {
    return true;
  }
  release();

  // Our own target. Godot writes it with texture_copy or a compute pass
  // (STORAGE), D3D11On12 reads it. No shareable formats: we never need views
  // of another format, and it keeps Godot from creating a TYPELESS resource.
  Ref<RDTextureFormat> fmt;
  fmt.instantiate();
  fmt->set_format(spout_send_format_to_rd(p_format));
  fmt->set_width(p_width);
  fmt->set_height(p_height);
  fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                      RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT |
                      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT);
  Ref<RDTextureView> view;
  view.instantiate();
  _target = _rd->texture_create(fmt, view);
  if (!_target.is_valid()) {
    spout_error("Could not create the send texture.");
    return false;
  }

  _dxgi_format = spout_send_format_to_dxgi(p_format);
  if (!_d3d11->create_shared_texture(p_width, p_height, _dxgi_format, &_shared,
                                     &_share_handle)) {
    release();
    return false;
  }

  _width = p_width;
  _height = p_height;
  _format = p_format;
  _generation++;
  _d3d11->drain_debug_messages("sender ensure");
  return true;
}

ID3D11Resource *SpoutD3D12Sender::wrapped_for(SpoutGodotAccess p_access) {
  ID3D11Resource *&slot =
      p_access == SPOUT_ACCESS_SHADER ? _wrapped_shader : _wrapped_copy;
  if (!slot) {
    auto resource = reinterpret_cast<ID3D12Resource *>(_rd->get_driver_resource(
        RenderingDevice::DRIVER_RESOURCE_TEXTURE, _target, 0));
    if (resource) {
      slot = wrap(*_d3d11, resource, state_for(p_access, true));
    }
  }
  return slot;
}

bool SpoutD3D12Sender::publish(SpoutGodotAccess p_access,
                               SpoutShareSender &p_share) {
  if (!_shared || p_access == SPOUT_ACCESS_NONE) {
    return false;
  }
  ID3D11Resource *wrapped = wrapped_for(p_access);
  if (!wrapped) {
    return false;
  }
  if (!p_share.lock(MUTEX_TIMEOUT_MS)) {
    return false;
  }
  copy_on12(*_d3d11, _shared, wrapped, wrapped);
  p_share.unlock();
  _d3d11->drain_debug_messages("sender publish");
  return true;
}

void SpoutD3D12Sender::release() {
  // Release the D3D11 views of _target before Godot frees it. D3D11On12 and
  // RD both defer the real destruction until the GPU is done with it.
  safe_release(_wrapped_copy);
  safe_release(_wrapped_shader);
  safe_release(_shared);
  _share_handle = 0;
  if (_target.is_valid()) {
    _rd->free_rid(_target);
    _target = RID();
  }
  _width = _height = 0;
}

// ---------------------------------------------------------------------------
// Receiver

SpoutD3D12Receiver::SpoutD3D12Receiver(RenderingDevice *p_rd)
    : _rd(p_rd), _d3d11(spout_d3d12_get_device(p_rd)) {}

SpoutD3D12Receiver::~SpoutD3D12Receiver() { close(); }

String SpoutD3D12Receiver::get_adapter_name() const {
  return _d3d11 ? _d3d11->adapter_name() : String();
}

bool SpoutD3D12Receiver::open(const SpoutSenderInfo &p_info, String &r_error) {
  close();
  if (!_d3d11) {
    r_error = "no D3D11On12 device";
    return false;
  }
  if (!_d3d11->open_shared_texture(p_info.share_handle, &_shared, r_error)) {
    return false;
  }

  // Use the format of the texture itself; what senders advertise is
  // sometimes wrong (spoutDX does the same check).
  D3D11_TEXTURE2D_DESC desc = {};
  _shared->GetDesc(&desc);
  const uint32_t dxgi = spout_dxgi_normalize(desc.Format);
  _source_format = spout_dxgi_to_rd(dxgi);
  if (_source_format == RenderingDevice::DATA_FORMAT_MAX) {
    r_error =
        "unsupported sender format " + spout_dxgi_format_name(desc.Format);
    close();
    return false;
  }

  // Same memory layout as the sender's texture so D3D11 CopyResource is
  // valid. (The old code always used BGRA8, which is an invalid copy from
  // RGBA8 / RGBA16F senders; plan bug B4.)
  Ref<RDTextureFormat> fmt;
  fmt.instantiate();
  fmt->set_format(_source_format);
  fmt->set_width(desc.Width);
  fmt->set_height(desc.Height);
  fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
                      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);
  Ref<RDTextureView> view;
  view.instantiate();
  _source = _rd->texture_create(fmt, view);
  if (!_source.is_valid()) {
    r_error = "could not create the receive texture";
    close();
    return false;
  }
  _d3d11->drain_debug_messages("receiver open");
  return true;
}

void SpoutD3D12Receiver::close() {
  safe_release(_wrapped_copy);
  safe_release(_wrapped_shader);
  safe_release(_shared);
  if (_source.is_valid()) {
    _rd->free_rid(_source);
    _source = RID();
  }
  _source_format = RenderingDevice::DATA_FORMAT_MAX;
}

ID3D11Resource *SpoutD3D12Receiver::wrapped_for(SpoutGodotAccess p_access) {
  ID3D11Resource *&slot =
      p_access == SPOUT_ACCESS_SHADER ? _wrapped_shader : _wrapped_copy;
  if (!slot) {
    auto resource = reinterpret_cast<ID3D12Resource *>(_rd->get_driver_resource(
        RenderingDevice::DRIVER_RESOURCE_TEXTURE, _source, 0));
    if (resource) {
      slot = wrap(*_d3d11, resource, state_for(p_access, false));
    }
  }
  return slot;
}

bool SpoutD3D12Receiver::acquire(SpoutGodotAccess p_last_access,
                                 SpoutShareReceiver &p_share) {
  if (!_shared || !_source.is_valid()) {
    return false;
  }
  if (p_last_access == SPOUT_ACCESS_NONE) {
    // Godot hasn't read _source yet, so we don't know which state its tracker
    // has it in. Skip the copy this once; the core still issues its read so
    // the state becomes known for the next frame. The core doesn't show the
    // texture before the first real frame.
    return false;
  }
  ID3D11Resource *wrapped = wrapped_for(p_last_access);
  if (!wrapped) {
    return false;
  }
  if (!p_share.lock(MUTEX_TIMEOUT_MS)) {
    return false;
  }
  copy_on12(*_d3d11, wrapped, _shared, wrapped);
  p_share.unlock();
  _d3d11->drain_debug_messages("receiver acquire");
  return true;
}
