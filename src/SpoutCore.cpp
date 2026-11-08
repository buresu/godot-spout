#include "SpoutCore.hpp"

#include "SpoutGpu.hpp"
#include "SpoutOpenGLGpu.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

#include <mutex>
#include <set>

using namespace godot;

namespace {

std::mutex s_cores_lock;
std::set<SpoutCoreBase *> s_cores;

// After a failure (no backend, texture too large, ...) don't retry every
// frame; once a second is plenty and keeps the log readable.
constexpr uint64_t RETRY_DELAY_MSEC = 1000;

RenderingServer *rs() { return RenderingServer::get_singleton(); }

void free_rid_safe(RenderingDevice *p_rd, RID &p_rid) {
  if (p_rid.is_valid() && p_rd && spout_rendering_available()) {
    p_rd->free_rid(p_rid);
  }
  p_rid = RID();
}

} // namespace

bool godot::spout_rendering_available() {
  Engine *engine = Engine::get_singleton();
  return engine && engine->has_singleton("RenderingServer") &&
         RenderingServer::get_singleton() != nullptr;
}

SpoutCoreBase::SpoutCoreBase() {
  std::lock_guard<std::mutex> guard(s_cores_lock);
  s_cores.insert(this);
}

SpoutCoreBase::~SpoutCoreBase() {
  std::lock_guard<std::mutex> guard(s_cores_lock);
  s_cores.erase(this);
}

void SpoutCoreBase::release_all() {
  std::set<SpoutCoreBase *> cores;
  {
    std::lock_guard<std::mutex> guard(s_cores_lock);
    cores = s_cores;
  }
  for (SpoutCoreBase *core : cores) {
    core->release();
  }
}

// ===========================================================================
// Sender

SpoutSenderCore::SpoutSenderCore() = default;

SpoutSenderCore::~SpoutSenderCore() { release(); }

String SpoutSenderCore::get_adapter_name() const {
  if (_gpu) {
    return _gpu->get_adapter_name();
  }
#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (_gl) {
    return _gl->get_adapter_name();
  }
#endif
  return String();
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

