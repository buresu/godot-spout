#pragma once

// The Godot-side half of every transfer: records "source -> destination" into
// the frame RenderingDevice is currently recording, either as a plain
// texture_copy (when nothing has to change) or as a small compute pass that
// can flip, force alpha to 1, convert between linear and sRGB encoding, and
// convert formats.
//
// Because this runs through RenderingDevice, Godot inserts all barriers and
// tracks all resource states / image layouts itself.

#include <cstdint>
#include <map>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/rid.hpp>

#include "SpoutGpu.hpp"

namespace godot {

enum SpoutColorConversion {
  SPOUT_COLOR_NONE = 0,
  SPOUT_COLOR_LINEAR_TO_SRGB = 1,
  SPOUT_COLOR_SRGB_TO_LINEAR = 2,
};

struct SpoutBlitOptions {
  bool flip_y = false;
  bool force_opaque = false;
  SpoutColorConversion color = SPOUT_COLOR_NONE;

  bool needs_shader() const {
    return flip_y || force_opaque || color != SPOUT_COLOR_NONE;
  }
};

class SpoutBlit {
public:
  explicit SpoutBlit(RenderingDevice *p_rd);
  ~SpoutBlit();

  // Records the transfer. Returns how Godot accessed both textures, or
  // SPOUT_ACCESS_NONE if neither a copy nor the compute pass is possible (the
  // reason is printed once).
  SpoutGodotAccess run(const RID &p_source, const RID &p_dest,
                       const SpoutBlitOptions &p_options);

  // True if the compute pass can write this format (image2D qualifier).
  static bool can_store(RenderingDevice::DataFormat p_format);

  void release();

private:
  struct Pipeline {
    RID shader;
    RID pipeline;
  };
  const Pipeline *get_pipeline(RenderingDevice::DataFormat p_format);

  RenderingDevice *_rd = nullptr;
  RID _sampler;
  std::map<int, Pipeline> _pipelines;
  RID _uniform_set;
  RID _set_source;
  RID _set_dest;
  bool _warned = false;
};

} // namespace godot
