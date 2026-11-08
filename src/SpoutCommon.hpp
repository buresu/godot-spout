#pragma once

// Small helpers shared by all godot-spout classes: logging, pixel format
// mapping between DXGI (what Spout advertises), Godot's RenderingDevice and
// Vulkan, and environment switches for diagnostics.
//
// This header deliberately does not include <windows.h> or any Spout header so
// that it can be used from the Godot-facing classes.

#include <cstdint>

#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/string.hpp>

namespace godot {

// Values of DXGI_FORMAT that godot-spout produces or accepts. Kept as plain
// integers so callers don't need dxgiformat.h.
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

// Pixel format of what a sender publishes.
enum SpoutSendFormat {
  SPOUT_SEND_FORMAT_AUTO = 0, // RGBA16F for float sources, else RGBA8
  SPOUT_SEND_FORMAT_RGBA8 = 1,
  SPOUT_SEND_FORMAT_RGBA16F = 2,
};

// Normalises what a sender advertises (TYPELESS, DX9 or sRGB variants) to the
// typed DXGI format we create or copy as. Returns SPOUT_DXGI_UNKNOWN for
// formats godot-spout can't receive.
uint32_t spout_dxgi_normalize(uint32_t p_dxgi_format);

// Godot RD format with the same memory layout as a (normalised) DXGI format.
RenderingDevice::DataFormat spout_dxgi_to_rd(uint32_t p_dxgi_format);

// DXGI format for a send format; RGBA8 is R8G8B8A8_UNORM (see SpoutSender.cpp
// for why not BGRA).
uint32_t spout_send_format_to_dxgi(SpoutSendFormat p_format);
RenderingDevice::DataFormat spout_send_format_to_rd(SpoutSendFormat p_format);

// Resolves AUTO against the source texture's format.
SpoutSendFormat spout_resolve_send_format(SpoutSendFormat p_requested,
                                          RenderingDevice::DataFormat p_source);

// True if two RD formats can be copied with texture_copy (same memory layout;
// UNORM and sRGB variants of the same layout count as equal).
bool spout_rd_formats_copy_compatible(RenderingDevice::DataFormat p_a,
                                      RenderingDevice::DataFormat p_b);

// sRGB variant of an 8-bit RD format, or DATA_FORMAT_MAX if there is none.
RenderingDevice::DataFormat
spout_rd_srgb_variant(RenderingDevice::DataFormat p);

String spout_dxgi_format_name(uint32_t p_dxgi_format);

// Diagnostics, switched on by environment variables (read once):
//   GODOT_SPOUT_LOG=1          Spout's own log, written to godot-spout.log in
//                              %APPDATA%/Spout (see SpoutUtils).
//   GODOT_SPOUT_VERBOSE=1      godot-spout's own messages printed always, not
//                              only with --verbose.
//   GODOT_SPOUT_D3D12_DEBUG=1  read and print the D3D12 / D3D11 debug-layer
//                              messages (needs --gpu-validation).
bool spout_env_flag(const char *p_name);
void spout_diagnostics_init();

void spout_log(const String &p_message);
void spout_warn(const String &p_message);
void spout_error(const String &p_message);

uint64_t spout_ticks_usec();
uint64_t spout_ticks_msec();

} // namespace godot
