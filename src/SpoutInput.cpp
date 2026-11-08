#include "SpoutInput.hpp"

#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutInput::_bind_methods() {

  // Bind methods
  ClassDB::bind_method(D_METHOD("get_channel_name"),
                       &SpoutInput::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &SpoutInput::set_channel_name);
  ClassDB::bind_method(D_METHOD("get_texture"), &SpoutInput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &SpoutInput::set_texture);

  // Add properties
  ClassDB::add_property("SpoutInput", {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(
      "SpoutInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2DRD"},
      "set_texture", "get_texture");
}

SpoutInput::SpoutInput() : Node() {
  _core.set_texture_handler([this](const RID &p_rid) { _show(p_rid); });
}

SpoutInput::~SpoutInput() { _stop(); }

String SpoutInput::get_channel_name() const { return _channel_name; }

void SpoutInput::set_channel_name(String p_name) {
  _channel_name = p_name;
  _core.set_name(p_name);
}

Ref<Texture2DRD> SpoutInput::get_texture() const { return _texture; }

void SpoutInput::set_texture(Ref<Texture2DRD> p_texture) {
  if (_texture == p_texture) {
    return;
  }
  if (_texture.is_valid() && spout_rendering_available()) {
    _texture->set_texture_rd_rid(RID());
  }
  _texture = p_texture;
  // Show the current frame right away if we're already receiving.
  _show(_core.get_visible_texture());
}

void SpoutInput::_show(const RID &p_rid) {
  if (_texture.is_valid() && spout_rendering_available()) {
    _texture->set_texture_rd_rid(p_rid);
  }
}

void SpoutInput::_notification(int p_what) {
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    _start();
    break;
  case NOTIFICATION_EXIT_TREE:
  case NOTIFICATION_PREDELETE:
    _stop();
    break;
  default:
    break;
  }
}

void SpoutInput::_start() {
  if (_connected || !spout_rendering_available()) {
    return;
  }
  RenderingServer::get_singleton()->connect(
      "frame_pre_draw", callable_mp(this, &SpoutInput::_on_frame_pre_draw));
  _connected = true;
}

void SpoutInput::_stop() {
  if (_connected && spout_rendering_available()) {
    RenderingServer::get_singleton()->disconnect(
        "frame_pre_draw", callable_mp(this, &SpoutInput::_on_frame_pre_draw));
  }
  _connected = false;
  if (spout_rendering_available()) {
    _core.release();
  }
}

void SpoutInput::_on_frame_pre_draw() {
  // RenderingDevice must only be used on the render thread. frame_pre_draw is
  // normally emitted there; hop over if it ever isn't.
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_on_render_thread()) {
    rs->call_on_render_thread(callable_mp(this, &SpoutInput::_process_frame));
    return;
  }
  _process_frame();
}

void SpoutInput::_process_frame() {
  if (_connected) {
    _core.on_frame_pre_draw();
  }
}
