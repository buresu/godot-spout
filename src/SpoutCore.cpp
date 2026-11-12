#include "SpoutCore.hpp"

#include "SpoutGpu.hpp"
#include "SpoutOpenGLGpu.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

#include <algorithm>
#include <vector>

using namespace godot;

namespace {

// Retry interval after a failure.
constexpr uint64_t RETRY_DELAY_MSEC = 1000;

RenderingServer *rs() { return RenderingServer::get_singleton(); }

void free_rid_safe(RenderingDevice *p_rd, RID &p_rid) {
  if (p_rid.is_valid() && p_rd && spout_rendering_available()) {
    p_rd->free_rid(p_rid);
  }
  p_rid = RID();
}

// Scheduler

using CoreList = std::vector<std::shared_ptr<SpoutCoreBase>>;

std::mutex s_sched_lock;
CoreList s_running;
CoreList s_releasing;
bool s_signals_connected = false;
bool s_shut_down = false;

CoreList running_snapshot() {
  std::lock_guard<std::mutex> guard(s_sched_lock);
  return s_running;
}

void process_releases() {
  CoreList releasing;
  {
    std::lock_guard<std::mutex> guard(s_sched_lock);
    releasing.swap(s_releasing);
  }
  for (const auto &core : releasing) {
    core->release();
  }
}

// Render thread. Swaps so far, counted at pre-draw (a deferred post-draw may
// run after the next draw).
uint64_t s_draws = 0;
uint64_t s_render_frame = 0;

void render_pre_draw() {
  s_render_frame = s_draws++;
  process_releases();
  for (const auto &core : running_snapshot()) {
    core->on_frame_pre_draw();
  }
}

void render_post_draw() {
  s_render_frame = s_draws;
  process_releases();
  for (const auto &core : running_snapshot()) {
    core->on_frame_post_draw();
  }
  spout_pump_deferred_releases();
}

// Static callables: a queued call never refers to a freed object.
void run_on_render_thread(void (*p_function)()) {
  RenderingServer *server = rs();
  if (server->is_on_render_thread()) {
    p_function();
  } else {
    server->call_on_render_thread(callable_mp_static(p_function));
  }
}

void on_frame_pre_draw_signal() { run_on_render_thread(&render_pre_draw); }

void on_frame_post_draw_signal() { run_on_render_thread(&render_post_draw); }

void connect_signals() {
  RenderingServer *server = rs();
  server->connect("frame_pre_draw",
                  callable_mp_static(&on_frame_pre_draw_signal));
  server->connect("frame_post_draw",
                  callable_mp_static(&on_frame_post_draw_signal));
}

void disconnect_signals() {
  RenderingServer *server = rs();
  server->disconnect("frame_pre_draw",
                     callable_mp_static(&on_frame_pre_draw_signal));
  server->disconnect("frame_post_draw",
                     callable_mp_static(&on_frame_post_draw_signal));
}

void shutdown_on_render_thread() {
  process_releases();
  spout_deferred_release_shutdown();
}

} // namespace

bool godot::spout_rendering_available() {
  Engine *engine = Engine::get_singleton();
  return engine && engine->has_singleton("RenderingServer") &&
         RenderingServer::get_singleton() != nullptr;
}

uint64_t godot::spout_render_frame() { return s_render_frame; }

bool godot::spout_render_frame_done(RenderingDevice *p_rd, uint64_t p_frame) {
  return s_render_frame >= p_frame + p_rd->get_frame_delay();
}

bool godot::spout_is_main_thread() {
  OS *os = OS::get_singleton();
  return os && os->get_thread_caller_id() == os->get_main_thread_id();
}

void godot::spout_set_texture_rd(Texture2DRD *p_texture, const RID &p_rid) {
  p_texture->set_texture_rd_rid(p_rid);
  if (!spout_is_main_thread()) {
    // By method name: the message queue checks the object is still alive.
    p_texture->call_deferred("emit_changed");
  }
}

