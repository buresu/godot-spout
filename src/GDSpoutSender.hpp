#pragma once

#include <Godot.hpp>
#include <Node.hpp>
#include <Texture.hpp>

class SpoutSender;

namespace godot {

class GDSpoutSender : public Node {
  GODOT_CLASS(GDSpoutSender, Node)
public:
  void _init();
  static void _register_methods();

  GDSpoutSender();
  virtual ~GDSpoutSender();

  String get_channel_name() const;
  void set_channel_name(String name);

  Ref<Texture> get_texture() const;
  void set_texture(Ref<Texture> texture);

protected:
  bool _make_current();
  bool _is_initialized() const;
  bool _create_sender(const String &name);
  void _release_sender();
  void _update_sender();
  void _send_texture();

private:
  // Spout
  SpoutSender *_sender = nullptr;
  String _channel_name;
  Ref<Texture> _texture;
};

} // namespace godot
