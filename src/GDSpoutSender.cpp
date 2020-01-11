#include "GDSpoutSender.hpp"
#include <SpoutSender.h>
#include <VisualServer.hpp>
#include <Windows.h>

using namespace godot;

void GDSpoutSender::_init() {

  // Connect
  VisualServer::get_singleton()->connect("frame_post_draw", this,
                                         "_send_texture");
}

void GDSpoutSender::_register_methods() {

  // Register method
  register_method("_send_texture", &GDSpoutSender::_send_texture);

  // Register property
  register_property<GDSpoutSender, String>(
      "channel_name", &GDSpoutSender::set_channel_name,
      &GDSpoutSender::get_channel_name, String(""));

  register_property<GDSpoutSender, Ref<Texture>>(
      "texture", &GDSpoutSender::set_texture, &GDSpoutSender::get_texture,
      Ref<Texture>(), GODOT_METHOD_RPC_MODE_DISABLED,
      GODOT_PROPERTY_USAGE_DEFAULT, GODOT_PROPERTY_HINT_RESOURCE_TYPE,
      String("Texture"));
}

GDSpoutSender::GDSpoutSender() : Node() {}

GDSpoutSender::~GDSpoutSender() {

  // Disconnect
  VisualServer::get_singleton()->disconnect("frame_post_draw", this,
                                            "_send_texture");

  // Release sender
  _release_sender();
}

String GDSpoutSender::get_channel_name() const { return _channel_name; }

void GDSpoutSender::set_channel_name(String name) {
  _channel_name = name;
  _update_sender();
}

Ref<Texture> GDSpoutSender::get_texture() const { return _texture; }

void GDSpoutSender::set_texture(Ref<Texture> texture) {
  _texture = texture;
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

  // Check texture
  if (_texture.is_null()) {
    return false;
  }

  unsigned int width, height;
  char channel[256] = {};

  // Release sender
  _release_sender();

  // Check channel name
  CharString channel_name = name.utf8();
  if (channel_name.length() > 0) {
    strcpy(channel, channel_name.get_data());
  } else {
    return false;
  }

  // Sender size
  width = static_cast<unsigned int>(_texture->get_width());
  height = static_cast<unsigned int>(_texture->get_height());

  if (!(width > 0 && height > 0)) {
    return false;
  }

  // Create sender
  _sender = new SpoutSender();

  if (_make_current()) {
    if (_sender->CreateSender(channel, width, height)) {
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

  // Check texture
  if (_texture.is_null()) {
    return;
  }

  unsigned int width, height;
  char channel[256] = {};

  // Check initialized
  if (!_is_initialized()) {
    if (!_create_sender(_channel_name)) {
      return;
    }
  }

  // Check channel name
  CharString channel_name = _channel_name.utf8();
  if (channel_name.length() > 0) {
    strcpy(channel, channel_name.get_data());
  } else {
    return;
  }

  // Sender size
  width = static_cast<unsigned int>(_texture->get_width());
  height = static_cast<unsigned int>(_texture->get_height());

  if (!(width > 0 && height > 0)) {
    return;
  }

  // Update sender
  if (_make_current()) {
    _sender->UpdateSender(channel, width, height);
  }
}

void GDSpoutSender::_send_texture() {

  // Check texture
  if (_texture.is_null()) {
    return;
  }

  // Check initialized
  if (!_is_initialized()) {
    return;
  }

  // Get texture id
  VisualServer *vs = VisualServer::get_singleton();
  GLint tex_id = GLint(vs->texture_get_texid(_texture->get_rid()));

  // Get texture size
  GLint width = static_cast<unsigned int>(_texture->get_width());
  GLint height = static_cast<unsigned int>(_texture->get_height());

  // Send texture
  if (_make_current()) {
    _sender->SendTexture(tex_id, GL_TEXTURE_2D, width, height, false);
  }
}
