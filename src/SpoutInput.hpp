#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2drd.hpp>

class spoutDX12;

namespace godot {

class SpoutInput : public Node {
  GDCLASS(SpoutInput, Node)
public:
  SpoutInput();
  virtual ~SpoutInput();

  String get_channel_name() const;
  void set_channel_name(String p_name);

  Ref<Texture2DRD> get_texture() const;
  void set_texture(Ref<Texture2DRD> p_texture);

protected:
  static void _bind_methods();
  bool _is_initialized() const;
  bool _create_receiver();
  void _release_receiver();
  bool _create_texture(uint32_t p_width, uint32_t p_height);
  void _release_texture();
  void _receive_texture();

private:
  // DX12 Spout
  spoutDX12 *_receiver = nullptr;

  // Godot texture
  RID _rd_texture;

  // Properties
  String _channel_name;
  Ref<Texture2DRD> _texture;
};

} // namespace godot
