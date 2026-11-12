#pragma once

// D3D12 backend (always built).

#include "SpoutGpu.hpp"

#include <memory>

struct ID3D11Resource;
struct ID3D11Texture2D;
struct ID3D12Resource;

namespace godot {

class SpoutD3D11Device;

// Our texture that both Godot and D3D11On12 access.
class SpoutD3D12Texture {
public:
  ~SpoutD3D12Texture() { release(); }

  bool create(RenderingDevice *p_rd, uint32_t p_width, uint32_t p_height,
              uint32_t p_dxgi_format, bool p_storage);
  void release();
  bool is_valid() const { return _rid.is_valid(); }
  const RID &rid() const { return _rid; }
  // Ours with simultaneous access (enhanced barriers); otherwise Godot's.
  bool simultaneous() const { return _owned != nullptr; }

  // D3D11 view in the state Godot's p_access left the texture in.
  ID3D11Resource *wrapped(SpoutD3D11Device &p_d3d11, SpoutGodotAccess p_access,
                          bool p_written);

private:
  RenderingDevice *_rd = nullptr;
  RID _rid;
  ID3D12Resource *_owned = nullptr;
  ID3D11Resource *_wrapped_copy = nullptr;
  ID3D11Resource *_wrapped_shader = nullptr;
};

class SpoutD3D12Sender : public SpoutGpuSender {
public:
  explicit SpoutD3D12Sender(RenderingDevice *p_rd);
  ~SpoutD3D12Sender() override;

  bool ensure(uint32_t p_width, uint32_t p_height,
              SpoutSendFormat p_format) override;
  uint64_t get_generation() const override { return _generation; }
  RID get_target() const override { return _target.rid(); }
  bool target_supports_storage() const override { return true; }
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
  SpoutD3D12Texture _target;
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
  RID get_source() const override { return _source.rid(); }
  RenderingDevice::DataFormat get_source_format() const override {
    return _source_format;
  }
  bool acquire(SpoutGodotAccess p_last_access, bool p_new_frame,
               SpoutShareReceiver &p_share) override;
  String get_adapter_name() const override;

private:
  RenderingDevice *_rd = nullptr;
  std::shared_ptr<SpoutD3D11Device> _d3d11;
  ID3D11Texture2D *_shared = nullptr;
  SpoutD3D12Texture _source;
  RenderingDevice::DataFormat _source_format = RenderingDevice::DATA_FORMAT_MAX;
};

} // namespace godot
