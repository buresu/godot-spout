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

// D3D11On12 runs on Godot's queue, so ordering is submission order and nothing
// waits on the CPU.
//   Sender: publish() (post-draw) copies the target Godot wrote in the frame
//   just submitted to the shared texture. One frame of latency.
//   Receiver: acquire() (pre-draw) copies the shared texture to _source before
//   Godot submits the frame that reads it.
//
// States: with enhanced barriers, D3D11On12's legacy barriers are only legal in
// COMMON, so we create the texture with ALLOW_SIMULTANEOUS_ACCESS (Godot emits
// no barriers for it) and wrap it in COMMON. With legacy barriers, it is
// wrapped in the state Godot's last access left it in, one wrapper per state.
// The legacy path is untested.

namespace {

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

template <typename T>
T *driver_resource(RenderingDevice *p_rd,
                   RenderingDevice::DriverResource p_resource,
                   const RID &p_rid = RID()) {
  return reinterpret_cast<T *>(p_rd->get_driver_resource(p_resource, p_rid, 0));
}

bool has_enhanced_barriers(ID3D12Device *p_device) {
  D3D12_FEATURE_DATA_D3D12_OPTIONS12 options = {};
  return p_device &&
         SUCCEEDED(p_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS12,
                                                 &options, sizeof(options))) &&
         options.EnhancedBarriersSupported;
}

std::shared_ptr<SpoutD3D11Device> get_d3d11(RenderingDevice *p_rd) {
  return SpoutD3D11Device::get_on12(
      driver_resource<ID3D12Device>(
          p_rd, RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE),
      driver_resource<IUnknown>(
          p_rd, RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE));
}

void copy_on12(SpoutD3D11Device &p_d3d11, ID3D11Resource *p_dst,
               ID3D11Resource *p_src, ID3D11Resource *p_wrapped) {
  p_d3d11.on12()->AcquireWrappedResources(&p_wrapped, 1);
  p_d3d11.context()->CopyResource(p_dst, p_src);
  p_d3d11.on12()->ReleaseWrappedResources(&p_wrapped, 1);
  p_d3d11.context()->Flush();
}

} // namespace

// ---------------------------------------------------------------------------
// SpoutD3D12Texture

