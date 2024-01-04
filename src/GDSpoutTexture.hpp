#pragma once

#include <godot_cpp/classes/texture2drd.hpp>

class SpoutReceiver;

namespace godot {

class RenderingDevice;

class GDSpoutTexture : public Texture2DRD {
  GDCLASS(GDSpoutTexture, Texture2DRD)
public:
  GDSpoutTexture();
  virtual ~GDSpoutTexture();

  String get_channel_name() const;
  void set_channel_name(String p_name);

protected:
  static void _bind_methods();
  bool _make_current();
  bool _is_initialized() const;
  bool _create_receiver();
  void _release_receiver();
  void _receive_texture();

private:
  // Rendering device
  RenderingDevice *_device = nullptr;

  // Texture
  mutable RID _texture;
  int32_t _width = 0;
  int32_t _height = 0;

  // Spout
  SpoutReceiver *_receiver = nullptr;
  String _channel_name;
};

} // namespace godot
