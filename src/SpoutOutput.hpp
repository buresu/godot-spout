#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>

namespace godot {

class SpoutBackend;

class SpoutOutput : public Node {
  GDCLASS(SpoutOutput, Node)
public:
  SpoutOutput();
  virtual ~SpoutOutput();

  String get_channel_name() const;
  void set_channel_name(String p_name);

  Ref<Texture> get_texture() const;
  void set_texture(Ref<Texture> p_texture);

protected:
  static void _bind_methods();
  bool _is_initialized() const;
  bool _create_sender();
  void _release_sender();
  void _send_texture();

private:
  SpoutBackend *_backend = nullptr;
  RID _backend_texture;

  // Properties
  String _channel_name = "godot-spout";
  Ref<Texture2D> _texture;
};

} // namespace godot
