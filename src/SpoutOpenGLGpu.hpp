#pragma once

// OpenGL (Compatibility renderer) sender, GODOT_SPOUT_ENABLE_OPENGL.
//
// The GL path doesn't go through RenderingDevice, so it doesn't fit the
// SpoutGpuSender interface: SpoutGL's Spout class does everything (GL/DX
// interop when the driver supports WGL_NV_DX_interop, otherwise a CPU
// readback, which SpoutGL picks on its own). Receiving on OpenGL is not
// implemented (plan P2-1).

#ifdef GODOT_SPOUT_ENABLE_OPENGL

#include <cstdint>
#include <memory>

#include <godot_cpp/variant/string.hpp>

class Spout;

namespace godot {

class SpoutGLSender {
public:
  SpoutGLSender();
  ~SpoutGLSender();

  // Sends the GL texture p_gl_texture. Registers (or renames) the sender
  // first if needed. Must run with Godot's GL context current, i.e. on the
  // render thread.
  bool send(uint64_t p_gl_texture, uint32_t p_width, uint32_t p_height,
            const String &p_name, bool p_invert);
  void release();

  bool is_registered() const { return _registered; }
  String get_name() const;
  String get_adapter_name() const;

private:
  std::unique_ptr<Spout> _spout;
  String _requested_name;
  bool _registered = false;
};

} // namespace godot

#endif // GODOT_SPOUT_ENABLE_OPENGL
