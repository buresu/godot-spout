#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>

#include "SpoutCore.hpp"

namespace godot {

// Sends a texture (typically a SubViewport's) to Spout receivers such as
// Resolume Arena. Active while the node is inside the scene tree.
class SpoutOutput : public Node {
  GDCLASS(SpoutOutput, Node)
public:
  SpoutOutput();
  ~SpoutOutput() override;

  String get_channel_name() const;
  void set_channel_name(String p_name);

  Ref<Texture2D> get_texture() const;
  void set_texture(Ref<Texture2D> p_texture);

protected:
  static void _bind_methods();
  void _notification(int p_what);

private:
  void _start();
  void _stop();
  void _on_frame_post_draw();
  void _process_frame();

  SpoutSenderCore _core;
  bool _connected = false;

  // Properties
  String _channel_name = "godot-spout";
  Ref<Texture2D> _texture;
};

} // namespace godot
