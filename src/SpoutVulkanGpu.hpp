#pragma once

// Vulkan backend (GODOT_SPOUT_ENABLE_VULKAN). See SpoutVulkanGpu.cpp.

#ifdef GODOT_SPOUT_ENABLE_VULKAN

#include "SpoutGpu.hpp"

#include <memory>

struct ID3D11Texture2D;

namespace godot {

class SpoutD3D11Device;
struct SpoutVulkanImport;

class SpoutVulkanSender : public SpoutGpuSender {
public:
  explicit SpoutVulkanSender(RenderingDevice *p_rd);
  ~SpoutVulkanSender() override;

  bool ensure(uint32_t p_width, uint32_t p_height,
              SpoutSendFormat p_format) override;
  uint64_t get_generation() const override { return _generation; }
  RID get_target() const override;
  bool target_supports_storage() const override;
  uint64_t get_share_handle() const override { return _share_handle; }
  uint32_t get_dxgi_format() const override { return _dxgi_format; }
  uint32_t get_width() const override { return _width; }
  uint32_t get_height() const override { return _height; }
  bool publish(SpoutGodotAccess p_access, SpoutShareSender &p_share) override;
  void release() override;
  String get_adapter_name() const override;

private:
  RenderingDevice *_rd = nullptr;
  std::shared_ptr<SpoutD3D11Device> _d3d11;
  std::unique_ptr<SpoutVulkanImport> _import;
  ID3D11Texture2D *_shared = nullptr;
  uint64_t _share_handle = 0;
  uint32_t _dxgi_format = 0;
  uint32_t _width = 0;
  uint32_t _height = 0;
  SpoutSendFormat _format = SPOUT_SEND_FORMAT_RGBA8;
  uint64_t _generation = 0;
  bool _usable = false;
};

class SpoutVulkanReceiver : public SpoutGpuReceiver {
public:
  explicit SpoutVulkanReceiver(RenderingDevice *p_rd);
  ~SpoutVulkanReceiver() override;

  bool open(const SpoutSenderInfo &p_info, String &r_error) override;
  void close() override;
  bool is_open() const override;
  RID get_source() const override;
  RenderingDevice::DataFormat get_source_format() const override;
  bool source_supports_copy() const override;
  bool acquire(SpoutGodotAccess p_last_access,
               SpoutShareReceiver &p_share) override;
  String get_adapter_name() const override;

private:
  RenderingDevice *_rd = nullptr;
  std::unique_ptr<SpoutVulkanImport> _import;
  String _adapter_name;
};

} // namespace godot

#endif // GODOT_SPOUT_ENABLE_VULKAN
