#pragma once

// OpenGL (Compatibility) sender via SpoutGL. No RenderingDevice, so it doesn't
// implement SpoutGpuSender. Receiving is not implemented.

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

  // Render thread (Godot's GL context current).
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
  int _slow_sends = 0;
  bool _finish_first = false;
};

} // namespace godot

#else

namespace godot {
// For SpoutSenderCore's unique_ptr member.
class SpoutGLSender {};
} // namespace godot

#endif // GODOT_SPOUT_ENABLE_OPENGL
