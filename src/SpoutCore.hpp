#pragma once

// Per-node logic shared by the Godot classes:
//   SpoutSenderCore   <- SpoutOutput
//   SpoutReceiverCore <- SpoutInput, SpoutTexture
//
// A core owns its GPU backend for as long as the node is active. Failures
// never destroy it; the core waits and retries with a back-off (the old code
// destroyed and re-created the backend every frame while a receiver had no
// sender).
//
// Threading: RenderingDevice may only be used on the render thread, but with
// the multi-threaded renderer RenderingServer emits frame_pre/post_draw on the
// main thread. So the scheduler (spout_core_start/stop) runs on_frame_*() and
// release() on the render thread, setters/getters go through mutex-guarded
// settings/status copies, and owner callbacks stop after detach().

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/variant/rid.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/vector2i.hpp>

#include "SpoutBlit.hpp"
#include "SpoutCommon.hpp"
#include "SpoutShare.hpp"

namespace godot {

class SpoutGpuSender;
class SpoutGpuReceiver;
class SpoutGLSender;

// True while Godot's RenderingServer exists. Destructors check this before
// touching it (a SpoutTexture resource can outlive the RenderingServer when
// a script constant holds it).
bool spout_rendering_available();

bool spout_is_main_thread();

// Texture2DRD emits "changed" on the render thread, which scene nodes reject;
// this re-emits it on the main thread.
void spout_set_texture_rd(Texture2DRD *p_texture, const RID &p_rid);

class SpoutCoreBase {
public:
  virtual ~SpoutCoreBase() = default;

  virtual void on_frame_pre_draw() {}
  virtual void on_frame_post_draw() {}
  // Frees GPU resources and unregisters from Spout. The core can be used
  // again afterwards (it re-creates everything lazily).
  virtual void release() = 0;

  // Stops owner callbacks; p_last runs under the same lock. Call before the
  // owner is destroyed.
  void detach(const std::function<void()> &p_last = nullptr);

protected:
  template <typename F> void with_owner(F &&p_fn) {
    std::lock_guard<std::mutex> guard(_owner_lock);
    if (!_detached) {
      p_fn();
    }
  }

private:
  std::mutex _owner_lock;
  bool _detached = false; // guarded by _owner_lock
};

void spout_core_start(const std::shared_ptr<SpoutCoreBase> &p_core);
// Also releases the core on the render thread.
void spout_core_stop(const std::shared_ptr<SpoutCoreBase> &p_core);
// Releases all cores on the render thread and waits for it.
void spout_cores_shutdown();

class SpoutSenderCore : public SpoutCoreBase {
public:
  enum Event { EVENT_STARTED, EVENT_STOPPED, EVENT_RESIZED };
  using EventHandler =
      std::function<void(Event, const String &, const Vector2i &)>;

  struct Status {
    bool sending = false;
    String active_name;
    Vector2i size;
    uint64_t last_cpu_usec = 0;
    uint64_t frames_sent = 0;
    uint64_t frames_skipped = 0;
    String adapter_name;
  };

  SpoutSenderCore();
  ~SpoutSenderCore() override;

  // Set once, before spout_core_start().
  void set_event_handler(EventHandler p_handler) { _events = p_handler; }

  void set_texture(const RID &p_texture);
  void set_name(const String &p_name);
  void set_enabled(bool p_enabled);
  void set_format(SpoutSendFormat p_format);
  void set_blit_options(const SpoutBlitOptions &p_options);
  void set_max_fps(double p_fps);
  // OpenGL only (see SpoutOutput::_resolve_gl_source).
  void set_gl_source(const RID &p_texture, const Vector2i &p_size);

  void on_frame_post_draw() override;
  void release() override;

  Status get_status() const;

private:
  struct Settings {
    RID texture;
    RID gl_source;
    Vector2i gl_size;
    String name = "godot-spout";
    bool enabled = true;
    SpoutSendFormat format = SPOUT_SEND_FORMAT_AUTO;
    SpoutBlitOptions options;
    double max_fps = 0.0;
  };

  void process_frame();
  void release_resources();
  void update_status();
  bool is_sending() const;
  String get_active_name() const;
  void stop();
  void publish_pending();
  void send_gl();
  void fail(const String &p_message);
  void emit(Event p_event, const String &p_name);

  EventHandler _events;

  mutable std::mutex _settings_lock;
  Settings _settings; // guarded by _settings_lock