void godot::spout_core_start(const std::shared_ptr<SpoutCoreBase> &p_core) {
  std::lock_guard<std::mutex> guard(s_sched_lock);
  if (s_shut_down || !spout_rendering_available()) {
    return;
  }
  if (!s_signals_connected) {
    connect_signals();
    s_signals_connected = true;
  }
  if (std::find(s_running.begin(), s_running.end(), p_core) ==
      s_running.end()) {
    s_running.push_back(p_core);
  }
}

void godot::spout_core_stop(const std::shared_ptr<SpoutCoreBase> &p_core) {
  {
    std::lock_guard<std::mutex> guard(s_sched_lock);
    auto it = std::find(s_running.begin(), s_running.end(), p_core);
    if (it == s_running.end()) {
      return;
    }
    s_running.erase(it);
    if (s_shut_down || !spout_rendering_available()) {
      return;
    }
    s_releasing.push_back(p_core);
  }
  // Now rather than next frame, so a stopped sender disappears promptly.
  run_on_render_thread(&process_releases);
}

void godot::spout_cores_shutdown() {
  bool connected = false;
  {
    std::lock_guard<std::mutex> guard(s_sched_lock);
    if (s_shut_down) {
      return;
    }
    s_shut_down = true;
    for (const auto &core : s_running) {
      s_releasing.push_back(core);
    }
    s_running.clear();
    connected = s_signals_connected;
    s_signals_connected = false;
  }
  if (!spout_rendering_available()) {
    return;
  }
  if (connected) {
    disconnect_signals();
  }
  // Wait: the queued calls refer to this library, which is about to unload.
  RenderingServer *server = rs();
  if (server->is_on_render_thread()) {
    shutdown_on_render_thread();
  } else {
    server->call_on_render_thread(
        callable_mp_static(&shutdown_on_render_thread));
    server->force_sync();
  }
}

void SpoutCoreBase::detach(const std::function<void()> &p_last) {
  std::lock_guard<std::mutex> guard(_owner_lock);
  if (_detached) {
    return;
  }
  _detached = true;
  if (p_last) {
    p_last();
  }
}

// ===========================================================================
// Sender

SpoutSenderCore::SpoutSenderCore() = default;

// No GPU work here: may run on any thread (the scheduler releases first).
SpoutSenderCore::~SpoutSenderCore() = default;

void SpoutSenderCore::set_texture(const RID &p_texture) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.texture = p_texture;
}

void SpoutSenderCore::set_name(const String &p_name) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.name = p_name;
}

void SpoutSenderCore::set_enabled(bool p_enabled) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.enabled = p_enabled;
}

void SpoutSenderCore::set_format(SpoutSendFormat p_format) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.format = p_format;
}

void SpoutSenderCore::set_blit_options(const SpoutBlitOptions &p_options) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.options = p_options;
}

void SpoutSenderCore::set_max_fps(double p_fps) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.max_fps = p_fps;
}

void SpoutSenderCore::set_gl_source(const RID &p_texture,
                                    const Vector2i &p_size) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.gl_source = p_texture;
  _settings.gl_size = p_size;
}

SpoutSenderCore::Status SpoutSenderCore::get_status() const {
  std::lock_guard<std::mutex> guard(_status_lock);
  return _status;
}

void SpoutSenderCore::update_status() {
  Status status;
  status.sending = is_sending();
  status.active_name = get_active_name();
  status.size = Vector2i(_width, _height);
  status.last_cpu_usec = _last_cpu_usec;
  status.frames_sent = _frames_sent;
  status.frames_skipped = _frames_skipped;
  if (_gpu) {
    status.adapter_name = _gpu->get_adapter_name();
  }
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  else if (_gl) {
    status.adapter_name = _gl->get_adapter_name();
  }
#endif
  std::lock_guard<std::mutex> guard(_status_lock);
  _status = status;
}

bool SpoutSenderCore::is_sending() const {
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (_gl) {
    return _gl->is_registered();
  }
#endif
  return _share.is_registered();
}

String SpoutSenderCore::get_active_name() const {
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (_gl) {
    return _gl->get_name();
  }
#endif
  return _share.is_registered() ? _share.get_name() : String();
}

