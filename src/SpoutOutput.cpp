#include "SpoutOutput.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/scene_tree.hpp>
#include <godot_cpp/classes/viewport.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>
#include <godot_cpp/classes/window.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutOutput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &SpoutOutput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &SpoutOutput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &SpoutOutput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &SpoutOutput::set_texture);
  ClassDB::bind_method(D_METHOD("is_enabled"), &SpoutOutput::is_enabled);
  ClassDB::bind_method(D_METHOD("set_enabled", "enabled"),
                       &SpoutOutput::set_enabled);
  ClassDB::bind_method(D_METHOD("is_active_in_editor"),
                       &SpoutOutput::is_active_in_editor);
  ClassDB::bind_method(D_METHOD("set_active_in_editor", "active"),
                       &SpoutOutput::set_active_in_editor);
  ClassDB::bind_method(D_METHOD("get_format"), &SpoutOutput::get_format);
  ClassDB::bind_method(D_METHOD("set_format", "format"),
                       &SpoutOutput::set_format);
  ClassDB::bind_method(D_METHOD("get_flip_y"), &SpoutOutput::get_flip_y);
  ClassDB::bind_method(D_METHOD("set_flip_y", "flip"),
                       &SpoutOutput::set_flip_y);
  ClassDB::bind_method(D_METHOD("get_alpha_mode"),
                       &SpoutOutput::get_alpha_mode);
  ClassDB::bind_method(D_METHOD("set_alpha_mode", "mode"),
                       &SpoutOutput::set_alpha_mode);
  ClassDB::bind_method(D_METHOD("get_color_conversion"),
                       &SpoutOutput::get_color_conversion);
  ClassDB::bind_method(D_METHOD("set_color_conversion", "conversion"),
                       &SpoutOutput::set_color_conversion);
  ClassDB::bind_method(D_METHOD("get_max_fps"), &SpoutOutput::get_max_fps);
  ClassDB::bind_method(D_METHOD("set_max_fps", "fps"),
                       &SpoutOutput::set_max_fps);

  ClassDB::bind_method(D_METHOD("is_sending"), &SpoutOutput::is_sending);
  ClassDB::bind_method(D_METHOD("get_active_sender_name"),
                       &SpoutOutput::get_active_sender_name);
  ClassDB::bind_method(D_METHOD("get_sender_size"),
                       &SpoutOutput::get_sender_size);
  ClassDB::bind_method(D_METHOD("get_last_send_cpu_usec"),
                       &SpoutOutput::get_last_send_cpu_usec);
  ClassDB::bind_method(D_METHOD("get_frames_sent"),
                       &SpoutOutput::get_frames_sent);
  ClassDB::bind_method(D_METHOD("get_frames_skipped"),
                       &SpoutOutput::get_frames_skipped);
  ClassDB::bind_method(D_METHOD("get_adapter_name"),
                       &SpoutOutput::get_adapter_name);

  // Add properties
  ClassDB::add_property("SpoutOutput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "SpoutOutput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2D"},
      "set_texture", "get_texture");
  ClassDB::add_property("SpoutOutput", {Variant::BOOL, "enabled"},
                        "set_enabled", "is_enabled");
  ClassDB::add_property("SpoutOutput", {Variant::BOOL, "active_in_editor"},
                        "set_active_in_editor", "is_active_in_editor");
  ClassDB::add_property(
      "SpoutOutput",
      {Variant::INT, "format", PROPERTY_HINT_ENUM, "Auto,RGBA8,RGBA16F"},
      "set_format", "get_format");
  ClassDB::add_property("SpoutOutput", {Variant::BOOL, "flip_y"}, "set_flip_y",
                        "get_flip_y");
  ClassDB::add_property(
      "SpoutOutput",
      {Variant::INT, "alpha_mode", PROPERTY_HINT_ENUM, "Keep,Opaque"},
      "set_alpha_mode", "get_alpha_mode");
  ClassDB::add_property("SpoutOutput",
                        {Variant::INT, "color_conversion", PROPERTY_HINT_ENUM,
                         "None,Linear to sRGB,sRGB to Linear"},
                        "set_color_conversion", "get_color_conversion");
  ClassDB::add_property("SpoutOutput",
                        {Variant::FLOAT, "max_fps", PROPERTY_HINT_RANGE,
                         "0,240,0.1,or_greater,suffix:FPS"},
                        "set_max_fps", "get_max_fps");

  BIND_ENUM_CONSTANT(FORMAT_AUTO);
  BIND_ENUM_CONSTANT(FORMAT_RGBA8);
  BIND_ENUM_CONSTANT(FORMAT_RGBA16F);
  BIND_ENUM_CONSTANT(ALPHA_KEEP);
  BIND_ENUM_CONSTANT(ALPHA_OPAQUE);
  BIND_ENUM_CONSTANT(COLOR_NONE);
  BIND_ENUM_CONSTANT(COLOR_LINEAR_TO_SRGB);
  BIND_ENUM_CONSTANT(COLOR_SRGB_TO_LINEAR);

  ADD_SIGNAL(MethodInfo("sender_started",
                        PropertyInfo(Variant::STRING, "sender_name")));
  ADD_SIGNAL(MethodInfo("sender_stopped",
                        PropertyInfo(Variant::STRING, "sender_name")));
  ADD_SIGNAL(MethodInfo("sender_resized",
                        PropertyInfo(Variant::STRING, "sender_name"),
                        PropertyInfo(Variant::VECTOR2I, "size")));
}

