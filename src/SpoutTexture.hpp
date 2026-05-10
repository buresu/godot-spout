#pragma once

#include <godot_cpp/classes/texture2drd.hpp>

namespace godot {

class SpoutBackend;

class SpoutTexture : public Texture2DRD {
  GDCLASS(SpoutTexture, Texture2DRD)
public:
  SpoutTexture();
  virtual ~SpoutTexture();

  String get_channel_name() const;
  void set_channel_name(String p_name);

protected:
  static void _bind_methods();
  bool _is_initialized() const;
  bool _create_receiver();
  void _release_receiver();
  bool _create_texture(uint32_t p_width, uint32_t p_height);
  void _release_texture();
  void _receive_texture();

private:
  SpoutBackend *_backend = nullptr;

  // Godot texture
  RID _rd_texture;

  // Properties
  String _channel_name;
};

} // namespace godot
