#pragma once

// GPU backends, one per rendering driver. Godot's own textures are only
// touched through RD; Godot copies into a texture we create, and only that one
// is shared with D3D11 / Spout, so RD's state tracking stays correct.

#include <cstdint>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include "SpoutCommon.hpp"

namespace godot {

class SpoutShareSender;
class SpoutShareReceiver;
struct SpoutSenderInfo;

// How Godot last accessed a shared texture; D3D11On12 must acquire and release
// it in the matching D3D12 state.
enum SpoutGodotAccess {
  SPOUT_ACCESS_NONE = 0, // Godot hasn't touched it yet
  SPOUT_ACCESS_COPY,     // texture_copy (copy source or destination)
  SPOUT_ACCESS_SHADER,   // compute pass (storage write / sampled read)
};

// Frame RD is recording. Its GPU work is done get_frame_delay() swaps later.
uint64_t spout_render_frame();
bool spout_render_frame_done(RenderingDevice *p_rd, uint64_t p_frame);

class SpoutGpuSender {
public:
  virtual ~SpoutGpuSender() = default;

  // (Re)creates the target and shared texture; get_generation() changes when
  // the share handle must be re-published.
  virtual bool ensure(uint32_t p_width, uint32_t p_height,
                      SpoutSendFormat p_format) = 0;
  virtual uint64_t get_generation() const = 0;

  // Godot writes the frame here.
  virtual RID get_target() const = 0;
  virtual bool target_supports_storage() const = 0;

  virtual uint64_t get_share_handle() const = 0;
  virtual uint32_t get_dxgi_format() const = 0;
  virtual uint32_t get_width() const = 0;
  virtual uint32_t get_height() const = 0;

  // After the frame that wrote the target was submitted. False if skipped
  // (e.g. a receiver held the mutex).
  virtual bool publish(SpoutGodotAccess p_access,
                       SpoutShareSender &p_share) = 0;

  virtual void release() = 0;
  virtual String get_adapter_name() const = 0;
};

class SpoutGpuReceiver {
public:
  virtual ~SpoutGpuReceiver() = default;

  // r_error says why on failure (typically another GPU).
  virtual bool open(const SpoutSenderInfo &p_info, String &r_error) = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;

  // Godot reads the sender's frame from here.
  virtual RID get_source() const = 0;
  virtual RenderingDevice::DataFormat get_source_format() const = 0;
  virtual bool source_supports_copy() const = 0;

  // After the previous frame was submitted, to start a copy early.
  virtual void prefetch(SpoutGodotAccess p_last_access, bool p_new_frame,
                        SpoutShareReceiver &p_share) {}

  // Before Godot records a frame: makes the newest frame available in
  // get_source(). False if nothing was updated.
  virtual bool acquire(SpoutGodotAccess p_last_access, bool p_new_frame,
                       SpoutShareReceiver &p_share) = 0;

  virtual String get_adapter_name() const = 0;
};

// nullptr (error printed once) if the driver isn't supported.
SpoutGpuSender *spout_gpu_sender_create(RenderingDevice *p_rd,
                                        const String &p_driver);
SpoutGpuReceiver *spout_gpu_receiver_create(RenderingDevice *p_rd,
                                            const String &p_driver);

} // namespace godot
