#include "GDSpoutSender.hpp"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/godot.hpp>

#include <SpoutSender.h>
#include <Windows.h>

using namespace godot;

void GDSpoutSender::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_send_texture"),
                       &GDSpoutSender::_send_texture);
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &GDSpoutSender::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &GDSpoutSender::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &GDSpoutSender::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &GDSpoutSender::set_texture);

  // Add properties
  ClassDB::add_property("GDSpoutSender", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property("GDSpoutSender", {Variant::OBJECT, "texture"},
                        "set_texture", "get_texture");
}

GDSpoutSender::GDSpoutSender() : Node() {

  // Connect
  RenderingServer::get_singleton()->connect("frame_post_draw",
                                            {this, "_send_texture"});
}

GDSpoutSender::~GDSpoutSender() {

  // Disconnect
  RenderingServer::get_singleton()->disconnect("frame_post_draw",
                                               {this, "_send_texture"});

  // Release sender
  _release_sender();
}

void GDSpoutSender::_ready() {

  printf("GDSpoutSender::_ready\n");

  // Initialize
  _update_sender();
}

String GDSpoutSender::get_channel_name() const { return _channel_name; }

void GDSpoutSender::set_channel_name(String name) {
  _channel_name = name;
  _update_sender();
}

Ref<Texture> GDSpoutSender::get_texture() const { return _texture; }

void GDSpoutSender::set_texture(Ref<Texture> p_texture) {
  _texture = p_texture;
  _update_sender();
}

bool GDSpoutSender::_make_current() {
  // Make current context
  HDC hdc = wglGetCurrentDC();
  HGLRC hglrc = wglGetCurrentContext();
  return wglMakeCurrent(hdc, hglrc);
}

bool GDSpoutSender::_is_initialized() const { return _sender != nullptr; }

bool GDSpoutSender::_create_sender(const String &name) {

  printf("hoge1\n");

  // Check texture
  if (_texture.is_null()) {
    return false;
  }

  printf("hoge2\n");

  unsigned int width, height;
  char channel[256] = {};

  // Release sender
  _release_sender();

  // Check channel name
  CharString channel_name = name.utf8();
  if (channel_name.length() > 0) {
    strcpy_s(channel, channel_name.get_data());
  } else {
    return false;
  }

  printf("hoge3\n");

  // Sender size
  width = static_cast<unsigned int>(_texture->get_width());
  height = static_cast<unsigned int>(_texture->get_height());

  if (!(width > 0 && height > 0)) {
    return false;
  }

  printf("hoge4\n");

  // Create sender
  _sender = new SpoutSender();

  if (_make_current()) {
    printf("hoge5\n");
    if (_sender->CreateSender(channel, width, height)) {
      printf("hoge6\n");
      return true;
    }
  }

  _release_sender();

  return false;
}

void GDSpoutSender::_release_sender() {
  if (_is_initialized()) {
    if (_make_current()) {
      _sender->ReleaseSender();
    }
    delete _sender;
    _sender = nullptr;
  }
}

void GDSpoutSender::_update_sender() {

  printf("fuga1\n");

  // Check texture
  if (_texture.is_null()) {
    return;
  }

  printf("fuga2\n");

  unsigned int width, height;
  char channel[256] = {};

  // Check initialized
  if (!_is_initialized()) {

    printf("fuga3\n");
    if (!_create_sender(_channel_name)) {
      return;
    }
  }

  printf("fuga4\n");

  // Check channel name
  CharString channel_name = _channel_name.utf8();
  if (channel_name.length() > 0) {
    strcpy_s(channel, channel_name.get_data());
  } else {
    return;
  }

  printf("fuga5\n");

  // Sender size
  width = static_cast<unsigned int>(_texture->get_width());
  height = static_cast<unsigned int>(_texture->get_height());

  if (!(width > 0 && height > 0)) {
    return;
  }

  printf("fuga6\n");

  // Update sender
  if (_make_current()) {
    _sender->UpdateSender(channel, width, height);

    printf("fuga7\n");
  }
}

void GDSpoutSender::_send_texture() {

  // printf("GDSpoutSender::_send_texture\n");

  // Check texture
  if (_texture.is_null()) {
    return;
  }

  // Check initialized
  if (!_is_initialized()) {
    return;
  }

  // Get texture id
  RenderingServer *rs = RenderingServer::get_singleton();
  GLint tex_id = GLint(rs->texture_get_native_handle(_texture->get_rid()));

  // Get texture size
  GLint width = static_cast<unsigned int>(_texture->get_width());
  GLint height = static_cast<unsigned int>(_texture->get_height());

  // Send texture
  if (_make_current()) {
    _sender->SendTexture(tex_id, GL_TEXTURE_2D, width, height, false);
  }
}
