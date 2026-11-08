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
    // Rename: release the old registration; the shared texture and interop
    // objects are re-created by SpoutGL on the next send.
    _spout->ReleaseSender();
    _registered = false;
  }
  if (!_registered) {
    // Set once per registration. SpoutGL creates the sender (and appends _1
    // if the name is taken) on the first SendTexture.
    _spout->SetSenderName(p_name.utf8().get_data());
    _requested_name = p_name;
  }
  // SendTexture handles creation and resizing of the sender.
  const bool ok =
      _spout->SendTexture(static_cast<GLuint>(p_gl_texture), GL_TEXTURE_2D,
                          p_width, p_height, p_invert);
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
  // SpoutGL creates its D3D11 device on the adapter Spout is configured for
  // (SpoutSettings), the same way SpoutGL-based apps do.
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