  mutable std::mutex _status_lock;
  Status _status; // guarded by _status_lock

  // Everything below is only touched on the render thread.
  Settings _cur; // this frame's copy of _settings
  String _driver;
  RenderingDevice *_rd = nullptr;
  std::unique_ptr<SpoutGpuSender> _gpu;
  std::unique_ptr<SpoutBlit> _blit;
  std::unique_ptr<SpoutGLSender> _gl;
  SpoutShareSender _share;

  // The frame recorded in the previous post-draw, published in this one.
  SpoutGodotAccess _pending_access = SPOUT_ACCESS_NONE;
  uint64_t _pending_generation = 0;
  uint64_t _published_generation = 0;

  uint32_t _width = 0;
  uint32_t _height = 0;
  uint64_t _retry_at_msec = 0;
  uint64_t _last_send_usec = 0;
  uint64_t _last_cpu_usec = 0;
  uint64_t _frames_sent = 0;
  uint64_t _frames_skipped = 0;
  String _last_error;
};

class SpoutReceiverCore : public SpoutCoreBase {
public:
  enum State {
    STATE_IDLE,      // disabled or not started
    STATE_WAITING,   // no sender (yet)
    STATE_CONNECTED, // receiving
    STATE_ERROR,     // sender found but its texture can't be used
  };
  enum Event { EVENT_CONNECTED, EVENT_DISCONNECTED, EVENT_FRAME, EVENT_ERROR };
  // The string is the sender name, or the message for EVENT_ERROR.
  using EventHandler = std::function<void(Event, const String &)>;
  // Must switch synchronously: the previous texture is freed right after.
  using TextureHandler = std::function<void(const RID &)>;

  struct Status {
    State state = STATE_IDLE;
    String sender_name;
    Vector2i sender_size;
    uint32_t sender_dxgi_format = 0;
    double sender_fps = 0.0;
    int64_t sender_frame = 0;
    uint64_t frames_received = 0;
    String adapter_name;
    String last_error;
  };

  SpoutReceiverCore();
  ~SpoutReceiverCore() override;

  // Set once, before spout_core_start().
  void set_event_handler(EventHandler p_handler) { _events = p_handler; }
  void set_texture_handler(TextureHandler p_handler) { _texture = p_handler; }

  void set_name(const String &p_name);
  void set_enabled(bool p_enabled);
  void set_flip_y(bool p_flip);
  void set_clear_on_disconnect(bool p_clear);
  void set_poll_interval_msec(uint32_t p_msec);
  // SpoutInput.texture
  void set_target(const Ref<Texture2DRD> &p_target);

  void on_frame_pre_draw() override;
  void release() override;

  Status get_status() const;

private:
  struct Settings {
    String name;
    uint64_t name_serial = 0; // bumped whenever name changes
    bool enabled = true;
    bool clear_on_disconnect = false;
    SpoutBlitOptions options;
    uint32_t poll_interval = 250;
    Ref<Texture2DRD> target;
  };

  void read_settings();
  void process_frame();
  void release_resources();
  void update_status();
  String get_sender_name() const;
  void disconnect(bool p_emit);
  bool connect_to(const SpoutSenderInfo &p_info);
  bool ensure_visible();
  void update_target();
  void show(const RID &p_rid);
  void clear_shown();
  void emit(Event p_event, const String &p_arg);

  EventHandler _events;
  TextureHandler _texture;

  mutable std::mutex _settings_lock;
  Settings _settings; // guarded by _settings_lock

  mutable std::mutex _status_lock;
  Status _status; // guarded by _status_lock

  // Everything below is only touched on the render thread.
  Settings _cur; // this frame's copy of _settings
  Ref<Texture2DRD> _target;
  String _driver;
  RenderingDevice *_rd = nullptr;
  std::unique_ptr<SpoutGpuReceiver> _gpu;
  std::unique_ptr<SpoutBlit> _blit;
  SpoutShareReceiver _share;

  State _state = STATE_IDLE;
  RID _visible; // written this connection
  RID _shown;   // shown by the target / owner
  RenderingDevice::DataFormat _visible_format =
      RenderingDevice::DATA_FORMAT_MAX;
  bool _visible_flip = false;
  SpoutGodotAccess _last_access = SPOUT_ACCESS_NONE;
  bool _force_next = false;
  uint64_t _failed_handle = 0;
  uint64_t _next_poll_msec = 0;
  uint64_t _frames_received = 0;
  String _last_error;
};

} // namespace godot
