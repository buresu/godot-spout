#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2drd.hpp>

class spoutDX12;
struct ID3D12Resource;

namespace godot {

class GDSpoutInput : public Node {
  GDCLASS(GDSpoutInput, Node)
public:
  GDSpoutInput();
  virtual ~GDSpoutInput();

  String get_channel_name() const;
  void set_channel_name(String p_name);

  Ref<Texture2DRD> get_texture() const;

protected:
  static void _bind_methods();
  bool _is_initialized() const;
  bool _create_receiver();
  void _release_receiver();
  void _receive_texture();

private:
  // DX12 Spout
  spoutDX12 *_receiver = nullptr;
  ID3D12Resource *_d3d12_texture = nullptr;

  // Godot texture
  Ref<Texture2DRD> _texture;
  RID _rd_texture;
  uint32_t _width = 0;
  uint32_t _height = 0;

  // Properties
  String _channel_name;
};

} // namespace godot
