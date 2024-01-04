#include "GDSpoutTexture.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

#include <SpoutReceiver.h>

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

  // Connect
  RenderingServer::get_singleton()->connect("frame_pre_draw",
                                            {this, "_receive_texture"});

  // Get device
  _device = RenderingServer::get_singleton()->create_local_rendering_device();
}

GDSpoutTexture::~GDSpoutTexture() {

  // Disconnect
  RenderingServer::get_singleton()->disconnect("frame_pre_draw",
                                               {this, "_receive_texture"});

  // Release receiver
  _release_receiver();
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

  if (!_device) {
    return false;
  }

  printf("hoge!!1");

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

  if (_make_current()) {
    if (!_receiver->CreateReceiver(channel, width, height)) {
      _release_receiver();
      return false;
    }
  } else {
    return false;
  }

  printf("hoge!!6");

  // Set texture size
  _width = width;
  _height = height;

  printf("hoge!!7 %dx%d", width, height);

  printf("hoge!!8 %p", _device);

  auto format = Ref(new RDTextureFormat);

  format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
  format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UINT);
  format->set_width(width);
  format->set_height(height);
  format->set_depth(1);
  format->set_array_layers(1);
  format->set_mipmaps(1);
  format->set_usage_bits(RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
                         RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
                         RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
                         RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);

  _texture = _device->texture_create(format, Ref(new RDTextureView));

  set_texture_rd_rid(_texture);

  printf("hoge!!13 %dx%d", width, height);

  return true;
}

void GDSpoutTexture::_release_receiver() {
  if (_is_initialized()) {
    _receiver->ReleaseReceiver();
    delete _receiver;
    _receiver = nullptr;
    if (_texture.is_valid()) {
      _device->free_rid(_texture);
    }
    _width = 0;
    _height = 0;
  }
}

void GDSpoutTexture::_receive_texture() {

  printf("_receive_texture()\n");

  if (!_device) {
    return;
  }

  printf("fuga!!1");

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

  if (width != _width || height != _height) {
    if (!_create_receiver()) {
      return;
    }
  }

  printf("fuga!!6 %dx%d", width, height);

  // Get texture id
  auto tex_id =
      static_cast<GLuint>(_device->texture_get_native_handle(_texture));

  // Receive texture
  if (_make_current()) {
    auto res = _receiver->ReceiveTexture(channel, width, height, tex_id,
                                         GL_TEXTURE_2D, false);
    printf("fuga!!7 %d", res);
  }
}
