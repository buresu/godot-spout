#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>

class SpoutSender;

namespace godot {

class GDSpoutSender : public Node {
  GDCLASS(GDSpoutSender, Node)
public:
  GDSpoutSender();
  virtual ~GDSpoutSender();

  String get_channel_name() const;
  void set_channel_name(String name);

  Ref<Texture> get_texture() const;
  void set_texture(Ref<Texture> p_texture);

  void _ready() override;

protected:
  static void _bind_methods();
  bool _make_current();
  bool _is_initialized() const;
  bool _create_sender();
  void _release_sender();
  void _update_sender();
  void _send_texture();

private:
  // Spout
  SpoutSender *_sender = nullptr;
  String _channel_name = "godot-spout";
  Ref<Texture2D> _texture;
};

} // namespace godot