bool SpoutD3D12Texture::create(RenderingDevice *p_rd, uint32_t p_width,
                               uint32_t p_height, uint32_t p_dxgi_format,
                               bool p_storage) {
  release();
  _rd = p_rd;
  const RenderingDevice::DataFormat format = spout_dxgi_to_rd(p_dxgi_format);
  BitField<RenderingDevice::TextureUsageBits> usage =
      RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
  if (p_storage) {
    usage.set_flag(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
  }

  ID3D12Device *device = driver_resource<ID3D12Device>(
      p_rd, RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE);
  if (!has_enhanced_barriers(device)) {
    // Godot creates and tracks it.
    Ref<RDTextureFormat> fmt;
    fmt.instantiate();
    fmt->set_format(format);
    fmt->set_width(p_width);
    fmt->set_height(p_height);
    fmt->set_usage_bits(usage);
    Ref<RDTextureView> view;
    view.instantiate();
    _rid = p_rd->texture_create(fmt, view);
    return _rid.is_valid();
  }

  D3D12_HEAP_PROPERTIES heap = {};
  heap.Type = D3D12_HEAP_TYPE_DEFAULT;
  D3D12_RESOURCE_DESC desc = {};
  desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  desc.Width = p_width;
  desc.Height = p_height;
  desc.DepthOrArraySize = 1;
  desc.MipLevels = 1;
  desc.Format = static_cast<DXGI_FORMAT>(p_dxgi_format);
  desc.SampleDesc.Count = 1;
  desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_SIMULTANEOUS_ACCESS;
  if (p_storage) {
    desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }
  ID3D12Resource *resource = nullptr;
  const HRESULT hr = device->CreateCommittedResource(
      &heap, D3D12_HEAP_FLAG_NONE, &desc, D3D12_RESOURCE_STATE_COMMON, nullptr,
      IID_PPV_ARGS(&resource));
  if (FAILED(hr)) {
    spout_error("CreateCommittedResource failed (0x" +
                String::num_int64(static_cast<uint32_t>(hr), 16) + ").");
    return false;
  }
  _rid = p_rd->texture_create_from_extension(
      RenderingDevice::TEXTURE_TYPE_2D, format,
      RenderingDevice::TEXTURE_SAMPLES_1, usage,
      reinterpret_cast<uint64_t>(resource), p_width, p_height, 1, 1);
  if (!_rid.is_valid()) {
    resource->Release();
    return false;
  }
  _owned = resource;
  return true;
}

void SpoutD3D12Texture::release() {
  // D3D11 views before the D3D12 texture.
  spout_release(_wrapped_copy);
  spout_release(_wrapped_shader);
  if (_rid.is_valid()) {
    _rd->free_rid(_rid); // Godot defers this until its frames are done
    _rid = RID();
  }
  if (_owned) {
    ID3D12Resource *resource = _owned;
    spout_defer_release([resource]() { resource->Release(); });
    _owned = nullptr;
  }
}

ID3D11Resource *SpoutD3D12Texture::wrapped(SpoutD3D11Device &p_d3d11,
                                           SpoutGodotAccess p_access,
                                           bool p_written) {
  ID3D11Resource *&slot =
      p_access == SPOUT_ACCESS_SHADER ? _wrapped_shader : _wrapped_copy;
  if (slot || !_rid.is_valid()) {
    return slot;
  }
  ID3D12Resource *resource = driver_resource<ID3D12Resource>(
      _rd, RenderingDevice::DRIVER_RESOURCE_TEXTURE, _rid);
  if (!resource) {
    return nullptr;
  }
  const D3D12_RESOURCE_STATES state = simultaneous()
                                          ? D3D12_RESOURCE_STATE_COMMON
                                          : state_for(p_access, p_written);
  D3D11_RESOURCE_FLAGS flags = {}; // inferred from the D3D12 resource
  const HRESULT hr = p_d3d11.on12()->CreateWrappedResource(
      resource, &flags, state, state, IID_PPV_ARGS(&slot));
  if (FAILED(hr)) {
    spout_error("CreateWrappedResource failed (0x" +
                String::num_int64(static_cast<uint32_t>(hr), 16) + ").");
    slot = nullptr;
  }
  return slot;
}

// ---------------------------------------------------------------------------
// Sender

SpoutD3D12Sender::SpoutD3D12Sender(RenderingDevice *p_rd)
    : _rd(p_rd), _d3d11(get_d3d11(p_rd)) {}

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

  _dxgi_format = spout_send_format_to_dxgi(p_format);
  if (!_target.create(_rd, p_width, p_height, _dxgi_format, true)) {
    spout_error("Could not create the send texture.");
    return false;
  }
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

bool SpoutD3D12Sender::publish(SpoutGodotAccess p_access,
                               SpoutShareSender &p_share) {
  if (!_shared || p_access == SPOUT_ACCESS_NONE) {
    return false;
  }
  ID3D11Resource *wrapped = _target.wrapped(*_d3d11, p_access, true);
  if (!wrapped || !p_share.lock()) {
    return false;
  }
  copy_on12(*_d3d11, _shared, wrapped, wrapped);
  p_share.unlock();
  _d3d11->drain_debug_messages("sender publish");
  return true;
}

void SpoutD3D12Sender::release() {
  _target.release();
  spout_release(_shared);
  _share_handle = 0;
  _width = _height = 0;
}

// ---------------------------------------------------------------------------
// Receiver

SpoutD3D12Receiver::SpoutD3D12Receiver(RenderingDevice *p_rd)
    : _rd(p_rd), _d3d11(get_d3d11(p_rd)) {}

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
  uint32_t width = 0, height = 0, dxgi = 0;
  if (!_d3d11->open_shared_texture(p_info.share_handle, &_shared, width, height,
                                   dxgi, r_error)) {
    return false;
  }
  if (!_source.create(_rd, width, height, dxgi, false)) {
    r_error = "could not create the receive texture";
    close();
    return false;
  }
  _source_format = spout_dxgi_to_rd(dxgi);
  _d3d11->drain_debug_messages("receiver open");
  return true;
}

void SpoutD3D12Receiver::close() {
  _source.release();
  spout_release(_shared);
  _source_format = RenderingDevice::DATA_FORMAT_MAX;
}

bool SpoutD3D12Receiver::acquire(SpoutGodotAccess p_last_access,
                                 bool p_new_frame,
                                 SpoutShareReceiver &p_share) {
  if (!_shared || !_source.is_valid()) {
    return false;
  }
  if (p_last_access == SPOUT_ACCESS_NONE && !_source.simultaneous()) {
    // Legacy barriers: _source's state is unknown until Godot's first read.
    return false;
  }
  ID3D11Resource *wrapped = _source.wrapped(*_d3d11, p_last_access, false);
  if (!wrapped || !p_share.lock()) {
    return false;
  }
  copy_on12(*_d3d11, wrapped, _shared, wrapped);
  p_share.unlock();
  _d3d11->drain_debug_messages("receiver acquire");
  return true;
}
