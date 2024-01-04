#include "GDSpoutTexture.hpp"

#include <godot_cpp/classes/rendering_server.hpp>

#include <SpoutReceiver.h>

using namespace godot;

void GDSpoutTexture::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("_get_rid"), &GDSpoutTexture::_get_rid);
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

GDSpoutTexture::GDSpoutTexture() : Texture2D() {

  printf("call!");

  // Connect
  RenderingServer::get_singleton()->connect("frame_pre_draw",
                                            {this, "_receive_texture"});
}

GDSpoutTexture::~GDSpoutTexture() {

  // Disconnect
  RenderingServer::get_singleton()->disconnect("frame_pre_draw",
                                               {this, "_receive_texture"});

  // Release receiver
  _release_receiver();

  // Free texture
  if (_texture.is_valid()) {
    RenderingServer::get_singleton()->free_rid(_texture);
  }
}

RID GDSpoutTexture::_get_rid() const {

  printf("_get_rid()\n");

  // Create texture
  if (!_texture.is_valid()) {
    auto rs = RenderingServer::get_singleton();
    _texture = rs->texture_2d_placeholder_create();
  }

  return _texture;
}

int32_t GDSpoutTexture::_get_width() const {
  printf("_get_width()\n");
  return _width;
}

int32_t GDSpoutTexture::_get_height() const {
  printf("_get_height()\n");
  return _height;
}

bool GDSpoutTexture::_has_alpha() const {
  printf("_has_alpha()\n");
  return false;
}

String GDSpoutTexture::get_channel_name() const { return _channel_name; }

void GDSpoutTexture::set_channel_name(String p_name) {
  printf("call!");
  _channel_name = p_name;
  //_create_receiver();
}

bool GDSpoutTexture::_make_current() {

  // Make current context
  HDC hdc = wglGetCurrentDC();
  HGLRC hglrc = wglGetCurrentContext();

  return wglMakeCurrent(hdc, hglrc);
}

bool GDSpoutTexture::_is_initialized() const { return _receiver != nullptr; }

bool GDSpoutTexture::_create_receiver() {

  printf("_create_receiver()\n");

  printf("hoge!!1");

  // Get rid
  auto rid = get_rid();

  printf("hoge!!2");

  // Channel name
  char channel[256] = "obs";
  auto buf = _channel_name.utf8();

  /*if (buf.length() > 0) {
    strcpy_s(channel, buf.length(), buf.get_data());
  } else {
    return false;
  }*/

  printf("hoge!!3");

  // Release receiver
  _release_receiver();

  printf("hoge!!4");

  // Create receiver
  _receiver = new SpoutReceiver();

  printf("hoge!!5");

  unsigned int width, height;

  if (!_receiver->CreateReceiver(channel, width, height)) {
    _release_receiver();
    return false;
  }

  printf("hoge!!6");

  // Set texture size
  _width = width;
  _height = height;

  auto rs = RenderingServer::get_singleton();
  rs->texture_set_size_override(rid, width, height);

  printf("hoge!!7 %dx%d", width, height);

  return true;
}

void GDSpoutTexture::_release_receiver() {
  if (_is_initialized()) {
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
    _width = 0;
    _height = 0;
  }
}

void GDSpoutTexture::_receive_texture() {

  printf("_receive_texture()\n");

  printf("fuga!!1");

  // Get rid
  auto rid = get_rid();

  printf("fuga!!2");

  // Check initialized
  if (!_is_initialized()) {
    if (!_create_receiver()) {
      return;
    }
  }

  printf("fuga!!3");

  // Channel name
  char channel[256] = "obs";
  /*auto buf = _channel_name.utf8();

  if (buf.length() > 0) {
    strcpy_s(channel, buf.length(), buf.get_data());
  } else {
    return;
  }*/

  printf("fuga!!4");

  // Check receiver
  bool connected;
  unsigned int width, height;

  if (!_receiver->CheckReceiver(channel, width, height, connected)) {
    return;
  }

  printf("fuga!!5 %d", connected);

  // Check connected
  /*if (!connected) {
    return;
  }*/

  printf("fuga!!6 %dx%d", width, height);

  // Get texture id
  auto rs = RenderingServer::get_singleton();
  auto tex_id = static_cast<GLuint>(rs->texture_get_native_handle(rid));

  // Receive texture
  auto res = _receiver->ReceiveTexture(channel, width, height, tex_id,
                                       GL_TEXTURE_2D, false);

  printf("fuga!!7 %d", res);
}
