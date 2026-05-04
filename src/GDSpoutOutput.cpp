#include "GDSpoutOutput.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <SpoutDX12.h>

using namespace godot;

void GDSpoutOutput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_send_texture"),
                       &GDSpoutOutput::_send_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &GDSpoutOutput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &GDSpoutOutput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &GDSpoutOutput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &GDSpoutOutput::set_texture);

  // Add properties
  ClassDB::add_property("GDSpoutOutput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "GDSpoutOutput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"},
      "set_texture", "get_texture");
}

GDSpoutOutput::GDSpoutOutput() : Node() {

  // Connect to post-draw signal
  RenderingServer::get_singleton()->connect("frame_post_draw",
                                            {this, "_send_texture"});
}

GDSpoutOutput::~GDSpoutOutput() {

  // Disconnect
  RenderingServer::get_singleton()->disconnect("frame_post_draw",
                                               {this, "_send_texture"});

  // Release sender
  _release_sender();
}

String GDSpoutOutput::get_channel_name() const { return _channel_name; }

void GDSpoutOutput::set_channel_name(String p_name) {
  _channel_name = p_name;
  _release_sender();
}

Ref<Texture> GDSpoutOutput::get_texture() const { return _texture; }

void GDSpoutOutput::set_texture(Ref<Texture> p_texture) {
  _texture = p_texture;
  _release_sender();
}

bool GDSpoutOutput::_is_initialized() const {
  return _sender != nullptr && _wrapped_resource != nullptr;
}

bool GDSpoutOutput::_create_sender() {

  if (_texture.is_null() || _channel_name.is_empty()) {
    return false;
  }

  _release_sender();

  // Get the main RenderingDevice
  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  if (!rd) {
    return false;
  }

  // Reject non-DirectX rendering drivers (Spout requires Direct3D 12)
  String driver = rs->get_current_rendering_driver_name();
  if (driver != "d3d12") {
    ERR_PRINT("GDSpoutOutput: Only Direct3D 12 rendering is supported. Current driver: " + driver);
    return false;
  }

  // Get the DX12 device (ID3D12Device*)
  auto d3d12_device = reinterpret_cast<ID3D12Device *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                               RID(), 0));
  if (!d3d12_device) {
    return false;
  }

  // Get the DX12 graphics command queue (ID3D12CommandQueue*)
  auto command_queue = reinterpret_cast<IUnknown *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE,
                               RID(), 0));
  if (!command_queue) {
    return false;
  }

  // Create the spoutDX12 sender
  _sender = new spoutDX12();

  // Set sender name before OpenDirectX12
  auto channel = _channel_name.utf8();
  _sender->SetSenderName(channel.get_data());

  // spoutDX12's destructor calls Release() on the device even for external
  // devices, so we AddRef here to keep the balance.
  d3d12_device->AddRef();

  // Initialize D3D11on12 using Godot's DX12 device and command queue
  if (!_sender->OpenDirectX12(d3d12_device, &command_queue)) {
    delete _sender;
    _sender = nullptr;
    return false;
  }

  // Get the RenderingDevice RID for the texture
  auto rd_tex_rid = rs->texture_get_rd_texture(_texture->get_rid(), false);
  if (!rd_tex_rid.is_valid()) {
    _release_sender();
    return false;
  }

  // Get the native ID3D12Resource* for the texture
  _d3d12_texture = reinterpret_cast<ID3D12Resource *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE,
                               rd_tex_rid, 0));
  if (!_d3d12_texture) {
    _release_sender();
    return false;
  }

  // Determine the DX12 resource state based on texture usage:
  // ViewportTexture / render target -> RENDER_TARGET
  // Regular sampled texture         -> PIXEL_SHADER_RESOURCE
  D3D12_RESOURCE_STATES in_state = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
  auto fmt = rd->texture_get_format(rd_tex_rid);
  if (fmt.is_valid()) {
    auto usage = fmt->get_usage_bits();
    if (usage & RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT) {
      in_state = D3D12_RESOURCE_STATE_RENDER_TARGET;
    }
  }

  // Wrap the DX12 resource as a D3D11on12 resource.
  if (!_sender->WrapDX12Resource(_d3d12_texture, &_wrapped_resource,
                                 in_state)) {
    _release_sender();
    return false;
  }

  return true;
}

void GDSpoutOutput::_release_sender() {

  // Release the wrapped D3D11on12 resource before destroying the sender
  if (_wrapped_resource) {
    _wrapped_resource->Release();
    _wrapped_resource = nullptr;
  }

  // _d3d12_texture is borrowed from Godot — do not Release
  _d3d12_texture = nullptr;

  if (_sender) {
    _sender->ReleaseSender();
    delete _sender; // ~spoutDX12 releases D3D11on12 internals + our AddRef'd device ref
    _sender = nullptr;
  }
}


void GDSpoutOutput::_send_texture() {

  if (_texture.is_null()) {
    return;
  }

  // Lazy initialization
  if (!_is_initialized()) {
    if (!_create_sender()) {
      return;
    }
  }

  auto width = static_cast<unsigned int>(_texture->get_width());
  auto height = static_cast<unsigned int>(_texture->get_height());

  // Recreate if the underlying D3D12 resource changed (e.g. ViewportTexture resize)
  // or if texture dimensions changed
  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  auto rd_tex_rid = rs->texture_get_rd_texture(_texture->get_rid(), false);
  auto current_d3d12 = rd_tex_rid.is_valid()
      ? reinterpret_cast<ID3D12Resource *>(rd->get_driver_resource(
            RenderingDevice::DRIVER_RESOURCE_TEXTURE, rd_tex_rid, 0))
      : nullptr;

  if (current_d3d12 != _d3d12_texture ||
      _sender->GetWidth() != width || _sender->GetHeight() != height) {
    if (!_create_sender()) {
      return;
    }
  }

  // Send the wrapped DX12 texture via D3D11on12
  _sender->SendDX11Resource(_wrapped_resource);
}