void SpoutSenderCore::emit(Event p_event, const String &p_name) {
  if (_events) {
    const Vector2i size(_width, _height);
    with_owner([&]() { _events(p_event, p_name, size); });
  }
}

void SpoutSenderCore::fail(const String &p_message) {
  if (p_message != _last_error) {
    spout_error("SpoutOutput \"" + _cur.name + "\": " + p_message);
    _last_error = p_message;
  }
  _retry_at_msec = spout_ticks_msec() + RETRY_DELAY_MSEC;
}

void SpoutSenderCore::stop() {
  // Leaves the GPU resources in place so re-enabling is instant.
  const bool was_sending = is_sending();
  const String name = get_active_name();
  _share.unregister();
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (_gl) {
    _gl->release();
  }
#endif
  _pending_access = SPOUT_ACCESS_NONE;
  _published_generation = 0;
  if (was_sending) {
    emit(EVENT_STOPPED, name);
  }
}

void SpoutSenderCore::release() {
  release_resources();
  update_status();
}

void SpoutSenderCore::release_resources() {
  stop();
  _blit.reset(); // frees its RD objects
  _gpu.reset();
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  _gl.reset();
#endif
  _rd = nullptr;
  _driver = String();
  _width = _height = 0;
}

void SpoutSenderCore::publish_pending() {
  // Drop it if the target was re-created since.
  if (_pending_access == SPOUT_ACCESS_NONE || !_gpu ||
      _pending_generation != _gpu->get_generation()) {
    _pending_access = SPOUT_ACCESS_NONE;
    return;
  }
  if (_published_generation != _gpu->get_generation() ||
      _share.get_requested_name() != _cur.name || !_share.is_registered()) {
    const bool renamed =
        _share.is_registered() && _share.get_requested_name() != _cur.name;
    if (renamed) {
      const String old_name = _share.get_name();
      _share.unregister();
      emit(EVENT_STOPPED, old_name);
    }
    const bool resize = _share.is_registered();
    if (!_share.publish(_cur.name, _gpu->get_share_handle(), _gpu->get_width(),
                        _gpu->get_height(), _gpu->get_dxgi_format())) {
      _pending_access = SPOUT_ACCESS_NONE;
      fail("could not register the Spout sender.");
      return;
    }
    _published_generation = _gpu->get_generation();
    emit(resize ? EVENT_RESIZED : EVENT_STARTED, _share.get_name());
  }
  if (_gpu->publish(_pending_access, _share)) {
    _share.signal_frame();
    _frames_sent++;
  } else {
    _frames_skipped++;
  }
  _pending_access = SPOUT_ACCESS_NONE;
}

void SpoutSenderCore::on_frame_post_draw() {
  {
    std::lock_guard<std::mutex> guard(_settings_lock);
    _cur = _settings;
  }
  process_frame();
  update_status();
}

void SpoutSenderCore::process_frame() {
  const uint64_t start = spout_ticks_usec();

  if (!_cur.enabled || !_cur.texture.is_valid()) {
    if (is_sending()) {
      stop();
    }
    _last_cpu_usec = 0;
    return;
  }

  RenderingServer *server = rs();
  const String driver = server->get_current_rendering_driver_name();

#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (driver == "opengl3") {
    send_gl();
    _last_cpu_usec = spout_ticks_usec() - start;
    return;
  }
