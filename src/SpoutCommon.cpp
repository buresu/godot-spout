#include "SpoutCommon.hpp"

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/classes/rendering_server.hpp>
#include <godot_cpp/classes/time.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

#include <cstdlib>
#include <mutex>
#include <vector>

#include <SpoutUtils.h>

using namespace godot;

uint32_t godot::spout_dxgi_normalize(uint32_t p_dxgi_format) {
  switch (p_dxgi_format) {
  case SPOUT_DXGI_B8G8R8A8_UNORM:
  case SPOUT_DXGI_B8G8R8A8_TYPELESS:
  case SPOUT_DXGI_B8G8R8A8_UNORM_SRGB:
  case SPOUT_DXGI_B8G8R8X8_UNORM:
  case SPOUT_D3DFMT_A8R8G8B8:
  case SPOUT_D3DFMT_X8R8G8B8:
  case 0: // Spout's documented default when a sender doesn't set a format
    return SPOUT_DXGI_B8G8R8A8_UNORM;
  case SPOUT_DXGI_R8G8B8A8_UNORM:
  case SPOUT_DXGI_R8G8B8A8_TYPELESS:
  case SPOUT_DXGI_R8G8B8A8_UNORM_SRGB:
    return SPOUT_DXGI_R8G8B8A8_UNORM;
  case SPOUT_DXGI_R10G10B10A2_UNORM:
  case SPOUT_DXGI_R10G10B10A2_TYPELESS:
    return SPOUT_DXGI_R10G10B10A2_UNORM;
  case SPOUT_DXGI_R16G16B16A16_FLOAT:
  case SPOUT_DXGI_R16G16B16A16_TYPELESS:
    return SPOUT_DXGI_R16G16B16A16_FLOAT;
  case SPOUT_DXGI_R16G16B16A16_UNORM:
    return SPOUT_DXGI_R16G16B16A16_UNORM;
  case SPOUT_DXGI_R32G32B32A32_FLOAT:
    return SPOUT_DXGI_R32G32B32A32_FLOAT;
  default:
    return SPOUT_DXGI_UNKNOWN;
  }
}

RenderingDevice::DataFormat godot::spout_dxgi_to_rd(uint32_t p_dxgi_format) {
  switch (spout_dxgi_normalize(p_dxgi_format)) {
  case SPOUT_DXGI_B8G8R8A8_UNORM:
    return RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM;
  case SPOUT_DXGI_R8G8B8A8_UNORM:
    return RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM;
  case SPOUT_DXGI_R10G10B10A2_UNORM:
    // DXGI R10G10B10A2 has R in the low bits, which is Vulkan/RD's
    // A2B10G10R10_PACK32 (components listed from the high bits).
    return RenderingDevice::DATA_FORMAT_A2B10G10R10_UNORM_PACK32;
  case SPOUT_DXGI_R16G16B16A16_FLOAT:
    return RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT;
  case SPOUT_DXGI_R16G16B16A16_UNORM:
    return RenderingDevice::DATA_FORMAT_R16G16B16A16_UNORM;
  case SPOUT_DXGI_R32G32B32A32_FLOAT:
    return RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT;
  default:
    return RenderingDevice::DATA_FORMAT_MAX;
  }
}

uint32_t godot::spout_send_format_to_dxgi(SpoutSendFormat p_format) {
  return p_format == SPOUT_SEND_FORMAT_RGBA16F ? SPOUT_DXGI_R16G16B16A16_FLOAT
                                               : SPOUT_DXGI_R8G8B8A8_UNORM;
}

RenderingDevice::DataFormat
godot::spout_send_format_to_rd(SpoutSendFormat p_format) {
  return p_format == SPOUT_SEND_FORMAT_RGBA16F
             ? RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT
             : RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM;
}

SpoutSendFormat
godot::spout_resolve_send_format(SpoutSendFormat p_requested,
                                 RenderingDevice::DataFormat p_source) {
  if (p_requested != SPOUT_SEND_FORMAT_AUTO) {
    return p_requested;
  }
  switch (p_source) {
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT:
  case RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT:
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_UNORM:
  case RenderingDevice::DATA_FORMAT_A2B10G10R10_UNORM_PACK32:
    return SPOUT_SEND_FORMAT_RGBA16F;
  default:
    return SPOUT_SEND_FORMAT_RGBA8;
  }
}

static RenderingDevice::DataFormat strip_srgb(RenderingDevice::DataFormat p) {
  switch (p) {
  case RenderingDevice::DATA_FORMAT_R8G8B8A8_SRGB:
    return RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM;
  case RenderingDevice::DATA_FORMAT_B8G8R8A8_SRGB:
    return RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM;
  default:
    return p;
  }
}

