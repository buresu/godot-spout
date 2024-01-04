#pragma once

#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/viewport_texture.hpp>

class SpoutReceiver;

namespace godot {

class ViewportTexture;

class GDSpoutTexture : public Texture2D {
  GDCLASS(GDSpoutTexture, Texture2D)
public:
  GDSpoutTexture();
  virtual ~GDSpoutTexture();

  // _get_rid() is not supported yet
  virtual RID _get_rid() const;
  virtual int32_t _get_width() const override;
  virtual int32_t _get_height() const override;
  virtual bool _has_alpha() const override;

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
  // Texture
  mutable RID _texture;
  int32_t _width = 0;
  int32_t _height = 0;

  // Spout
  SpoutReceiver *_receiver = nullptr;
  String _channel_name;
};

} // namespace godot
