#include "GDSpoutTexture.hpp"
#if 0
#include <SpoutReceiver.h>
#include <VisualServer.hpp>

#define GL_DRAW_FRAMEBUFFER_BINDING 0x8CA6

using namespace godot;

void GDSpoutTexture::_init() {
  // Set pre draw callback
  VisualServer::get_singleton()->connect("frame_pre_draw", this,
                                         "_receive_texture");

  // VisualServer::get_singleton()->texture_set_force_redraw_if_visible(texture,
  // true);
}

void GDSpoutTexture::_register_methods() {
  // Register method
  register_method("_receive_texture", &SpoutTexture::_receive_texture);
}

GDSpoutTexture::GDSpoutTexture() : Texture() {
  // Create texture
  _texture = VisualServer::get_singleton()->texture_create();
}

GDSpoutTexture::~GDSpoutTexture() {
  // Release receiver
  _release_receiver();

  // Free texture
  VisualServer::get_singleton()->free_rid(_texture);
}

bool GDSpoutTexture::_is_initialized() const { return _receiver != nullptr; }

bool GDSpoutTexture::_create_receiver(const String &name) {

  RID _texture = get_rid();

  unsigned int width, height;
  char channel[256] = {};

  VisualServer *vs = VisualServer::get_singleton();

  // Release receiver
  _release_receiver();

  // Check channel name
  CharString channel_name = name.utf8();
  if (channel_name.length() > 0) {
    strcpy_s(channel, channel_name.length(), channel_name.get_data());
  } else {
    return false;
  }

  // Create receiver
  _receiver = new SpoutReceiver();

  if (!_receiver->CreateReceiver(channel, width, height, true)) {
    _release_receiver();
    return false;
  }

  // Allocate texture
  vs->texture_allocate(_texture, width, height, 0, Image::FORMAT_RGBA8,
                       VisualServer::TEXTURE_TYPE_2D);

  return true;
}

void GDSpoutTexture::_release_receiver() {
  if (_is_initialized()) {
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
  }
}

void GDSpoutTexture::_receive_texture() {

  RID _texture = get_rid();

  bool connected;
  unsigned int width, height;
  char channel[256] = {};

  VisualServer *vs = VisualServer::get_singleton();

  // Check initialized
  if (!_is_initialized()) {
    return;
  }

  // Check channel name
  CharString channel_name = _channel_name.utf8();
  if (channel_name.length() > 0) {
    strcpy_s(channel, channel_name.length(), channel_name.get_data());
  } else {
    return;
  }

  // Check receiver
  if (!_receiver->CheckReceiver(channel, width, height, connected)) {
    return;
  }

  // Check connected
  if (!connected) {
    return;
  }

  // Check texture size
  if (width != _width || height != _height) {
    vs->texture_set_size_override(_texture, width, height, 0);
    _width = vs->texture_get_width(_texture);
    _height = vs->texture_get_height(_texture);
  }

  // Get texture id
  GLint tex_id = GLint(vs->texture_get_texid(_texture));

  // Get fbo id
  // TODO: Get fbo id from godot api
  GLint fbo_id = 0;
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &fbo_id);

  // Receive texture
  _receiver->ReceiveTexture(channel, width, height, tex_id, GL_TEXTURE_2D,
                            false, fbo_id);
}
#endif