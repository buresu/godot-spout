#pragma once

// Logging, DXGI/RD format mapping and diagnostics. Keep <windows.h> and Spout
// headers out of here.

#include <cstdint>
#include <functional>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// DXGI_FORMAT values we use, without dxgiformat.h.
enum SpoutDxgiFormat : uint32_t {
  SPOUT_DXGI_UNKNOWN = 0,
  SPOUT_DXGI_R32G32B32A32_FLOAT = 2,
  SPOUT_DXGI_R16G16B16A16_TYPELESS = 9,
  SPOUT_DXGI_R16G16B16A16_FLOAT = 10,
  SPOUT_DXGI_R16G16B16A16_UNORM = 11,
  SPOUT_DXGI_R10G10B10A2_TYPELESS = 23,
  SPOUT_DXGI_R10G10B10A2_UNORM = 24,
  SPOUT_DXGI_R8G8B8A8_TYPELESS = 27,
  SPOUT_DXGI_R8G8B8A8_UNORM = 28,
  SPOUT_DXGI_R8G8B8A8_UNORM_SRGB = 29,
  SPOUT_DXGI_B8G8R8A8_UNORM = 87,
  SPOUT_DXGI_B8G8R8X8_UNORM = 88,
  SPOUT_DXGI_B8G8R8A8_TYPELESS = 90,
  SPOUT_DXGI_B8G8R8A8_UNORM_SRGB = 91,
  // DX9 senders advertise D3DFMT values instead of DXGI.
  SPOUT_D3DFMT_A8R8G8B8 = 21,
  SPOUT_D3DFMT_X8R8G8B8 = 22,
};

enum SpoutSendFormat {
  SPOUT_SEND_FORMAT_AUTO = 0, // RGBA16F for float sources, else RGBA8
  SPOUT_SEND_FORMAT_RGBA8 = 1,
  SPOUT_SEND_FORMAT_RGBA16F = 2,
};

// Typed format for TYPELESS/DX9/sRGB variants; UNKNOWN if unsupported.
uint32_t spout_dxgi_normalize(uint32_t p_dxgi_format);

RenderingDevice::DataFormat spout_dxgi_to_rd(uint32_t p_dxgi_format);

// RGBA8 is R8G8B8A8_UNORM: BGRA8 isn't storable by the compute pass.
uint32_t spout_send_format_to_dxgi(SpoutSendFormat p_format);

SpoutSendFormat spout_resolve_send_format(SpoutSendFormat p_requested,
                                          RenderingDevice::DataFormat p_source);

// Same memory layout (UNORM and sRGB count as equal).
bool spout_rd_formats_copy_compatible(RenderingDevice::DataFormat p_a,
                                      RenderingDevice::DataFormat p_b);

// DATA_FORMAT_MAX if there is none.
RenderingDevice::DataFormat
spout_rd_srgb_variant(RenderingDevice::DataFormat p);

String spout_dxgi_format_name(uint32_t p_dxgi_format);

// Environment switches (read once):
//   GODOT_SPOUT_LOG=1          Spout's log to %APPDATA%/Spout/godot-spout.log
//   GODOT_SPOUT_VERBOSE=1      print our messages without --verbose
//   GODOT_SPOUT_D3D12_DEBUG=1  print debug-layer messages (--gpu-validation)
bool spout_env_flag(const char *p_name);
void spout_diagnostics_init();

void spout_log(const String &p_message);
// Always printed.
void spout_info(const String &p_message);
void spout_warn(const String &p_message);
void spout_error(const String &p_message);

uint64_t spout_ticks_usec();
uint64_t spout_ticks_msec();

// Runs p_release SPOUT_RELEASE_DELAY_FRAMES frames later, when frames in flight
// no longer use the object. Leftovers at shutdown are left to the OS.
constexpr int SPOUT_RELEASE_DELAY_FRAMES = 4;
void spout_defer_release(std::function<void()> p_release);
void spout_pump_deferred_releases();
void spout_deferred_release_shutdown();

} // namespace godot