bool godot::spout_rd_formats_copy_compatible(RenderingDevice::DataFormat p_a,
                                             RenderingDevice::DataFormat p_b) {
  return strip_srgb(p_a) == strip_srgb(p_b);
}

RenderingDevice::DataFormat
godot::spout_rd_srgb_variant(RenderingDevice::DataFormat p) {
  switch (p) {
  case RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM:
    return RenderingDevice::DATA_FORMAT_R8G8B8A8_SRGB;
  case RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM:
    return RenderingDevice::DATA_FORMAT_B8G8R8A8_SRGB;
  default:
    return RenderingDevice::DATA_FORMAT_MAX;
  }
}

String godot::spout_dxgi_format_name(uint32_t p_dxgi_format) {
  switch (p_dxgi_format) {
  case SPOUT_DXGI_B8G8R8A8_UNORM:
    return "BGRA8";
  case SPOUT_DXGI_R8G8B8A8_UNORM:
    return "RGBA8";
  case SPOUT_DXGI_R10G10B10A2_UNORM:
    return "RGB10A2";
  case SPOUT_DXGI_R16G16B16A16_FLOAT:
    return "RGBA16F";
  case SPOUT_DXGI_R16G16B16A16_UNORM:
    return "RGBA16";
  case SPOUT_DXGI_R32G32B32A32_FLOAT:
    return "RGBA32F";
  default:
    return "DXGI(" + String::num_int64(p_dxgi_format) + ")";
  }
}

bool godot::spout_env_flag(const char *p_name) {
  // Plain getenv so this also works before Godot's OS singleton is usable.
  const char *v = std::getenv(p_name);
  return v && v[0] && !(v[0] == '0' && v[1] == 0);
}

static bool s_verbose = false;

void godot::spout_diagnostics_init() {
  s_verbose = spout_env_flag("GODOT_SPOUT_VERBOSE");
  if (spout_env_flag("GODOT_SPOUT_LOG")) {
    spoututils::SetSpoutLogLevel(spoututils::SPOUT_LOG_VERBOSE);
    spoututils::EnableSpoutLogFile("godot-spout.log");
  }
}

void godot::spout_log(const String &p_message) {
  if (s_verbose) {
    UtilityFunctions::print("[godot-spout] ", p_message);
  } else {
    UtilityFunctions::print_verbose("[godot-spout] ", p_message);
  }
}

void godot::spout_info(const String &p_message) {
  UtilityFunctions::print("[godot-spout] ", p_message);
}

void godot::spout_warn(const String &p_message) {
  UtilityFunctions::push_warning("[godot-spout] " + p_message);
}

void godot::spout_error(const String &p_message) {
  UtilityFunctions::push_error("[godot-spout] " + p_message);
}

uint64_t godot::spout_ticks_usec() {
  return Time::get_singleton()->get_ticks_usec();
}

uint64_t godot::spout_ticks_msec() {
  return Time::get_singleton()->get_ticks_msec();
}

namespace {

struct DeferredRelease {
  std::function<void()> release;
  int64_t after_frame;
};
std::mutex s_deferred_lock;
std::vector<DeferredRelease> s_deferred;
bool s_pump_connected = false;

void pump_deferred_releases() {
  std::vector<std::function<void()>> due;
  {
    std::lock_guard<std::mutex> guard(s_deferred_lock);
    const int64_t frame = Engine::get_singleton()->get_frames_drawn();
    for (size_t i = 0; i < s_deferred.size();) {
      if (frame > s_deferred[i].after_frame) {
        due.push_back(std::move(s_deferred[i].release));
        s_deferred[i] = std::move(s_deferred.back());
        s_deferred.pop_back();
      } else {
        i++;
      }
    }
  }
  for (auto &fn : due) {
    fn();
  }
}

} // namespace

void godot::spout_defer_release(std::function<void()> p_release) {
  std::lock_guard<std::mutex> guard(s_deferred_lock);
  s_deferred.push_back(
      {std::move(p_release), Engine::get_singleton()->get_frames_drawn() +
                                 SPOUT_RELEASE_DELAY_FRAMES});
  if (!s_pump_connected) {
    // Stays connected for the rest of the run; pumping an empty list is free.
    RenderingServer::get_singleton()->connect(
        "frame_post_draw", callable_mp_static(&pump_deferred_releases));
    s_pump_connected = true;
  }
}

void godot::spout_deferred_release_shutdown() {
  pump_deferred_releases();
  std::lock_guard<std::mutex> guard(s_deferred_lock);
  if (s_pump_connected &&
      Engine::get_singleton()->has_singleton("RenderingServer")) {
    RenderingServer::get_singleton()->disconnect(
        "frame_post_draw", callable_mp_static(&pump_deferred_releases));
  }
  s_pump_connected = false;
  // Not released: may still be referenced by the last submitted frames.
  s_deferred.clear();
}
