#include "GDSpoutTexture.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <SpoutDX12.h>

using namespace godot;

void GDSpoutTexture::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_receive_texture"),
                       &GDSpoutTexture::_receive_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &GDSpoutTexture::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &GDSpoutTexture::set_channel_name);

  // Add properties
  ClassDB::add_property("GDSpoutTexture", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
}

GDSpoutTexture::GDSpoutTexture() : Texture2DRD() {
  RenderingServer::get_singleton()->connect("frame_pre_draw",
                                            {this, "_receive_texture"});
}

GDSpoutTexture::~GDSpoutTexture() {

  RenderingServer::get_singleton()->disconnect("frame_pre_draw",
                                               {this, "_receive_texture"});

  _release_receiver();
  _release_texture();
}

String GDSpoutTexture::get_channel_name() const { return _channel_name; }

void GDSpoutTexture::set_channel_name(String p_name) {
  _channel_name = p_name;
  _release_receiver();
}

bool GDSpoutTexture::_is_initialized() const { return _receiver != nullptr; }

bool GDSpoutTexture::_create_receiver() {

  _release_receiver();

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  if (!rd) {
    return false;
  }

  String driver = rs->get_current_rendering_driver_name();
  if (driver != "d3d12") {
    ERR_PRINT("GDSpoutTexture: Only Direct3D 12 rendering is supported. "
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

void GDSpoutTexture::_release_receiver() {

  if (_receiver) {
    _receiver->CloseDirectX12();
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
  }
}

bool GDSpoutTexture::_create_texture(uint32_t p_width, uint32_t p_height) {

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

  // Apply RID directly to self since GDSpoutTexture IS the texture
  set_texture_rd_rid(_rd_texture);

  return true;
}

void GDSpoutTexture::_release_texture() {

  if (_rd_texture.is_valid()) {
    set_texture_rd_rid(RID());
    auto rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd) {
      rd->free_rid(_rd_texture);
    }
    _rd_texture = RID();
  }
}

void GDSpoutTexture::_receive_texture() {

  if (!_is_initialized()) {
    if (!_create_receiver()) {
      return;
    }
  }

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  auto rd_tex_rid = get_texture_rd_rid();
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
