#include "SpoutOutput.hpp"

#include "SpoutBackend.hpp"
#include "SpoutBackendFactory.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

using namespace godot;

void SpoutOutput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_send_texture"),
                       &SpoutOutput::_send_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &SpoutOutput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &SpoutOutput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &SpoutOutput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &SpoutOutput::set_texture);

  // Add properties
  ClassDB::add_property("SpoutOutput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "SpoutOutput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"},
      "set_texture", "get_texture");
}

SpoutOutput::SpoutOutput() : Node() {

  // Connect to post-draw signal
  RenderingServer::get_singleton()->connect("frame_post_draw",
                                            {this, "_send_texture"});
}

SpoutOutput::~SpoutOutput() {

  // Disconnect
  RenderingServer::get_singleton()->disconnect("frame_post_draw",
                                               {this, "_send_texture"});

  // Release sender
  _release_sender();
}

String SpoutOutput::get_channel_name() const { return _channel_name; }

void SpoutOutput::set_channel_name(String p_name) {
  _channel_name = p_name;
  _release_sender();
}

Ref<Texture> SpoutOutput::get_texture() const { return _texture; }

void SpoutOutput::set_texture(Ref<Texture> p_texture) {
  _texture = p_texture;
  _release_sender();
}

bool SpoutOutput::_is_initialized() const {
  return _backend != nullptr && _backend->is_initialized();
}

bool SpoutOutput::_create_sender() {

  if (_texture.is_null() || _channel_name.is_empty()) {
    return false;
  }

  _release_sender();

  auto rs = RenderingServer::get_singleton();
  String driver = rs->get_current_rendering_driver_name();
  auto rd = rs->get_rendering_device();

  RID backend_texture;
  if (driver == "opengl3") {
    backend_texture = _texture->get_rid();
  } else {
    if (!rd) {
      return false;
    }
    backend_texture = rs->texture_get_rd_texture(_texture->get_rid(), false);
  }
  if (!backend_texture.is_valid()) {
    return false;
  }

  _backend = spout_backend_create(driver, "SpoutOutput");
  if (!_backend || !_backend->initialize(rd)) {
    _release_sender();
    return false;
  }

  _backend_texture = backend_texture;
  return true;
}

void SpoutOutput::_release_sender() {

  if (_backend) {
    _backend->release();
    delete _backend;
    _backend = nullptr;
  }
  _backend_texture = RID();
}


void SpoutOutput::_send_texture() {

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

  auto rs = RenderingServer::get_singleton();
  auto driver = rs->get_current_rendering_driver_name();
  RID current_texture = driver == "opengl3"
                            ? _texture->get_rid()
                            : rs->texture_get_rd_texture(_texture->get_rid(),
                                                         false);

  if (current_texture != _backend_texture) {
    if (!_create_sender()) {
      return;
    }
  }

  auto channel = _channel_name.utf8();
  if (!_backend->send(_backend_texture, width, height, channel.get_data())) {
    _release_sender();
  }
}
