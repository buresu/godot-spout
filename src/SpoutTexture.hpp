#pragma once

#include <godot_cpp/classes/texture2drd.hpp>

#include "SpoutCore.hpp"

namespace godot {

// A Texture2D that shows a Spout sender. Can be assigned anywhere a texture
// is accepted (materials, TextureRect, ...). Receives while it exists.
class SpoutTexture : public Texture2DRD {
  GDCLASS(SpoutTexture, Texture2DRD)
public:
  SpoutTexture();
  ~SpoutTexture() override;

  String get_channel_name() const;
  void set_channel_name(String p_name);

protected:
  static void _bind_methods();

private:
  void _on_frame_pre_draw();
  void _process_frame();
  void _show(const RID &p_rid);

  SpoutReceiverCore _core;
  bool _connected = false;

  // Properties
  String _channel_name;
};

} // namespace godot
