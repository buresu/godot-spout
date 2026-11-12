#pragma once

// D3D11 devices for Spout's KMT shared textures, on Godot's adapter (the
// handles only open on the GPU that created them): D3D11On12 on Godot's queue
// for D3D12, a plain device matching the VkPhysicalDevice LUID for Vulkan.
// One refcounted device is shared by all nodes.

#include <cstdint>
#include <memory>

#include <godot_cpp/variant/string.hpp>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11InfoQueue;
struct ID3D11On12Device;
struct ID3D11Texture2D;
struct ID3D12Device;
struct ID3D12InfoQueue;
struct IUnknown;

namespace godot {

template <typename T> void spout_release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
}

class SpoutD3D11Device {
public:
  ~SpoutD3D11Device();

  // D3D11On12 device on Godot's D3D12 device and queue.
  static std::shared_ptr<SpoutD3D11Device> get_on12(ID3D12Device *p_device,
                                                    IUnknown *p_queue);
  // Plain D3D11 device on the adapter with this LUID.
  static std::shared_ptr<SpoutD3D11Device> get_for_luid(uint64_t p_luid);

  ID3D11Device *device() const { return _device; }
  ID3D11DeviceContext *context() const { return _context; }
  // Only set for D3D11On12 devices.
  ID3D11On12Device *on12() const { return _on12; }
  const String &adapter_name() const { return _adapter_name; }

  // Legacy shared handle, as spoutDirectX creates it.
  bool create_shared_texture(uint32_t p_width, uint32_t p_height,
                             uint32_t p_dxgi_format,
                             ID3D11Texture2D **r_texture,
                             uint64_t *r_handle) const;

  // Fails if the texture is on another adapter or its format is unsupported.
  // r_dxgi_format is the texture's own format (advertised ones are sometimes
  // wrong), normalised.
  bool open_shared_texture(uint64_t p_handle, ID3D11Texture2D **r_texture,
                           uint32_t &r_width, uint32_t &r_height,
                           uint32_t &r_dxgi_format, String &r_error) const;

  // Only with GODOT_SPOUT_D3D12_DEBUG.
  void drain_debug_messages(const char *p_where) const;

private:
  SpoutD3D11Device() = default;
  void init_debug(ID3D12Device *p_device12);

  ID3D11Device *_device = nullptr;
  ID3D11DeviceContext *_context = nullptr;
  ID3D11On12Device *_on12 = nullptr;
  ID3D11InfoQueue *_info_queue11 = nullptr;
  ID3D12InfoQueue *_info_queue12 = nullptr;
  String _adapter_name;
};

} // namespace godot