SpoutOutput::SpoutOutput()
    : Node(), _core(std::make_shared<SpoutSenderCore>()) {
  _core->set_name(_channel_name);
  // Events are reported on the render thread; signals are emitted on the
  // main thread through call_deferred.
  _core->set_event_handler([this](SpoutSenderCore::Event p_event,
                                  const String &p_name,
                                  const Vector2i &p_size) {
    switch (p_event) {
    case SpoutSenderCore::EVENT_STARTED:
      call_deferred("emit_signal", "sender_started", p_name);
      break;
    case SpoutSenderCore::EVENT_STOPPED:
      call_deferred("emit_signal", "sender_stopped", p_name);
      break;
    case SpoutSenderCore::EVENT_RESIZED:
      call_deferred("emit_signal", "sender_resized", p_name, p_size);
      break;
    }
  });
}

SpoutOutput::~SpoutOutput() {
  _core->detach();
  _stop();
}

String SpoutOutput::get_channel_name() const { return _channel_name; }

void SpoutOutput::set_channel_name(String p_name) {
  // Takes effect with the next frame; the sender is renamed in place.
  _channel_name = p_name;
  _core->set_name(p_name);
}

Ref<Texture2D> SpoutOutput::get_texture() const { return _texture; }

void SpoutOutput::set_texture(Ref<Texture2D> p_texture) {
  // Switching the source keeps the sender (and its shared texture, if the
  // size and format match), so receivers don't see it disappear.
  _texture = p_texture;
  _gl_viewport = ObjectID();
  _gl_resolve_at_msec = 0;
  _sync_source();
}

void SpoutOutput::set_enabled(bool p_enabled) {
  _enabled = p_enabled;
  // Disabling unregisters the sender at the next frame but keeps the GPU
  // resources, so enabling again is instant.
  _core->set_enabled(p_enabled);
}

void SpoutOutput::set_active_in_editor(bool p_active) {
  _active_in_editor = p_active;
  _update_running();
}

void SpoutOutput::set_format(Format p_format) {
  _format = p_format;
  _core->set_format(static_cast<SpoutSendFormat>(p_format));
}

void SpoutOutput::set_flip_y(bool p_flip) {
  _flip_y = p_flip;
  _apply_options();
}

void SpoutOutput::set_alpha_mode(AlphaMode p_mode) {
  _alpha_mode = p_mode;
  _apply_options();
}

void SpoutOutput::set_color_conversion(ColorConversion p_conversion) {
  _color_conversion = p_conversion;
  _apply_options();
}

void SpoutOutput::set_max_fps(double p_fps) {
  _max_fps = p_fps > 0.0 ? p_fps : 0.0;
  _core->set_max_fps(_max_fps);
}

void SpoutOutput::_apply_options() {
  SpoutBlitOptions options;
  options.flip_y = _flip_y;
  options.force_opaque = _alpha_mode == ALPHA_OPAQUE;
  options.color = static_cast<SpoutColorConversion>(_color_conversion);
  _core->set_blit_options(options);
}

bool SpoutOutput::is_sending() const { return _core->get_status().sending; }

String SpoutOutput::get_active_sender_name() const {
  return _core->get_status().active_name;
}

