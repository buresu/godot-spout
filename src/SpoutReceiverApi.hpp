#pragma once

// Shared GDScript API of the two receiver classes (SpoutInput, SpoutTexture).
// Both own a SpoutReceiverCore; these helpers keep their bindings, signals and
// inspector behaviour identical.

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/property_info.hpp>
#include <godot_cpp/variant/packed_string_array.hpp>

#include "SpoutCore.hpp"

namespace godot {

// Inspector suggestions for channel_name: the senders running right now. A
// free-text name (for a sender that isn't running yet) is still allowed.
inline void spout_receiver_validate_channel(PropertyInfo &p_property) {
  if (p_property.name != StringName("channel_name")) {
    return;
  }
  p_property.hint = PROPERTY_HINT_ENUM_SUGGESTION;
  p_property.hint_string = String(",").join(spout_get_sender_list());
}

// Forwards core events to signals, emitted on the main thread.
template <typename T> void spout_receiver_connect_events(T *p_owner) {
  p_owner->core().set_event_handler(
      [p_owner](SpoutReceiverCore::Event p_event, const String &p_name) {
        switch (p_event) {
        case SpoutReceiverCore::EVENT_CONNECTED:
          p_owner->call_deferred("emit_signal", "sender_connected", p_name);
          break;
        case SpoutReceiverCore::EVENT_DISCONNECTED:
          p_owner->call_deferred("emit_signal", "sender_disconnected", p_name);
          break;
        case SpoutReceiverCore::EVENT_FRAME:
          p_owner->call_deferred("emit_signal", "frame_received");
          break;
        case SpoutReceiverCore::EVENT_ERROR:
          p_owner->call_deferred("emit_signal", "receive_error",
                                 p_owner->core().get_last_error());
          break;
        }
      });
}

// Methods, properties, constants and signals both classes share. T must
// provide the listed getters/setters.
template <typename T> void spout_receiver_bind(const char *p_class) {
  ClassDB::bind_static_method(p_class, D_METHOD("get_sender_list"),
                              &spout_get_sender_list);

  ClassDB::bind_method(D_METHOD("get_channel_name"), &T::get_channel_name);
  ClassDB::bind_method(D_METHOD("set_channel_name", "name"),
                       &T::set_channel_name);
  ClassDB::bind_method(D_METHOD("is_enabled"), &T::is_enabled);
  ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &T::set_enabled);
  ClassDB::bind_method(D_METHOD("is_active_in_editor"),
                       &T::is_active_in_editor);
  ClassDB::bind_method(D_METHOD("set_active_in_editor", "active"),
                       &T::set_active_in_editor);
  ClassDB::bind_method(D_METHOD("get_flip_y"), &T::get_flip_y);
  ClassDB::bind_method(D_METHOD("set_flip_y", "flip"), &T::set_flip_y);
  ClassDB::bind_method(D_METHOD("get_clear_on_disconnect"),
                       &T::get_clear_on_disconnect);
  ClassDB::bind_method(D_METHOD("set_clear_on_disconnect", "clear"),
                       &T::set_clear_on_disconnect);

  ClassDB::bind_method(D_METHOD("is_sender_connected"),
                       &T::is_sender_connected);
  ClassDB::bind_method(D_METHOD("get_receiver_state"), &T::get_receiver_state);
  ClassDB::bind_method(D_METHOD("get_sender_name"), &T::get_sender_name);
  ClassDB::bind_method(D_METHOD("get_sender_size"), &T::get_sender_size);
  ClassDB::bind_method(D_METHOD("get_sender_format"), &T::get_sender_format);
  ClassDB::bind_method(D_METHOD("get_sender_fps"), &T::get_sender_fps);
  ClassDB::bind_method(D_METHOD("get_sender_frame"), &T::get_sender_frame);
  ClassDB::bind_method(D_METHOD("get_frames_received"),
                       &T::get_frames_received);
  ClassDB::bind_method(D_METHOD("get_adapter_name"), &T::get_adapter_name);
  ClassDB::bind_method(D_METHOD("get_last_error"), &T::get_last_error);

  ClassDB::add_property(p_class, {Variant::STRING, "channel_name"},
                        "set_channel_name", "get_channel_name");
  ClassDB::add_property(p_class, {Variant::BOOL, "enabled"}, "set_enabled",
                        "is_enabled");
  ClassDB::add_property(p_class, {Variant::BOOL, "active_in_editor"},
                        "set_active_in_editor", "is_active_in_editor");
  ClassDB::add_property(p_class, {Variant::BOOL, "flip_y"}, "set_flip_y",
                        "get_flip_y");
  ClassDB::add_property(p_class, {Variant::BOOL, "clear_on_disconnect"},
                        "set_clear_on_disconnect", "get_clear_on_disconnect");

  ClassDB::bind_integer_constant(p_class, "ReceiverState", "STATE_IDLE",
                                 SpoutReceiverCore::STATE_IDLE);
  ClassDB::bind_integer_constant(p_class, "ReceiverState", "STATE_WAITING",
                                 SpoutReceiverCore::STATE_WAITING);
  ClassDB::bind_integer_constant(p_class, "ReceiverState", "STATE_CONNECTED",
                                 SpoutReceiverCore::STATE_CONNECTED);
  ClassDB::bind_integer_constant(p_class, "ReceiverState", "STATE_ERROR",
                                 SpoutReceiverCore::STATE_ERROR);

  ClassDB::add_signal(p_class,
                      MethodInfo("sender_connected",
                                 PropertyInfo(Variant::STRING, "sender_name")));
  ClassDB::add_signal(p_class,
                      MethodInfo("sender_disconnected",
                                 PropertyInfo(Variant::STRING, "sender_name")));
  ClassDB::add_signal(p_class, MethodInfo("frame_received"));
  ClassDB::add_signal(
      p_class,
      MethodInfo("receive_error", PropertyInfo(Variant::STRING, "message")));
}

} // namespace godot

// Status getters both receiver classes implement by forwarding to the core.
#define SPOUT_RECEIVER_STATUS_METHODS                                          \
  bool is_sender_connected() const { return _core.is_connected(); }            \
  int get_receiver_state() const { return _core.get_state(); }                 \
  String get_sender_name() const { return _core.get_sender_name(); }           \
  Vector2i get_sender_size() const { return _core.get_sender_size(); }         \
  String get_sender_format() const {                                           \
    return _core.is_connected() ? spout_dxgi_format_name(spout_dxgi_normalize( \
                                      _core.get_sender_dxgi_format()))         \
                                : String();                                    \
  }                                                                            \
  double get_sender_fps() const { return _core.get_sender_fps(); }             \
  int64_t get_sender_frame() const { return _core.get_sender_frame(); }        \
  int64_t get_frames_received() const {                                        \
    return static_cast<int64_t>(_core.get_frames_received());                  \
  }                                                                            \
  String get_adapter_name() const { return _core.get_adapter_name(); }         \
  String get_last_error() const { return _core.get_last_error(); }             \
  SpoutReceiverCore &core() { return _core; }