#endif

  // 1. Publish what was recorded during the previous frame.
  publish_pending();

  // 2. Record the copy of this frame's source.
  if (spout_ticks_msec() < _retry_at_msec) {
    return;
  }
  if (_cur.max_fps > 0.0 && _last_send_usec != 0 &&
      double(start - _last_send_usec) < 1.0e6 / _cur.max_fps) {
    _last_cpu_usec = spout_ticks_usec() - start;
    return;
  }

  if (!_gpu || driver != _driver) {
    release_resources();
    _driver = driver;
    _rd = server->get_rendering_device();
    _gpu.reset(spout_gpu_sender_create(_rd, driver));
    if (!_gpu) {
      fail("rendering driver \"" + driver + "\" is not supported for sending" +
           (driver == "vulkan"
                ? String(" (build with GODOT_SPOUT_ENABLE_VULKAN)")
                : String()) +
           ".");
      return;
    }
    _blit = std::make_unique<SpoutBlit>(_rd);
    spout_log("SpoutOutput \"" + _cur.name + "\" using " + driver + " on " +
              _gpu->get_adapter_name());
  }

  const RID source = server->texture_get_rd_texture(_cur.texture, false);
  if (!source.is_valid() || !_rd->texture_is_valid(source)) {
    // E.g. a ViewportTexture whose viewport isn't set up yet.
    _last_cpu_usec = spout_ticks_usec() - start;
    return;
  }
  Ref<RDTextureFormat> source_format = _rd->texture_get_format(source);
  const uint32_t width = source_format->get_width();
  const uint32_t height = source_format->get_height();
  const uint64_t max_size =
      _rd->limit_get(RenderingDevice::LIMIT_MAX_TEXTURE_SIZE_2D);
  if (width == 0 || height == 0 || width > max_size || height > max_size) {
    fail("texture size " + String::num_int64(width) + "x" +
         String::num_int64(height) + " is not supported (max " +
         String::num_int64(max_size) + ").");
    return;
  }

  const SpoutSendFormat format =
      spout_resolve_send_format(_cur.format, source_format->get_format());
  if (!_gpu->ensure(width, height, format)) {
    fail("could not create the shared texture.");
    return;
  }
  _width = width;
  _height = height;

  SpoutBlitOptions options = _cur.options;
  if (!_gpu->target_supports_storage() && options.needs_shader()) {
    // No STORAGE import (some Vulkan drivers): send unprocessed.
    options = SpoutBlitOptions();
  }
  const SpoutGodotAccess access =
      _blit->run(source, _gpu->get_target(), options);
  if (access == SPOUT_ACCESS_NONE) {
    fail("could not copy the texture (unsupported source format?).");
    return;
  }
  _pending_access = access;
  _pending_generation = _gpu->get_generation();
  _last_send_usec = start;
  _last_error = String();
  _last_cpu_usec = spout_ticks_usec() - start;
}

void SpoutSenderCore::send_gl() {
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (_gpu) {
    release_resources(); // driver changed (only possible in tests)
  }
  if (!_gl) {
    _gl = std::make_unique<SpoutGLSender>();
    _driver = "opengl3";
  }
  RenderingServer *server = rs();
  const uint64_t handle = server->texture_get_native_handle(
      _cur.gl_source.is_valid() ? _cur.gl_source : _cur.texture, false);
  const uint32_t width = static_cast<uint32_t>(_cur.gl_size.x);
  const uint32_t height = static_cast<uint32_t>(_cur.gl_size.y);
  if (handle == 0 || width == 0 || height == 0) {
    if (_last_error.is_empty()) {
      _last_error = "OpenGL: texture not ready (handle " +
                    String::num_int64(handle) + ", " +
                    String::num_int64(width) + "x" + String::num_int64(height) +
                    ")";
      spout_log("SpoutOutput \"" + _cur.name + "\": " + _last_error);
    }
    return;
  }
  const bool was_registered = _gl->is_registered();
  const String old_name = _gl->get_name();
  const bool renamed = was_registered && old_name != _cur.name &&
                       !old_name.begins_with(_cur.name + "_");
  if (renamed) {
    emit(EVENT_STOPPED, old_name);
  }
  _last_error = String();
  if (_gl->send(handle, width, height, _cur.name, _cur.options.flip_y)) {
    const bool resized = width != _width || height != _height;
    _width = width;
    _height = height;
    if (!was_registered || renamed) {
      emit(EVENT_STARTED, _gl->get_name());
    } else if (resized) {
      emit(EVENT_RESIZED, _gl->get_name());
    }
    _frames_sent++;
  } else {
    if (_frames_skipped++ == 0) {
      spout_log("SpoutOutput \"" + _cur.name +
                "\": SpoutGL SendTexture failed.");
    }
  }
#endif
}

// ===========================================================================
// Receiver

SpoutReceiverCore::SpoutReceiverCore() = default;

