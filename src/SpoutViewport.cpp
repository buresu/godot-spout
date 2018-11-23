#include "SpoutViewport.hpp"
#include <SpoutSender.h>
#include <VisualServer.hpp>

#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6

using namespace godot;

void SpoutViewport::_init() {

  // Connect
  connect("size_changed", this, "_update_sender");
  VisualServer::get_singleton()->connect("frame_post_draw", this,
                                         "_send_texture");
}

void SpoutViewport::_register_methods() {

  // Register method
  register_method("_update_sender", &SpoutViewport::_update_sender);
  register_method("_send_texture", &SpoutViewport::_send_texture);

  // Register property
  register_property<SpoutViewport, String>(
      "channel_name", &SpoutViewport::set_channel_name,
      &SpoutViewport::get_channel_name, String(""));
}

SpoutViewport::SpoutViewport() : Viewport() {}

SpoutViewport::~SpoutViewport() {

  // Disconnect
  disconnect("size_changed", this, "_update_sender");
  VisualServer::get_singleton()->disconnect("frame_post_draw", this,
                                            "_send_texture");

  // Release sender
  _release_sender();
}

String SpoutViewport::get_channel_name() const { return _channel_name; }

void SpoutViewport::set_channel_name(String name) { _channel_name = name; }

bool SpoutViewport::_is_initialized() const { return _sender != nullptr; }

bool SpoutViewport::_create_sender(const String &name) {

  unsigned int width, height;
  char channel[256] = {};

  // Release receiver
  _release_sender();

  // Check channel name
  CharString channel_name = name.utf8();
  if (channel_name.length() > 0) {
    strcpy_s(channel, channel_name.length(), channel_name.get_data());
  } else {
    return false;
  }

  // Sender size
  width = static_cast<unsigned int>(get_size().width);
  height = static_cast<unsigned int>(get_size().height);

  // Create sender
  _sender = new SpoutSender();

  if (!_sender->CreateSender(channel, width, height)) {
    _release_sender();
    return false;
  }

  return true;
}

void SpoutViewport::_release_sender() {
  if (_is_initialized()) {
    _sender->ReleaseSender();
    delete _sender;
    _sender = nullptr;
  }
}

void SpoutViewport::_update_sender() {

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
    strcpy_s(channel, channel_name.length(), channel_name.get_data());
  } else {
    return;
  }

  // Sender size
  width = static_cast<unsigned int>(get_size().width);
  height = static_cast<unsigned int>(get_size().height);

  // Update sender
  _sender->UpdateSender(channel, width, height);
}

void SpoutViewport::_send_texture() {

  VisualServer *vs = VisualServer::get_singleton();

  // Check initialized
  if (!_is_initialized()) {
    return;
  }

  // Get texture id
  RID viewport_rid = get_viewport_rid();
  RID texture_rid = vs->viewport_get_texture(viewport_rid);

  GLint texture_id = GLint(vs->texture_get_texid(texture_rid));

  // Get texture size
  GLint texture_width = GLint(vs->texture_get_width(texture_rid));
  GLint texture_height = GLint(vs->texture_get_height(texture_rid));

  // Get fbo id
  // TODO: Get fbo id from godot api
  GLint fbo_id = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo_id);

  // Send texture
  _sender->SendTexture(texture_id, GL_TEXTURE_2D, texture_width, texture_height,
                       true, fbo_id);
}
