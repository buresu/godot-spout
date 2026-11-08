#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2drd.hpp>

#include "SpoutCore.hpp"

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

protected:
  static void _bind_methods();
  void _notification(int p_what);

private:
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
};

} // namespace godot
