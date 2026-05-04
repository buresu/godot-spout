#include "GDSpoutInput.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <SpoutDX12.h>

using namespace godot;

void GDSpoutInput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_receive_texture"),
                       &GDSpoutInput::_receive_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &GDSpoutInput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &GDSpoutInput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &GDSpoutInput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &GDSpoutInput::set_texture);

  // Add properties
  ClassDB::add_property("GDSpoutInput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "GDSpoutInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2DRD"},
      "set_texture", "get_texture");
}

GDSpoutInput::GDSpoutInput() : Node() {
  RenderingServer::get_singleton()->connect("frame_pre_draw",
                                            {this, "_receive_texture"});
}

GDSpoutInput::~GDSpoutInput() {

  RenderingServer::get_singleton()->disconnect("frame_pre_draw",
                                               {this, "_receive_texture"});

  _release_receiver();
  _release_texture();
}

String GDSpoutInput::get_channel_name() const { return _channel_name; }

void GDSpoutInput::set_channel_name(String p_name) {
  _channel_name = p_name;
  _release_receiver();
}

Ref<Texture2DRD> GDSpoutInput::get_texture() const { return _texture; }

void GDSpoutInput::set_texture(Ref<Texture2DRD> p_texture) {
  if (_texture == p_texture) {
    return;
  }
  // Clear RID from old texture
  if (_texture.is_valid()) {
    _texture->set_texture_rd_rid(RID());
  }
  _texture = p_texture;
  // Apply current RID to new texture immediately if already receiving
  if (_texture.is_valid() && _rd_texture.is_valid()) {
    _texture->set_texture_rd_rid(_rd_texture);
  }
}

bool GDSpoutInput::_is_initialized() const { return _receiver != nullptr; }

bool GDSpoutInput::_create_receiver() {

  _release_receiver();

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  if (!rd) {
    return false;
  }

  String driver = rs->get_current_rendering_driver_name();
  if (driver != "d3d12") {
    ERR_PRINT("GDSpoutInput: Only Direct3D 12 rendering is supported. "
              "Current driver: " +
              driver);
    return false;
  }

  auto d3d12_device = reinterpret_cast<ID3D12Device *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                              RID(), 0));
  if (!d3d12_device) {
    return false;
  }

  auto command_queue = reinterpret_cast<IUnknown *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE,
                              RID(), 0));
  if (!command_queue) {
    return false;
  }

  _receiver = new spoutDX12();

  if (!_channel_name.is_empty()) {
    auto channel = _channel_name.utf8();
    _receiver->SetReceiverName(channel.get_data());
  }

  // spoutDX12's destructor calls Release() on the device even for external
  // devices, so we AddRef here to keep the balance.
  d3d12_device->AddRef();

  if (!_receiver->OpenDirectX12(d3d12_device, &command_queue)) {
    delete _receiver;
    _receiver = nullptr;
    return false;
  }

  return true;
}

void GDSpoutInput::_release_receiver() {

  if (_receiver) {
    _receiver->CloseDirectX12();
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
  }
}

bool GDSpoutInput::_create_texture(uint32_t p_width, uint32_t p_height) {

  auto rd = RenderingServer::get_singleton()->get_rendering_device();
  if (!rd) {
    return false;
  }

  _release_texture();

  Ref<RDTextureFormat> fmt;
  fmt.instantiate();
  fmt->set_format(RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM);
  fmt->set_width(p_width);
  fmt->set_height(p_height);
  fmt->set_depth(1);
  fmt->set_array_layers(1);
  fmt->set_mipmaps(1);
  fmt->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
  fmt->set_samples(RenderingDevice::TEXTURE_SAMPLES_1);
  fmt->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT);

  Ref<RDTextureView> view;
  view.instantiate();

  _rd_texture = rd->texture_create(fmt, view);
  if (!_rd_texture.is_valid()) {
    return false;
  }

  auto d3d12_texture = reinterpret_cast<ID3D12Resource *>(
      rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE,
                              _rd_texture, 0));
  if (!d3d12_texture) {
    rd->free_rid(_rd_texture);
    _rd_texture = RID();
    return false;
  }

  if (_texture.is_valid()) {
    _texture->set_texture_rd_rid(_rd_texture);
  }

  return true;
}

void GDSpoutInput::_release_texture() {

  if (_rd_texture.is_valid()) {
    if (_texture.is_valid()) {
      _texture->set_texture_rd_rid(RID());
    }
    auto rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd) {
      rd->free_rid(_rd_texture);
    }
    _rd_texture = RID();
  }
}

void GDSpoutInput::_receive_texture() {

  if (_texture.is_null()) {
    return;
  }

  if (!_is_initialized()) {
    if (!_create_receiver()) {
      return;
    }
  }

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  auto rd_tex_rid = _texture->get_texture_rd_rid();
  auto d3d12_texture = rd_tex_rid.is_valid()
      ? reinterpret_cast<ID3D12Resource *>(rd->get_driver_resource(
            RenderingDevice::DRIVER_RESOURCE_TEXTURE, rd_tex_rid, 0))
      : nullptr;

  if (_receiver->ReceiveDX12Resource(&d3d12_texture)) {
    if (_receiver->IsUpdated()) {
      auto width = _receiver->GetSenderWidth();
      auto height = _receiver->GetSenderHeight();
      _create_texture(width, height);
    }
  } else {
    _release_receiver();
  }
}
