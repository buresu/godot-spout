#pragma once

#ifdef GODOT_SPOUT_ENABLE_OPENGL

#include "SpoutBackend.hpp"

class Spout;

namespace godot {

class SpoutOpenGLBackend : public SpoutBackend {
public:
  SpoutOpenGLBackend() = default;
  ~SpoutOpenGLBackend() override;

  bool initialize(RenderingDevice *p_rd) override;
  void release() override;
  bool is_initialized() const override;

  bool send(const RID &p_texture, uint32_t p_width, uint32_t p_height,
            const char *p_channel_name) override;
  bool update_receiver(const char *p_channel_name) override;
  bool receive(const RID &p_texture, uint32_t p_width, uint32_t p_height,
               const char *p_channel_name) override;

private:
  Spout *_spout = nullptr;
};

} // namespace godot

#endif // GODOT_SPOUT_ENABLE_OPENGL
