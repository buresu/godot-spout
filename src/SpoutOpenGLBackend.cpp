#include "SpoutOpenGLBackend.hpp"

#ifdef GODOT_SPOUT_ENABLE_OPENGL

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/error_macros.hpp>

#include <Spout.h>

using namespace godot;

SpoutOpenGLBackend::~SpoutOpenGLBackend() { release(); }

bool SpoutOpenGLBackend::initialize(RenderingDevice *p_rd) {
  if (is_initialized()) {
    return true;
  }

  _spout = new Spout();
  return true;
}

void SpoutOpenGLBackend::release() {
  if (_spout) {
    _spout->ReleaseReceiver();
    _spout->ReleaseSender();
    delete _spout;
    _spout = nullptr;
  }
}

bool SpoutOpenGLBackend::is_initialized() const { return _spout != nullptr; }

bool SpoutOpenGLBackend::send(const RID &p_texture, uint32_t p_width,
                              uint32_t p_height, const char *p_channel_name) {
  if (!is_initialized() || !p_texture.is_valid() || p_width == 0 ||
      p_height == 0) {
    return false;
  }

  auto rs = RenderingServer::get_singleton();
  if (!rs) {
    return false;
  }

  const uint64_t native_handle = rs->texture_get_native_handle(p_texture, false);
  if (native_handle == 0) {
    ERR_PRINT("SpoutOpenGLBackend: Could not get OpenGL texture handle.");
    return false;
  }

  if (p_channel_name && p_channel_name[0]) {
    _spout->SetSenderName(p_channel_name);
  }

  return _spout->SendTexture(static_cast<GLuint>(native_handle), GL_TEXTURE_2D,
                             p_width, p_height, false);
}

bool SpoutOpenGLBackend::update_receiver(const char *p_channel_name) {
  ERR_PRINT_ONCE("SpoutOpenGLBackend: Receiving is not supported yet because "
                 "SpoutInput and SpoutTexture currently expose Texture2DRD.");
  return false;
}

bool SpoutOpenGLBackend::receive(const RID &p_texture, uint32_t p_width,
                                 uint32_t p_height,
                                 const char *p_channel_name) {
  return false;
}

#endif // GODOT_SPOUT_ENABLE_OPENGL
