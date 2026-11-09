#pragma once

// Shared D3D11 devices used to create and copy Spout's shared textures.
//
// Spout shares textures through legacy (KMT) DXGI shared handles created by
// D3D11. Such a handle only opens on the GPU that created it, so every D3D11
// device here is created on the same adapter as Godot's rendering device:
//
//   * D3D12 driver: a D3D11On12 device layered on Godot's own ID3D12Device and
//     command queue. Its work is submitted to Godot's queue, so GPU ordering
//     with Godot's frames is the queue's submission order.
//   * Vulkan driver: a plain D3D11 device on the DXGI adapter whose LUID
//     matches Godot's VkPhysicalDevice (SpoutVK always used
//     the system default adapter).
//
// One device is shared by all senders and receivers (one D3D11On12 device per
// node was heavyweight and was re-created every frame while a receiver waited).
// Devices are reference counted with shared_ptr and destroyed
// when the last user releases them.

#include <cstdint>
#include <memory>

#include <godot_cpp/variant/string.hpp>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11On12Device;
struct ID3D11Resource;
struct ID3D11Texture2D;
struct ID3D12Device;
struct IUnknown;

namespace godot {

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

  // Creates a texture other processes can open through its legacy shared
  // handle (D3D11_RESOURCE_MISC_SHARED, like spoutDirectX does). Bind flags
  // render target + shader resource are what DX9/DX11 receivers expect.
  bool create_shared_texture(uint32_t p_width, uint32_t p_height,
                             uint32_t p_dxgi_format,
                             ID3D11Texture2D **r_texture,
                             uint64_t *r_handle) const;

  // Opens a sender's shared texture. Fails if it lives on another adapter.
  bool open_shared_texture(uint64_t p_handle, ID3D11Texture2D **r_texture,
                           String &r_error) const;

  // Prints and clears D3D11 / D3D12 debug-layer messages if
  // GODOT_SPOUT_D3D12_DEBUG is set. p_where names the call site.
  void drain_debug_messages(const char *p_where) const;

private:
  SpoutD3D11Device() = default;
  void init_debug(ID3D12Device *p_device12);

  ID3D11Device *_device = nullptr;
  ID3D11DeviceContext *_context = nullptr;
  ID3D11On12Device *_on12 = nullptr;
  ID3D12Device *_device12 = nullptr; // not owned beyond D3D11On12's own ref
  IUnknown *_info_queue12 = nullptr;
  IUnknown *_info_queue11 = nullptr;
  String _adapter_name;
};

} // namespace godot
