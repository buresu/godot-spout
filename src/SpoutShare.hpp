#pragma once

// Spout protocol layer: sender registration, sender lookup, the per-sender
// access mutex and frame counting. No GPU work happens here; the GPU backends
// (SpoutD3D12*, SpoutVulkan*) create and copy the textures.
//
// Why not use spoutDX / spoutVK directly:
//   * spoutDX::ReleaseReceiver() calls Sleep(67) on the calling thread (Godot's
//     main/render thread) every time a sender goes away.
//   * spoutVK::SetSenderName() renames an already registered sender to
//     "<name>_1", and its receivers change the system-wide active sender.
//   * Neither lets us rename a sender without recreating its texture.
// These classes use the same low-level Spout2 building blocks
// (spoutSenderNames, spoutFrameCount) and stay wire-compatible with every
// Spout 2 application: same shared-memory layout, same mutex and semaphore
// names.

#include <cstdint>
#include <memory>

#include <godot_cpp/variant/packed_string_array.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// What a sender advertises in its shared memory.
struct SpoutSenderInfo {
  String name;
  uint64_t share_handle = 0; // legacy (KMT) DXGI shared handle
  uint32_t width = 0;
  uint32_t height = 0;
  uint32_t dxgi_format = 0; // as advertised; see spout_dxgi_normalize()

  bool same_texture(const SpoutSenderInfo &p_other) const {
    return share_handle == p_other.share_handle && width == p_other.width &&
           height == p_other.height && dxgi_format == p_other.dxgi_format;
  }
};

// Names of all senders currently registered on this machine.
PackedStringArray spout_get_sender_list();

class SpoutShareSender {
public:
  SpoutShareSender();
  ~SpoutShareSender();

  // Registers the sender, or updates its texture information when it is
  // already registered under p_requested_name. If the name is taken by
  // another sender, Spout picks "<name>_1", "<name>_2", ...; see get_name().
  bool publish(const String &p_requested_name, uint64_t p_share_handle,
               uint32_t p_width, uint32_t p_height, uint32_t p_dxgi_format);

  // Removes the sender from the registry. Safe to call when not registered.
  void unregister();

  bool is_registered() const { return _registered; }
  // The name receivers see (may differ from the requested one).
  String get_name() const;
  String get_requested_name() const { return _requested_name; }

  // Access mutex shared with receivers. Never waits longer than p_timeout_ms;
  // returns false if a receiver holds it (the caller skips this frame).
  bool lock(uint32_t p_timeout_ms);
  void unlock();

  // Tells receivers that a new frame is in the shared texture.
  void signal_frame();
  uint64_t get_frames_signalled() const { return _frames; }

private:
  struct Impl;
  std::unique_ptr<Impl> _impl;
  String _requested_name;
  bool _registered = false;
  uint64_t _frames = 0;
};

class SpoutShareReceiver {
public:
  enum PollResult {
    POLL_NO_SENDER,  // nothing (or no usable texture) under that name
    POLL_SAME,       // connected to the same texture as last time
    POLL_CHANGED,    // new sender, or the sender re-created its texture
    POLL_CPU_SENDER, // sender uses Spout's memory-share mode (unsupported)
  };

  SpoutShareReceiver();
  ~SpoutShareReceiver();

  // Looks up p_name (empty = the active sender) and reads its info. Cheap: one
  // named file mapping is opened and read; no allocation on the steady path.
  PollResult poll(const String &p_name, SpoutSenderInfo &r_info);

  // Forgets the current sender (mutex and frame counter handles are closed).
  void disconnect();
  bool is_connected() const { return _connected; }
  const SpoutSenderInfo &get_info() const { return _info; }

  bool lock(uint32_t p_timeout_ms);
  void unlock();

  // True if the sender signalled a frame since the last call. Senders with
  // frame counting disabled always report true.
  bool is_new_frame();
  double get_sender_fps() const;
  int64_t get_sender_frame() const;

private:
  void open_sync_objects(const String &p_name);

  struct Impl;
  std::unique_ptr<Impl> _impl;
  SpoutSenderInfo _info;
  bool _connected = false;
};

} // namespace godot
