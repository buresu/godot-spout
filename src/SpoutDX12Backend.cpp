#include "SpoutDX12Backend.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <SpoutDX12.h>

using namespace godot;

SpoutDX12Backend::~SpoutDX12Backend() { release(); }

bool SpoutDX12Backend::initialize(RenderingDevice *p_rd) {
  if (is_initialized()) {
    return true;
  }
  if (!p_rd) {
    return false;
  }

  _rd = p_rd;
  _device = reinterpret_cast<ID3D12Device *>(_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0));
  _queue = reinterpret_cast<IUnknown *>(_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, RID(), 0));
  if (!_device || !_queue) {
    release();
    return false;
  }

  _spout = new spoutDX12();
  return true;
}

void SpoutDX12Backend::release() {
  release_wrapped_texture();

  if (_spout) {
    if (_sender_open || _receiver_open) {
      _spout->CloseDirectX12();
    }
    if (_sender_open) {
      _spout->ReleaseSender();
    }
    if (_receiver_open) {
      _spout->ReleaseReceiver();
    }
    delete _spout;
    _spout = nullptr;
  }

  _rd = nullptr;
  _device = nullptr;
  _queue = nullptr;
  _sender_open = false;
  _receiver_open = false;
}

bool SpoutDX12Backend::is_initialized() const {
  return _spout != nullptr && _rd != nullptr && _device != nullptr &&
         _queue != nullptr;
}

bool SpoutDX12Backend::send(const RID &p_texture, uint32_t p_width,
                            uint32_t p_height, const char *p_channel_name) {
  if (!is_initialized() || !p_texture.is_valid()) {
    return false;
  }
  if (!open_sender(p_channel_name)) {
    return false;
  }

  auto current_texture = reinterpret_cast<ID3D12Resource *>(
      _rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE,
                               p_texture, 0));
  if (!current_texture) {
    return false;
  }

  if (current_texture != _send_texture || _spout->GetWidth() != p_width ||
      _spout->GetHeight() != p_height) {
    release_wrapped_texture();
    if (!wrap_send_texture(p_texture)) {
      return false;
    }
  }

  return _spout->SendDX11Resource(_wrapped_resource);
}

bool SpoutDX12Backend::update_receiver(const char *p_channel_name) {
  if (!is_initialized() || !open_receiver(p_channel_name)) {
    return false;
  }

  ID3D12Resource *texture = nullptr;
  return _spout->ReceiveDX12Resource(&texture);
}

bool SpoutDX12Backend::receive(const RID &p_texture, uint32_t p_width,
                               uint32_t p_height, const char *p_channel_name) {
  if (!is_initialized() || !p_texture.is_valid() ||
      !open_receiver(p_channel_name)) {
    return false;
  }

  auto texture = reinterpret_cast<ID3D12Resource *>(_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_TEXTURE, p_texture, 0));
  if (!texture) {
    return false;
  }

  if (!_spout->ReceiveDX12Resource(&texture)) {
    return false;
  }

  return p_width == 0 || p_height == 0 ||
         (_spout->GetSenderWidth() == p_width &&
          _spout->GetSenderHeight() == p_height) ||
         _spout->IsUpdated();
}

uint32_t SpoutDX12Backend::get_sender_width() const {
  return _spout ? _spout->GetSenderWidth() : 0;
}

uint32_t SpoutDX12Backend::get_sender_height() const {
  return _spout ? _spout->GetSenderHeight() : 0;
}

bool SpoutDX12Backend::open_sender(const char *p_channel_name) {
  if (_sender_open) {
    return true;
  }
  if (_receiver_open) {
    return false;
  }

  if (p_channel_name && p_channel_name[0]) {
    _spout->SetSenderName(p_channel_name);
  }

  _device->AddRef();
  if (!_spout->OpenDirectX12(_device, &_queue)) {
    return false;
  }

  _sender_open = true;
  return true;
}

bool SpoutDX12Backend::open_receiver(const char *p_channel_name) {
  if (_receiver_open) {
    return true;
  }
  if (_sender_open) {
    return false;
  }

  if (p_channel_name && p_channel_name[0]) {
    _spout->SetReceiverName(p_channel_name);
  }

  _device->AddRef();
  if (!_spout->OpenDirectX12(_device, &_queue)) {
    return false;
  }

  _receiver_open = true;
  return true;
}

bool SpoutDX12Backend::wrap_send_texture(const RID &p_texture) {
  _send_texture = reinterpret_cast<ID3D12Resource *>(_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_TEXTURE, p_texture, 0));
  if (!_send_texture) {
    return false;
  }

  D3D12_RESOURCE_STATES in_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  auto fmt = _rd->texture_get_format(p_texture);
  if (fmt.is_valid() &&
      (fmt->get_usage_bits() &
       RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)) {
    in_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
  }

  if (!_spout->WrapDX12Resource(_send_texture, &_wrapped_resource, in_state)) {
    release_wrapped_texture();
    return false;
  }

  return true;
}

void SpoutDX12Backend::release_wrapped_texture() {
  if (_wrapped_resource) {
    _wrapped_resource->Release();
    _wrapped_resource = nullptr;
  }
  _send_texture = nullptr;
}
