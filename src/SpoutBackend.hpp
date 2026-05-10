#pragma once

#include <cstdint>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/rid.hpp>

namespace godot {

class SpoutBackend {
public:
  virtual ~SpoutBackend() = default;

  virtual bool initialize(RenderingDevice *p_rd) = 0;
  virtual void release() = 0;
  virtual bool is_initialized() const = 0;

  virtual bool send(const RID &p_texture, uint32_t p_width, uint32_t p_height,
                    const char *p_channel_name) {
    return false;
  }

  virtual bool update_receiver(const char *p_channel_name) { return false; }

  virtual bool receive(const RID &p_texture, uint32_t p_width,
                       uint32_t p_height, const char *p_channel_name) {
    return false;
  }

  virtual uint32_t get_sender_width() const { return 0; }
  virtual uint32_t get_sender_height() const { return 0; }
};

} // namespace godot
