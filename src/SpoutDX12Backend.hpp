#pragma once

#include "SpoutBackend.hpp"

class spoutDX12;
struct ID3D11Resource;
struct ID3D12Device;
struct ID3D12Resource;
struct IUnknown;

namespace godot {

class SpoutDX12Backend : public SpoutBackend {
public:
  SpoutDX12Backend() = default;
  ~SpoutDX12Backend() override;

  bool initialize(RenderingDevice *p_rd) override;
  void release() override;
  bool is_initialized() const override;

  bool send(const RID &p_texture, uint32_t p_width, uint32_t p_height,
            const char *p_channel_name) override;
  bool update_receiver(const char *p_channel_name) override;
  bool receive(const RID &p_texture, uint32_t p_width, uint32_t p_height,
               const char *p_channel_name) override;

  uint32_t get_sender_width() const override;
  uint32_t get_sender_height() const override;

private:
  bool open_sender(const char *p_channel_name);
  bool open_receiver(const char *p_channel_name);
  bool wrap_send_texture(const RID &p_texture);
  void release_wrapped_texture();

  RenderingDevice *_rd = nullptr;
  spoutDX12 *_spout = nullptr;
  ID3D12Device *_device = nullptr;
  IUnknown *_queue = nullptr;
  ID3D11Resource *_wrapped_resource = nullptr;
  ID3D12Resource *_send_texture = nullptr;
  bool _sender_open = false;
  bool _receiver_open = false;
};

} // namespace godot
