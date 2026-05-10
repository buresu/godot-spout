#include "SpoutTexture.hpp"

#include "SpoutBackend.hpp"
#include "SpoutBackendFactory.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void SpoutTexture::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_receive_texture"),
                       &SpoutTexture::_receive_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &SpoutTexture::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &SpoutTexture::set_channel_name);

  // Add properties
  ClassDB::add_property("SpoutTexture", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
}

SpoutTexture::SpoutTexture() : Texture2DRD() {
  RenderingServer::get_singleton()->connect("frame_pre_draw",
                                            {this, "_receive_texture"});
}

SpoutTexture::~SpoutTexture() {

  RenderingServer::get_singleton()->disconnect("frame_pre_draw",
                                               {this, "_receive_texture"});

  _release_receiver();
  _release_texture();
}

String SpoutTexture::get_channel_name() const { return _channel_name; }

void SpoutTexture::set_channel_name(String p_name) {
  _channel_name = p_name;
  _release_receiver();
}

bool SpoutTexture::_is_initialized() const {
  return _backend != nullptr && _backend->is_initialized();
}

bool SpoutTexture::_create_receiver() {

  _release_receiver();

  auto rs = RenderingServer::get_singleton();
  auto rd = rs->get_rendering_device();
  if (!rd) {
    return false;
  }

  _backend = spout_backend_create(rs->get_current_rendering_driver_name(),
                                  "SpoutTexture");
  if (!_backend || !_backend->initialize(rd)) {
    _release_receiver();
    return false;
  }

  return true;
}

void SpoutTexture::_release_receiver() {

  if (_backend) {
    _backend->release();
    delete _backend;
    _backend = nullptr;
  }
}

bool SpoutTexture::_create_texture(uint32_t p_width, uint32_t p_height) {

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

  // Apply RID directly to self since SpoutTexture IS the texture
  set_texture_rd_rid(_rd_texture);

  return true;
}

void SpoutTexture::_release_texture() {

  if (_rd_texture.is_valid()) {
    set_texture_rd_rid(RID());
    auto rd = RenderingServer::get_singleton()->get_rendering_device();
    if (rd) {
      rd->free_rid(_rd_texture);
    }
    _rd_texture = RID();
  }
}

void SpoutTexture::_receive_texture() {

  if (!_is_initialized()) {
    if (!_create_receiver()) {
      return;
    }
  }

  auto channel = _channel_name.utf8();
  if (!_backend->update_receiver(channel.get_data())) {
    _release_receiver();
    return;
  }

  auto width = _backend->get_sender_width();
  auto height = _backend->get_sender_height();
  if (width == 0 || height == 0) {
    return;
  }

  if (!_rd_texture.is_valid() || static_cast<uint32_t>(get_width()) != width ||
      static_cast<uint32_t>(get_height()) != height) {
    if (!_create_texture(width, height)) {
      return;
    }
  }

  if (!_backend->receive(_rd_texture, width, height, channel.get_data())) {
    _release_receiver();
  }
}