Vector2i SpoutOutput::get_sender_size() const {
  const SpoutSenderCore::Status status = _core->get_status();
  return status.sending ? status.size : Vector2i();
}

int64_t SpoutOutput::get_last_send_cpu_usec() const {
  return static_cast<int64_t>(_core->get_status().last_cpu_usec);
}

int64_t SpoutOutput::get_frames_sent() const {
  return static_cast<int64_t>(_core->get_status().frames_sent);
}

int64_t SpoutOutput::get_frames_skipped() const {
  return static_cast<int64_t>(_core->get_status().frames_skipped);
}

String SpoutOutput::get_adapter_name() const {
  return _core->get_status().adapter_name;
}

void SpoutOutput::_notification(int p_what) {
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    _update_running();
    break;
  case NOTIFICATION_INTERNAL_PROCESS:
    _sync_source();
    break;
  case NOTIFICATION_PREDELETE:
    _core->detach();
    _stop();
    break;
  case NOTIFICATION_EXIT_TREE:
    _stop();
    break;
  default:
    break;
  }
}

bool SpoutOutput::_should_run() const {
  if (!is_inside_tree()) {
    return false;
  }
  return _active_in_editor || !Engine::get_singleton()->is_editor_hint();
}

void SpoutOutput::_update_running() {
  if (_should_run()) {
    _start();
  } else {
    _stop();
  }
}

void SpoutOutput::_start() {
  if (_running || !spout_rendering_available()) {
    return;
  }
  _sync_source();
  spout_core_start(_core);
  set_process_internal(true); // _sync_source() every frame
  _running = true;
}

void SpoutOutput::_stop() {
  if (!_running) {
    return;
  }
  _running = false;
  if (is_inside_tree()) {
    set_process_internal(false);
  }
  // Unregisters the sender and frees the GPU resources (scene change, node
  // freed, quit).
  spout_core_stop(_core);
}

void SpoutOutput::_sync_source() {
  // Main thread: the core (render thread) only gets RIDs and sizes.
  if (_texture.is_null()) {
    _core->set_texture(RID());
    _core->set_gl_source(RID(), Vector2i());
    return;
  }
  _core->set_texture(_texture->get_rid());
  if (spout_rendering_available() &&
      RenderingServer::get_singleton()->get_current_rendering_driver_name() ==
          "opengl3") {
    _resolve_gl_source();
  }
}

void SpoutOutput::_resolve_gl_source() {
  // In the Compatibility renderer, a ViewportTexture's RID is a proxy that
  // has no GL texture name (texture_get_native_handle returns 0), so the old
  // code never sent viewports on OpenGL. Find the viewport the texture
  // belongs to and send its render target instead. RD drivers don't need
  // this: texture_get_rd_texture resolves proxies.
  RenderingServer *rs = RenderingServer::get_singleton();
  Viewport *viewport =
      Object::cast_to<Viewport>(ObjectDB::get_instance(_gl_viewport));
  Ref<ViewportTexture> vt = _texture;
  if (!viewport && vt.is_valid() && spout_ticks_msec() >= _gl_resolve_at_msec) {
    // Rate-limited: the search walks the scene tree.
    _gl_resolve_at_msec = spout_ticks_msec() + 1000;
    // A ViewportTexture resource in a scene: path relative to its scene.
    Node *scene = vt->get_local_scene();
    if (scene && !vt->get_viewport_path_in_scene().is_empty()) {
      viewport = Object::cast_to<Viewport>(
          scene->get_node_or_null(vt->get_viewport_path_in_scene()));
    }
    // SubViewport.get_texture(): find the viewport that returns it.
    if (!viewport && is_inside_tree()) {
      Window *root = get_tree()->get_root();
      if (root->get_texture() == vt) {
        viewport = root;
      } else {
        TypedArray<Node> found =
            root->find_children("*", "SubViewport", true, false);
        for (int i = 0; i < found.size() && !viewport; i++) {
          Viewport *v = Object::cast_to<Viewport>(found[i]);
          if (v && v->get_texture() == vt) {
            viewport = v;
          }
        }
      }
    }
    _gl_viewport = viewport ? viewport->get_instance_id() : ObjectID();
  }
  _core->set_gl_source(
      viewport ? rs->viewport_get_texture(viewport->get_viewport_rid()) : RID(),
      Vector2i(_texture->get_width(), _texture->get_height()));
}
