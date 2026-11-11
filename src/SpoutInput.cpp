#include "SpoutInput.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/class_db.hpp>

using namespace godot;

void SpoutInput::_bind_methods() {
  spout_receiver_bind<SpoutInput>("SpoutInput");

  ClassDB::bind_method(D_METHOD("get_texture"), &SpoutInput::get_texture);
  ClassDB::bind_method(D_METHOD("set_texture", "p_texture"),
                       &SpoutInput::set_texture);
  ClassDB::add_property(
      "SpoutInput",
      {Variant::OBJECT, "texture", PROPERTY_HINT_RESOURCE_TYPE, "Texture2DRD"},
      "set_texture", "get_texture");
}

SpoutInput::SpoutInput()
    : Node(), _core(std::make_shared<SpoutReceiverCore>()) {
  spout_receiver_connect_events(this);
}

SpoutInput::~SpoutInput() {
  _core->detach();
  _stop();
}

void SpoutInput::_validate_property(PropertyInfo &p_property) const {
  spout_receiver_validate_channel(p_property);
}

String SpoutInput::get_channel_name() const { return _channel_name; }

void SpoutInput::set_channel_name(String p_name) {
  _channel_name = p_name;
  _core->set_name(p_name);
}

Ref<Texture2DRD> SpoutInput::get_texture() const { return _texture; }

void SpoutInput::set_texture(Ref<Texture2DRD> p_texture) {
  _texture = p_texture;
  _core->set_target(p_texture);
}

void SpoutInput::set_enabled(bool p_enabled) {
  _enabled = p_enabled;
  _core->set_enabled(p_enabled);
}

void SpoutInput::set_active_in_editor(bool p_active) {
  _active_in_editor = p_active;
  _update_running();
}

void SpoutInput::set_flip_y(bool p_flip) {
  _flip_y = p_flip;
  _core->set_flip_y(p_flip);
}

void SpoutInput::set_clear_on_disconnect(bool p_clear) {
  _clear_on_disconnect = p_clear;
  _core->set_clear_on_disconnect(p_clear);
}

void SpoutInput::_notification(int p_what) {
  switch (p_what) {
  case NOTIFICATION_ENTER_TREE:
    _update_running();
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

void SpoutInput::_update_running() {
  const bool run =
      is_inside_tree() &&
      (_active_in_editor || !Engine::get_singleton()->is_editor_hint());
  if (run) {
    _start();
  } else {
    _stop();
  }
}

void SpoutInput::_start() {
  if (_running || !spout_rendering_available()) {
    return;
  }
  spout_core_start(_core);
  _running = true;
}

void SpoutInput::_stop() {
  if (!_running) {
    return;
  }
  _running = false;
  spout_core_stop(_core);
}