void SpoutSenderCore::fail(const String &p_message) {
  if (p_message != _last_error) {
    spout_error("SpoutOutput \"" + _name + "\": " + p_message);
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
  // The copy recorded in the previous post-draw has been submitted with the
  // frame Godot just finished; make it visible to receivers. If the target was
  // re-created since (resize), that copy went into a texture that no longer
  // exists, so drop it.
  if (_pending_access == SPOUT_ACCESS_NONE || !_gpu ||
      _pending_generation != _gpu->get_generation()) {
    _pending_access = SPOUT_ACCESS_NONE;
    return;
  }
  if (_published_generation != _gpu->get_generation() ||
      _share.get_requested_name() != _name || !_share.is_registered()) {
    const bool renamed =
        _share.is_registered() && _share.get_requested_name() != _name;
    if (renamed) {
      const String old_name = _share.get_name();
      _share.unregister();
      emit(EVENT_STOPPED, old_name);
    }
    const bool resize = _share.is_registered();
    if (!_share.publish(_name, _gpu->get_share_handle(), _gpu->get_width(),
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
  const uint64_t start = spout_ticks_usec();

  if (!_enabled || _texture.is_null()) {
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
  if (_max_fps > 0.0 && _last_send_usec != 0 &&
      double(start - _last_send_usec) < 1.0e6 / _max_fps) {
    _last_cpu_usec = spout_ticks_usec() - start;
    return;
  }

  if (!_gpu || driver != _driver) {
    release();
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
    spout_log("SpoutOutput \"" + _name + "\" using " + driver + " on " +
              _gpu->get_adapter_name());
  }

  const RID source = server->texture_get_rd_texture(_texture->get_rid(), false);
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
      spout_resolve_send_format(_format, source_format->get_format());
  if (!_gpu->ensure(width, height, format)) {
    fail("could not create the shared texture.");
    return;
  }
  _width = width;
  _height = height;

  SpoutBlitOptions options = _options;
  if (!_gpu->target_supports_storage() && options.needs_shader()) {
    // Vulkan drivers that can't import shared textures with STORAGE usage:
    // send unprocessed rather than nothing.
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
    release(); // driver changed (only possible in tests)
  }
  if (!_gl) {
    _gl = std::make_unique<SpoutGLSender>();
    _driver = "opengl3";
  }
  RenderingServer *server = rs();
  const uint64_t handle =
      server->texture_get_native_handle(_texture->get_rid(), false);
  const uint32_t width = static_cast<uint32_t>(_texture->get_width());
  const uint32_t height = static_cast<uint32_t>(_texture->get_height());
  if (handle == 0 || width == 0 || height == 0) {
    return;
  }
  const bool was_registered = _gl->is_registered();
  const String old_name = _gl->get_name();
  const bool renamed =
      was_registered && old_name != _name && !old_name.begins_with(_name + "_");
  if (renamed) {
    emit(EVENT_STOPPED, old_name);
  }
  if (_gl->send(handle, width, height, _name, _options.flip_y)) {
    if (!was_registered || renamed) {
      emit(EVENT_STARTED, _gl->get_name());
    } else if (width != _width || height != _height) {
      emit(EVENT_RESIZED, _gl->get_name());
    }
    _width = width;
    _height = height;
    _frames_sent++;
  } else {
    _frames_skipped++;
  }
#endif
}

// ===========================================================================
// Receiver

SpoutReceiverCore::SpoutReceiverCore() = default;

SpoutReceiverCore::~SpoutReceiverCore() { release(); }

void SpoutReceiverCore::set_name(const String &p_name) {
  if (p_name == _name) {
    return;
  }
  _name = p_name;
  // Poll right away instead of waiting for the next interval.
  _next_poll_msec = 0;
  _failed_handle = 0;
}

String SpoutReceiverCore::get_adapter_name() const {
  return _gpu ? _gpu->get_adapter_name() : String();
}

String SpoutReceiverCore::get_sender_name() const {
  return _share.is_connected() ? _share.get_info().name : String();
}

Vector2i SpoutReceiverCore::get_sender_size() const {
  if (!_share.is_connected()) {
    return Vector2i();
  }
  return Vector2i(_share.get_info().width, _share.get_info().height);
}

uint32_t SpoutReceiverCore::get_sender_dxgi_format() const {
  return _share.is_connected() ? _share.get_info().dxgi_format : 0;
}

double SpoutReceiverCore::get_sender_fps() const {
  return is_connected() ? _share.get_sender_fps() : 0.0;
}

int64_t SpoutReceiverCore::get_sender_frame() const {
  return is_connected() ? _share.get_sender_frame() : 0;
}

void SpoutReceiverCore::show(const RID &p_rid) {
  if (p_rid == _shown) {
    return;
  }
  const RID old = _shown;
  _shown = p_rid;
  if (_texture) {
    _texture(_shown);
  }
  // Free the previous texture only after the Texture2DRD stopped using it. RD
  // defers the actual destruction until the GPU is done with it.
  if (old != _visible) {
    RID to_free = old;
    free_rid_safe(_rd, to_free);
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
  if (_clear_on_disconnect) {
    show(RID());
    free_rid_safe(_rd, _visible);
  }
  _state = _enabled ? STATE_WAITING : STATE_IDLE;
  if (p_emit && was_connected) {
    emit(EVENT_DISCONNECTED, name);
  }
}

void SpoutReceiverCore::release() {
  disconnect(true);
  show(RID());
  free_rid_safe(_rd, _visible);
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
  // The texture materials sample. Same format as the source when we can copy;
  // a storable format when the flip pass writes it.
  const RenderingDevice::DataFormat source_format = _gpu->get_source_format();
  RenderingDevice::DataFormat format = source_format;
  const bool shader = _options.needs_shader() || !_gpu->source_supports_copy();
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
      // Lets scripts read the received image (Texture2D.get_image()).
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
  if (shader) {
    usage.set_flag(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
  }
  fmt->set_usage_bits(usage);
  // Godot creates an sRGB view of every Texture2DRD whose format has an sRGB
  // variant (used by 3D materials with source_color). That view is only valid
  // if the texture was created with the variant as a shareable format.
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
    // Don't retry the same texture every frame; wait for the sender to
    // change it (or for the next slow poll).
    if (_failed_handle != p_info.share_handle) {
      _last_error = "Spout sender \"" + p_info.name + "\": " + error;
      spout_warn(_last_error);
      emit(EVENT_ERROR, p_info.name);
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

void SpoutReceiverCore::on_frame_pre_draw() {
  if (!_enabled) {
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
    release();
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

  // Look up the sender: every frame while connected (cheap, and how Spout's
  // own receivers notice resizes and closes), otherwise at the poll interval.
  const uint64_t now = spout_ticks_msec();
  if (_state != STATE_CONNECTED && now < _next_poll_msec) {
    return;
  }
  SpoutSenderInfo info;
  const SpoutShareReceiver::PollResult poll = _share.poll(_name, info);
  if (poll == SpoutShareReceiver::POLL_NO_SENDER ||
      poll == SpoutShareReceiver::POLL_CPU_SENDER) {
    if (poll == SpoutShareReceiver::POLL_CPU_SENDER && _last_error.is_empty()) {
      _last_error = "Spout sender \"" + _name +
                    "\" uses memory sharing (no GPU texture), which "
                    "godot-spout doesn't support.";
      spout_warn(_last_error);
    }
    if (_state == STATE_CONNECTED || _state == STATE_ERROR) {
      disconnect(true);
    }
    _state = STATE_WAITING;
    _next_poll_msec = now + _poll_interval;
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
  // The flip setting changed: the visible texture may need another format.
  if ((_options.needs_shader() || !_gpu->source_supports_copy()) !=
      _visible_flip) {
    if (!ensure_visible()) {
      return;
    }
  }

  const bool new_frame = _share.is_new_frame();
  if (!new_frame && !_force_next) {
    return;
  }
  const bool updated = _gpu->acquire(_last_access, _share);
  if (!updated && _last_access != SPOUT_ACCESS_NONE) {
    // Mutex busy; try again next frame.
    _force_next = true;
    return;
  }
  const SpoutGodotAccess access =
      _blit->run(_gpu->get_source(), _visible, _options);
  if (access == SPOUT_ACCESS_NONE) {
    return;
  }
  const bool first = _last_access == SPOUT_ACCESS_NONE;
  _last_access = access;
  if (first) {
    // That was the priming pass (see SpoutD3D12Receiver::acquire): copy the
    // real frame next time even if the sender doesn't signal a new one.
    _force_next = true;
    return;
  }
  _force_next = false;
  _frames_received++;
  show(_visible);
  emit(EVENT_FRAME, info.name);
}