SpoutReceiverCore::~SpoutReceiverCore() = default;

void SpoutReceiverCore::set_name(const String &p_name) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  if (p_name != _settings.name) {
    _settings.name = p_name;
    _settings.name_serial++;
  }
}

void SpoutReceiverCore::set_enabled(bool p_enabled) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.enabled = p_enabled;
}

void SpoutReceiverCore::set_flip_y(bool p_flip) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.options.flip_y = p_flip;
}

void SpoutReceiverCore::set_clear_on_disconnect(bool p_clear) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.clear_on_disconnect = p_clear;
}

void SpoutReceiverCore::set_poll_interval_msec(uint32_t p_msec) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.poll_interval = p_msec;
}

void SpoutReceiverCore::set_target(const Ref<Texture2DRD> &p_target) {
  std::lock_guard<std::mutex> guard(_settings_lock);
  _settings.target = p_target;
}

SpoutReceiverCore::Status SpoutReceiverCore::get_status() const {
  std::lock_guard<std::mutex> guard(_status_lock);
  return _status;
}

void SpoutReceiverCore::update_status() {
  Status status;
  status.state = _state;
  if (_share.is_connected()) {
    const SpoutSenderInfo &info = _share.get_info();
    status.sender_name = info.name;
    status.sender_size = Vector2i(info.width, info.height);
    status.sender_dxgi_format = info.dxgi_format;
  }
  if (_state == STATE_CONNECTED) {
    status.sender_fps = _share.get_sender_fps();
    status.sender_frame = _share.get_sender_frame();
  }
  status.frames_received = _frames_received;
  if (_gpu) {
    status.adapter_name = _gpu->get_adapter_name();
  }
  status.last_error = _last_error;
  std::lock_guard<std::mutex> guard(_status_lock);
  _status = status;
}

String SpoutReceiverCore::get_sender_name() const {
  return _share.is_connected() ? _share.get_info().name : String();
}

void SpoutReceiverCore::emit(Event p_event, const String &p_arg) {
  if (_events) {
    with_owner([&]() { _events(p_event, p_arg); });
  }
}

void SpoutReceiverCore::read_settings() {
  Settings settings;
  {
    std::lock_guard<std::mutex> guard(_settings_lock);
    settings = _settings;
  }
  if (settings.name_serial != _cur.name_serial) {
    // Poll right away instead of waiting for the next interval.
    _next_poll_msec = 0;
    _failed_handle = 0;
  }
  _cur = settings;
}

void SpoutReceiverCore::update_target() {
  if (_cur.target == _target) {
    return;
  }
  if (_target.is_valid()) {
    spout_set_texture_rd(_target.ptr(), RID());
  }
  _target = _cur.target;
  if (_target.is_valid() && _shown.is_valid()) {
    spout_set_texture_rd(_target.ptr(), _shown);
  }
}

void SpoutReceiverCore::show(const RID &p_rid) {
  if (p_rid == _shown) {
    return;
  }
  const RID old = _shown;
  _shown = p_rid;
  if (_target.is_valid()) {
    spout_set_texture_rd(_target.ptr(), _shown);
  }
  if (_texture) {
    with_owner([&]() { _texture(_shown); });
  }
  // Both switches above are synchronous, so nothing uses the old one now.
  if (old != _visible) {
    RID to_free = old;
    free_rid_safe(_rd, to_free);
  }
}

void SpoutReceiverCore::clear_shown() {
  const RID shown = _shown;
  RID visible = _visible;
  _visible = RID();
  show(RID()); // frees `shown`
  if (visible != shown) {
    free_rid_safe(_rd, visible);
  }
}

void SpoutReceiverCore::disconnect(bool p_emit) {
  const bool was_connected = _state == STATE_CONNECTED;
  const String name = get_sender_name();
  if (_gpu) {
    _gpu->close();
  }
  _share.disconnect();
  _last_access = SPOUT_ACCESS_NONE;
  _present = false;
  if (_cur.clear_on_disconnect) {
    clear_shown();
  }
  _state = _cur.enabled ? STATE_WAITING : STATE_IDLE;
  if (p_emit && was_connected) {
    emit(EVENT_DISCONNECTED, name);
  }
}

