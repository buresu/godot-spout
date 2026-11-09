#pragma once

// GPU side of godot-spout, one implementation per Godot rendering driver.
//
// The design rule that all backends follow: Godot's RenderingDevice owns every
// barrier on textures Godot uses. We never wrap, transition or copy one of
// Godot's own textures (viewport render targets, imported images) from
// outside. Godot copies (or runs a compute pass) into a texture *we* created,
// and only that texture is shared with D3D11 / Spout. This keeps Godot's
// resource-state / image-layout tracking correct, which the previous code
// broke.

#include <cstdint>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>

#include "SpoutCommon.hpp"

namespace godot {

class SpoutShareSender;
class SpoutShareReceiver;
struct SpoutSenderInfo;

// How Godot last accessed a texture that is also touched through D3D11On12.
// Godot's D3D12 driver tracks a resource state per texture; D3D11On12 must be
// told that same state when it acquires the resource and must hand it back in
// that state. Which state Godot uses depends on what it last did with the
// texture, so the cores record it each time they issue the Godot-side pass.
enum SpoutGodotAccess {
  SPOUT_ACCESS_NONE = 0, // Godot hasn't touched it yet
  SPOUT_ACCESS_COPY,     // texture_copy (copy source or destination)
  SPOUT_ACCESS_SHADER,   // compute pass (storage write / sampled read)
};

class SpoutGpuSender {
public:
  virtual ~SpoutGpuSender() = default;

  // Makes sure a target and a shared texture of this size and format exist.
  // Re-creates both when the size or format changed; get_generation() then
  // changes and the caller must re-publish the share handle.
  virtual bool ensure(uint32_t p_width, uint32_t p_height,
                      SpoutSendFormat p_format) = 0;
  virtual uint64_t get_generation() const = 0;

  // RD texture Godot writes the frame into (copy or compute storage write).
  virtual RID get_target() const = 0;
  virtual bool target_supports_storage() const = 0;

  virtual uint64_t get_share_handle() const = 0;
  virtual uint32_t get_dxgi_format() const = 0;
  virtual uint32_t get_width() const = 0;
  virtual uint32_t get_height() const = 0;

  // Called after Godot submitted the frame in which it wrote the target with
  // p_access. Makes that content visible in the shared texture (D3D12: one
  // D3D11On12 copy; Vulkan: nothing, the target *is* the shared memory).
  // Returns false if the frame was skipped (e.g. a receiver held the mutex).
  virtual bool publish(SpoutGodotAccess p_access,
                       SpoutShareSender &p_share) = 0;

  virtual void release() = 0;
  virtual String get_adapter_name() const = 0;
};

class SpoutGpuReceiver {
public:
  virtual ~SpoutGpuReceiver() = default;

  // Opens a sender's shared texture. On failure r_error says why (typically
  // the sender is on another GPU, see plan §1.5).
  virtual bool open(const SpoutSenderInfo &p_info, String &r_error) = 0;
  virtual void close() = 0;
  virtual bool is_open() const = 0;

  // RD texture holding the sender's frame for Godot to read (copy or sample).
  virtual RID get_source() const = 0;
  virtual RenderingDevice::DataFormat get_source_format() const = 0;
  virtual bool source_supports_copy() const = 0;

  // Makes the sender's newest frame available in get_source() for the frame
  // Godot is about to record. p_last_access is how Godot read the source in
  // the previous frame. Returns false if nothing was updated this time.
  virtual bool acquire(SpoutGodotAccess p_last_access,
                       SpoutShareReceiver &p_share) = 0;

  virtual String get_adapter_name() const = 0;
};

// Backend for the current rendering driver, or nullptr (with an error printed
// once) if the driver isn't supported for this direction.
SpoutGpuSender *spout_gpu_sender_create(RenderingDevice *p_rd,
                                        const String &p_driver);
SpoutGpuReceiver *spout_gpu_receiver_create(RenderingDevice *p_rd,
                                            const String &p_driver);

} // namespace godot
