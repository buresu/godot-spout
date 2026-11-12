#include "SpoutOpenGLGpu.hpp"

#ifdef GODOT_SPOUT_ENABLE_OPENGL

#include "SpoutCommon.hpp"

#include <Spout.h>

using namespace godot;

SpoutGLSender::SpoutGLSender() = default;

SpoutGLSender::~SpoutGLSender() { release(); }

bool SpoutGLSender::send(uint64_t p_gl_texture, uint32_t p_width,
                         uint32_t p_height, const String &p_name,
                         bool p_invert) {
  if (!_spout) {
    _spout = std::make_unique<Spout>();
  }
  if (_registered && p_name != _requested_name) {
    _spout->ReleaseSender();
    _registered = false;
  }
  if (!_registered) {
    _spout->SetSenderName(p_name.utf8().get_data());
    _requested_name = p_name;
  }
  // Some drivers (AMD) wait for the vsync'd swap in wglDXUnlockObjectsNV while
  // SpoutGL holds the access mutex, starving receivers. If so, wait first.
  if (_finish_first) {
    glFinish();
  }
  const uint64_t start = spout_ticks_usec();
  const bool ok =
      _spout->SendTexture(static_cast<GLuint>(p_gl_texture), GL_TEXTURE_2D,
                          p_width, p_height, p_invert);
  if (!_finish_first) {
    _slow_sends = spout_ticks_usec() - start > 4000 ? _slow_sends + 1 : 0;
    if (_slow_sends >= 10) {
      _finish_first = true;
      spout_log("SendTexture blocks in the GL/DX interop unlock; waiting for "
                "GL before it from now on.");
    }
  }
  if (ok && !_registered) {
    _registered = true;
    spout_log("Registered OpenGL sender \"" + get_name() + "\"");
  }
  return ok;
}

void SpoutGLSender::release() {
  if (_spout) {
    _spout->ReleaseSender();
    _spout.reset();
  }
  _registered = false;
}

String SpoutGLSender::get_name() const {
  return _spout ? String::utf8(_spout->GetName()) : String();
}

String SpoutGLSender::get_adapter_name() const {
  if (!_spout) {
    return String();
  }
  char name[256] = {};
  if (_spout->GetAdapterName(_spout->GetAdapter(), name, 256)) {
    return String::utf8(name);
  }
  return String();
}

#endif // GODOT_SPOUT_ENABLE_OPENGL
