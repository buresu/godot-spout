#include "SpoutTexture.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutTexture::_bind_methods() {
  spout_receiver_bind<SpoutTexture>("SpoutTexture");
}

SpoutTexture::SpoutTexture() : Texture2DRD() {
  _core.set_texture_handler([this](const RID &p_rid) { _show(p_rid); });
  spout_receiver_connect_events(this);
  if (spout_rendering_available()) {
    RenderingServer::get_singleton()->connect(
        "frame_pre_draw", callable_mp(this, &SpoutTexture::_on_frame_pre_draw));
    _connected = true;
  }
}

SpoutTexture::~SpoutTexture() {
  // A resource can outlive the RenderingServer (e.g. held by a script
  // constant until the very end); then there is nothing left to release.
  if (!spout_rendering_available()) {
    return;
  }
  if (_connected) {
    RenderingServer::get_singleton()->disconnect(
        "frame_pre_draw", callable_mp(this, &SpoutTexture::_on_frame_pre_draw));
  }
  _core.release();
}

void SpoutTexture::_validate_property(PropertyInfo &p_property) const {
  spout_receiver_validate_channel(p_property);
}

String SpoutTexture::get_channel_name() const { return _channel_name; }

void SpoutTexture::set_channel_name(String p_name) {
  _channel_name = p_name;
  _core.set_name(p_name);
}

void SpoutTexture::set_enabled(bool p_enabled) {
  _enabled = p_enabled;
  _core.set_enabled(p_enabled);
}

void SpoutTexture::set_active_in_editor(bool p_active) {
  _active_in_editor = p_active;
}

void SpoutTexture::set_flip_y(bool p_flip) {
  _flip_y = p_flip;
  _core.set_flip_y(p_flip);
}

void SpoutTexture::set_clear_on_disconnect(bool p_clear) {
  _clear_on_disconnect = p_clear;
  _core.set_clear_on_disconnect(p_clear);
}

void SpoutTexture::_show(const RID &p_rid) {
  if (spout_rendering_available()) {
    set_texture_rd_rid(p_rid);
  }
}

void SpoutTexture::_on_frame_pre_draw() {
  // RenderingDevice must only be used on the render thread. frame_pre_draw is
  // normally emitted there; hop over if it ever isn't.
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_on_render_thread()) {
    rs->call_on_render_thread(callable_mp(this, &SpoutTexture::_process_frame));
    return;
  }
  _process_frame();
}

void SpoutTexture::_process_frame() {
  if (!_connected) {
    return;
  }
  if (!_active_in_editor && Engine::get_singleton()->is_editor_hint()) {
    if (_core.get_state() != SpoutReceiverCore::STATE_IDLE) {
      _core.release();
    }
    return;
  }
  _core.on_frame_pre_draw();
}
