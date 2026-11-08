#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2drd.hpp>

#include "SpoutReceiverApi.hpp"

namespace godot {

// Receives a Spout sender into a Texture2DRD. Active while the node is inside
// the scene tree.
class SpoutInput : public Node {
  GDCLASS(SpoutInput, Node)
public:
  SpoutInput();
  ~SpoutInput() override;

  String get_channel_name() const;
  void set_channel_name(String p_name);

  Ref<Texture2DRD> get_texture() const;
  void set_texture(Ref<Texture2DRD> p_texture);

  bool is_enabled() const { return _enabled; }
  void set_enabled(bool p_enabled);
  bool is_active_in_editor() const { return _active_in_editor; }
  void set_active_in_editor(bool p_active);
  bool get_flip_y() const { return _flip_y; }
  void set_flip_y(bool p_flip);
  bool get_clear_on_disconnect() const { return _clear_on_disconnect; }
  void set_clear_on_disconnect(bool p_clear);

  SPOUT_RECEIVER_STATUS_METHODS

protected:
  static void _bind_methods();
  void _notification(int p_what);
  void _validate_property(PropertyInfo &p_property) const;

private:
  void _update_running();
  void _start();
  void _stop();
  void _on_frame_pre_draw();
  void _process_frame();
  void _show(const RID &p_rid);

  SpoutReceiverCore _core;
  bool _connected = false;

  // Properties
  String _channel_name;
  Ref<Texture2DRD> _texture;
  bool _enabled = true;
  bool _active_in_editor = true;
  bool _flip_y = false;
  bool _clear_on_disconnect = false;
};

} // namespace godot
