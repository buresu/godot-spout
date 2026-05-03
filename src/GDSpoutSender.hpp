#pragma once

#include <godot_cpp/classes/node.hpp>
#include <godot_cpp/classes/texture2d.hpp>

class spoutDX12;
struct ID3D11Resource;
struct ID3D12Resource;

namespace godot {

class RenderingDevice;

class GDSpoutSender : public Node {
  GDCLASS(GDSpoutSender, Node)
public:
  GDSpoutSender();
  virtual ~GDSpoutSender();

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
  // DX12 Spout
  spoutDX12 *_sender = nullptr;
  ID3D11Resource *_wrapped_resource = nullptr;
  ID3D12Resource *_d3d12_texture = nullptr;

  // Rendering device (borrowed from Godot, do not delete)
  RenderingDevice *_rd = nullptr;

  // Properties
  String _channel_name = "godot-spout";
  Ref<Texture2D> _texture;
};

} // namespace godot
