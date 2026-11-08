#pragma once

// D3D12 backend (always built). See SpoutD3D12Gpu.cpp for the sync model.

#include "SpoutGpu.hpp"

#include <memory>

struct ID3D11Resource;
struct ID3D11Texture2D;
struct ID3D12Resource;

namespace godot {

class SpoutD3D11Device;

class SpoutD3D12Sender : public SpoutGpuSender {
public:
  explicit SpoutD3D12Sender(RenderingDevice *p_rd);
  ~SpoutD3D12Sender() override;

  bool ensure(uint32_t p_width, uint32_t p_height,
              SpoutSendFormat p_format) override;
  uint64_t get_generation() const override { return _generation; }
  RID get_target() const override { return _target; }
  bool target_supports_storage() const override { return true; }
  uint64_t get_share_handle() const override { return _share_handle; }
  uint32_t get_dxgi_format() const override { return _dxgi_format; }
  uint32_t get_width() const override { return _width; }
  uint32_t get_height() const override { return _height; }
  bool publish(SpoutGodotAccess p_access, SpoutShareSender &p_share) override;
  void release() override;
  String get_adapter_name() const override;

private:
  ID3D11Resource *wrapped_for(SpoutGodotAccess p_access);

  RenderingDevice *_rd = nullptr;
  std::shared_ptr<SpoutD3D11Device> _d3d11;
  RID _target;
  // Set when we created the resource ourselves (simultaneous access, see
  // SpoutD3D12Gpu.cpp); then we own it and Godot only has a view of it.
  ID3D12Resource *_owned = nullptr;
  bool _simultaneous = false;
  ID3D11Resource *_wrapped_copy = nullptr;
  ID3D11Resource *_wrapped_shader = nullptr;
  ID3D11Texture2D *_shared = nullptr;
  uint64_t _share_handle = 0;
  uint32_t _dxgi_format = 0;
  uint32_t _width = 0;
  uint32_t _height = 0;
  SpoutSendFormat _format = SPOUT_SEND_FORMAT_RGBA8;
  uint64_t _generation = 0;
};

class SpoutD3D12Receiver : public SpoutGpuReceiver {
public:
  explicit SpoutD3D12Receiver(RenderingDevice *p_rd);
  ~SpoutD3D12Receiver() override;

  bool open(const SpoutSenderInfo &p_info, String &r_error) override;
  void close() override;
  bool is_open() const override { return _source.is_valid(); }
  RID get_source() const override { return _source; }
  RenderingDevice::DataFormat get_source_format() const override {
    return _source_format;
  }
  bool source_supports_copy() const override { return true; }
  bool acquire(SpoutGodotAccess p_last_access,
               SpoutShareReceiver &p_share) override;
  String get_adapter_name() const override;

private:
  ID3D11Resource *wrapped_for(SpoutGodotAccess p_access);

  RenderingDevice *_rd = nullptr;
  std::shared_ptr<SpoutD3D11Device> _d3d11;
  ID3D11Texture2D *_shared = nullptr;
  RID _source;
  ID3D12Resource *_owned = nullptr;
  bool _simultaneous = false;
  RenderingDevice::DataFormat _source_format = RenderingDevice::DATA_FORMAT_MAX;
  ID3D11Resource *_wrapped_copy = nullptr;
  ID3D11Resource *_wrapped_shader = nullptr;
};

// Shared helper: Godot's D3D12 device and queue as D3D11On12 wants them.
std::shared_ptr<SpoutD3D11Device> spout_d3d12_get_device(RenderingDevice *p_rd);

} // namespace godot