void SpoutReceiverCore::release() {
  release_resources();
  // Re-read from the settings after a restart.
  _target.unref();
  _cur.target.unref();
  update_status();
}

void SpoutReceiverCore::release_resources() {
  disconnect(true);
  clear_shown();
  _visible_format = RenderingDevice::DATA_FORMAT_MAX;
  _blit.reset();
  _gpu.reset();
  _rd = nullptr;
  _driver = String();
  _state = STATE_IDLE;
  _failed_handle = 0;
  _next_poll_msec = 0;
}

bool SpoutReceiverCore::ensure_visible() {
  // The source format if copyable, else a storable one.
  const RenderingDevice::DataFormat source_format = _gpu->get_source_format();
  RenderingDevice::DataFormat format = source_format;
  const bool shader =
      _cur.options.needs_shader() || !_gpu->source_supports_copy();
  if (shader && !SpoutBlit::can_store(format)) {
    format = (source_format == RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM)
                 ? RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM
                 : RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT;
  }
  const SpoutSenderInfo &info = _share.get_info();
  if (_visible.is_valid() && _visible_format == format &&
      _visible_flip == shader) {
    Ref<RDTextureFormat> current = _rd->texture_get_format(_visible);
    if (current.is_valid() && current->get_width() == info.width &&
        current->get_height() == info.height) {
      return true;
    }
  }

  Ref<RDTextureFormat> fmt;
  fmt.instantiate();
  fmt->set_format(format);
  fmt->set_width(info.width);
  fmt->set_height(info.height);
  BitField<RenderingDevice::TextureUsageBits> usage =
      RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT |
      // For Texture2D.get_image().
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
  if (shader) {
    usage.set_flag(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
  }
  fmt->set_usage_bits(usage);
  // Godot creates an sRGB view of Texture2DRD, which needs this.
  const RenderingDevice::DataFormat srgb = spout_rd_srgb_variant(format);
  if (srgb != RenderingDevice::DATA_FORMAT_MAX && !shader) {
    fmt->add_shareable_format(format);
    fmt->add_shareable_format(srgb);
  }
  Ref<RDTextureView> view;
  view.instantiate();
  const RID created = _rd->texture_create(fmt, view);
  if (!created.is_valid()) {
    return false;
  }
  // Keep showing the previous texture until this one has a frame in it.
  if (_visible != _shown) {
    free_rid_safe(_rd, _visible);
  }
  _visible = created;
  _visible_format = format;
  _visible_flip = shader;
  return true;
}

bool SpoutReceiverCore::connect_to(const SpoutSenderInfo &p_info) {
  String error;
  if (!_gpu->open(p_info, error)) {
    // Report once per texture.
    if (_failed_handle != p_info.share_handle) {
      _last_error = "Spout sender \"" + p_info.name + "\": " + error;
      spout_warn(_last_error);
      emit(EVENT_ERROR, _last_error);
    }
    _failed_handle = p_info.share_handle;
    _state = STATE_ERROR;
    return false;
  }
  _failed_handle = 0;
  _last_access = SPOUT_ACCESS_NONE;
  if (!ensure_visible()) {
    _gpu->close();
    _state = STATE_ERROR;
    return false;
  }
  _force_next = true;
  _last_error = String();
  const bool was_connected = _state == STATE_CONNECTED;
  _state = STATE_CONNECTED;
  spout_log("Receiving \"" + p_info.name + "\" " +
            String::num_int64(p_info.width) + "x" +
            String::num_int64(p_info.height) + " " +
            spout_dxgi_format_name(p_info.dxgi_format));
  if (!was_connected) {
    emit(EVENT_CONNECTED, p_info.name);
  }
  return true;
}

// Started right after the previous frame, so asynchronous copies (Vulkan)
// are done by present().
void SpoutReceiverCore::on_frame_post_draw() {
  read_settings();
  process_frame();
  update_status();
}

void SpoutReceiverCore::on_frame_pre_draw() {
  update_target();
  present();
  update_status();
}

void SpoutReceiverCore::process_frame() {
  if (!_cur.enabled) {
    if (_state != STATE_IDLE) {
      disconnect(true);
      _state = STATE_IDLE;
    }
    return;
  }

  RenderingServer *server = rs();
  const String driver = server->get_current_rendering_driver_name();
  if (!_gpu || driver != _driver) {
    if (spout_ticks_msec() < _next_poll_msec) {
      return;
    }
    release_resources();
    _driver = driver;
    _rd = server->get_rendering_device();
    _gpu.reset(spout_gpu_receiver_create(_rd, driver));
    if (!_gpu) {
      if (_last_error.is_empty()) {
        _last_error =
            "Receiving is not supported on the \"" + driver +
            "\" rendering driver" +
            (driver == "opengl3"
                 ? String(" (Compatibility renderer).")
                 : String(".") + (driver == "vulkan"
                                      ? String(" Build with "
                                               "GODOT_SPOUT_ENABLE_VULKAN.")
                                      : String()));
        spout_error(_last_error);
      }
      _next_poll_msec = spout_ticks_msec() + 10 * RETRY_DELAY_MSEC;
      return;
    }
    _blit = std::make_unique<SpoutBlit>(_rd);
    _state = STATE_WAITING;
  }

  // Every frame while connected (notices resizes and closes), else at the poll
  // interval.
  const uint64_t now = spout_ticks_msec();
  if (_state != STATE_CONNECTED && now < _next_poll_msec) {
    return;
  }
  SpoutSenderInfo info;
  const SpoutShareReceiver::PollResult poll = _share.poll(_cur.name, info);
  if (poll == SpoutShareReceiver::POLL_NO_SENDER ||
      poll == SpoutShareReceiver::POLL_CPU_SENDER) {
    if (poll == SpoutShareReceiver::POLL_CPU_SENDER && _last_error.is_empty()) {
      _last_error = "Spout sender \"" + _cur.name +
                    "\" uses memory sharing (no GPU texture), which "
                    "godot-spout doesn't support.";
      spout_warn(_last_error);
    }
    if (_state == STATE_CONNECTED || _state == STATE_ERROR) {
      disconnect(true);
    }
    _state = STATE_WAITING;
    _next_poll_msec = now + _cur.poll_interval;
    return;
  }
  if (poll == SpoutShareReceiver::POLL_CHANGED || !_gpu->is_open()) {
    if (_state == STATE_ERROR && info.share_handle == _failed_handle) {
      _next_poll_msec = now + RETRY_DELAY_MSEC;
      return;
    }
    if (!connect_to(info)) {
      _next_poll_msec = now + RETRY_DELAY_MSEC;
      return;
    }
  }
  if ((_cur.options.needs_shader() || !_gpu->source_supports_copy()) !=
      _visible_flip) {
    if (!ensure_visible()) {
      return;
    }
  }

  const bool new_frame = _share.is_new_frame();
  if (!new_frame && !_force_next) {
    return;
  }
  _gpu->prefetch(_last_access, new_frame, _share);
  _present = true;
  _present_new = new_frame;
}

void SpoutReceiverCore::present() {
  if (!_present) {
    return;
  }
  _present = false;
  if (!_gpu || !_gpu->is_open() || _state != STATE_CONNECTED) {
    return;
  }
  const bool updated = _gpu->acquire(_last_access, _present_new, _share);
  if (!updated && _last_access != SPOUT_ACCESS_NONE) {
    // Mutex busy or copy still running; try again next frame.
    _force_next = true;
    return;
  }
  const SpoutGodotAccess access =
      _blit->run(_gpu->get_source(), _visible, _cur.options);
  if (access == SPOUT_ACCESS_NONE) {
    return;
  }
  const bool first = _last_access == SPOUT_ACCESS_NONE;
  _last_access = access;
  if (first) {
    // Priming pass: copy the real frame next time.
    _force_next = true;
    return;
  }
  _force_next = false;
  _frames_received++;
  show(_visible);
  emit(EVENT_FRAME, _share.get_info().name);
}
