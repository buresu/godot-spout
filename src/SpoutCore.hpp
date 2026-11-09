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
// Threading: on_frame_*() must run on the render thread (the Godot classes
// take care of that). Events are reported through a callback; the Godot
// classes turn them into deferred signal emissions.

#include <cstdint>
#include <functional>
#include <memory>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2d.hpp>
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

class SpoutCoreBase {
public:
  SpoutCoreBase();
  virtual ~SpoutCoreBase();
  // Frees GPU resources and unregisters from Spout. The core can be used
  // again afterwards (it re-creates everything lazily).
  virtual void release() = 0;
  // Called once at module shutdown for cores that are still alive.
  static void release_all();
};

class SpoutSenderCore : public SpoutCoreBase {
public:
  enum Event { EVENT_STARTED, EVENT_STOPPED, EVENT_RESIZED };
  using EventHandler = std::function<void(Event, const String &)>;

  SpoutSenderCore();
  ~SpoutSenderCore() override;

  void set_event_handler(EventHandler p_handler) { _events = p_handler; }

  // Settings; all can change at any time.
  void set_texture(const Ref<Texture2D> &p_texture) { _texture = p_texture; }
  void set_name(const String &p_name) { _name = p_name; }
  void set_enabled(bool p_enabled) { _enabled = p_enabled; }
  void set_format(SpoutSendFormat p_format) { _format = p_format; }
  void set_blit_options(const SpoutBlitOptions &p_options) {
    _options = p_options;
  }
  void set_max_fps(double p_fps) { _max_fps = p_fps; }
  // OpenGL only: RenderingServer texture to read the GL texture name from,
  // when _texture itself has none (a ViewportTexture's proxy; see
  // SpoutOutput::_resolve_gl_source).
  void set_gl_source(const RID &p_texture) { _gl_source = p_texture; }

  // Per frame, from RenderingServer's frame_post_draw.
  void on_frame_post_draw();
  void release() override;

  bool is_sending() const;
  String get_active_name() const;
  Vector2i get_size() const { return Vector2i(_width, _height); }
  uint64_t get_last_cpu_usec() const { return _last_cpu_usec; }
  uint64_t get_frames_sent() const { return _frames_sent; }
  uint64_t get_frames_skipped() const { return _frames_skipped; }
  String get_adapter_name() const;

private:
  void stop();
  void publish_pending();
  void send_gl();
  void fail(const String &p_message);
  void emit(Event p_event, const String &p_name) {
    if (_events) {
      _events(p_event, p_name);
    }
  }

  EventHandler _events;
  Ref<Texture2D> _texture;
  RID _gl_source;
  String _name = "godot-spout";
  bool _enabled = true;
  SpoutSendFormat _format = SPOUT_SEND_FORMAT_AUTO;
  SpoutBlitOptions _options;
  double _max_fps = 0.0;

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
  using EventHandler = std::function<void(Event, const String &)>;
  // Called when the RD texture that should be shown changes (RID() = none).
  using TextureHandler = std::function<void(const RID &)>;

  SpoutReceiverCore();
  ~SpoutReceiverCore() override;

  void set_event_handler(EventHandler p_handler) { _events = p_handler; }
  void set_texture_handler(TextureHandler p_handler) { _texture = p_handler; }

  void set_name(const String &p_name);
  void set_enabled(bool p_enabled) { _enabled = p_enabled; }
  void set_flip_y(bool p_flip) { _options.flip_y = p_flip; }
  void set_clear_on_disconnect(bool p_clear) { _clear_on_disconnect = p_clear; }
  void set_poll_interval_msec(uint32_t p_msec) { _poll_interval = p_msec; }

  // Per frame, from RenderingServer's frame_pre_draw.
  void on_frame_pre_draw();
  void release() override;

  State get_state() const { return _state; }
  bool is_connected() const { return _state == STATE_CONNECTED; }
  String get_sender_name() const;
  Vector2i get_sender_size() const;
  uint32_t get_sender_dxgi_format() const;
  double get_sender_fps() const;
  int64_t get_sender_frame() const;
  uint64_t get_frames_received() const { return _frames_received; }
  RID get_visible_texture() const { return _shown; }
  String get_adapter_name() const;
  String get_last_error() const { return _last_error; }

private:
  void disconnect(bool p_emit);
  bool connect_to(const SpoutSenderInfo &p_info);
  bool ensure_visible();
  void show(const RID &p_rid);
  void emit(Event p_event, const String &p_name) {
    if (_events) {
      _events(p_event, p_name);
    }
  }

  EventHandler _events;
  TextureHandler _texture;
  String _name;
  bool _enabled = true;
  bool _clear_on_disconnect = false;
  SpoutBlitOptions _options;
  uint32_t _poll_interval = 250;

  String _driver;
  RenderingDevice *_rd = nullptr;
  std::unique_ptr<SpoutGpuReceiver> _gpu;
  std::unique_ptr<SpoutBlit> _blit;
  SpoutShareReceiver _share;

  State _state = STATE_IDLE;
  RID _visible; // written this connection
  RID _shown;   // handed to the Texture2DRD
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
