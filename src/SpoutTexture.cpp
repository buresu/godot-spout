#include "SpoutTexture.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutTexture::_bind_methods() {
  spout_receiver_bind<SpoutTexture>("SpoutTexture");
}

SpoutTexture::SpoutTexture()
    : Texture2DRD(), _core(std::make_shared<SpoutReceiverCore>()) {
  _core->set_texture_handler(
      [this](const RID &p_rid) { spout_set_texture_rd(this, p_rid); });
  spout_receiver_connect_events(this);
  _apply_enabled();
  if (spout_rendering_available()) {
    spout_core_start(_core);
    _running = true;
  }
}

SpoutTexture::~SpoutTexture() { _shutdown(); }

void SpoutTexture::_notification(int p_what) {
  if (p_what == NOTIFICATION_PREDELETE) {
    _shutdown();
  }
}

void SpoutTexture::_shutdown() {
  if (!_running) {
    _core->detach();
    return;
  }
  _running = false;
  if (!spout_rendering_available()) {
    _core->detach();
    return;
  }
  // The release clears this texture: wait while it's intact (main thread only).
  spout_core_stop(_core);
  RenderingServer *rs = RenderingServer::get_singleton();
  if (!rs->is_on_render_thread() && spout_is_main_thread()) {
    rs->force_sync();
  }
  _core->detach();
}

void SpoutTexture::_validate_property(PropertyInfo &p_property) const {
  spout_receiver_validate_channel(p_property);
}

String SpoutTexture::get_channel_name() const { return _channel_name; }

void SpoutTexture::set_channel_name(String p_name) {
  _channel_name = p_name;
  _core->set_name(p_name);
}

void SpoutTexture::set_enabled(bool p_enabled) {
  _enabled = p_enabled;
  _apply_enabled();
}

void SpoutTexture::set_active_in_editor(bool p_active) {
  _active_in_editor = p_active;
  _apply_enabled();
}

void SpoutTexture::_apply_enabled() {
  Engine *engine = Engine::get_singleton();
  const bool in_editor = engine && engine->is_editor_hint();
  _core->set_enabled(_enabled && (_active_in_editor || !in_editor));
}

void SpoutTexture::set_flip_y(bool p_flip) {
  _flip_y = p_flip;
  _core->set_flip_y(p_flip);
}

void SpoutTexture::set_clear_on_disconnect(bool p_clear) {
  _clear_on_disconnect = p_clear;
  _core->set_clear_on_disconnect(p_clear);
}
