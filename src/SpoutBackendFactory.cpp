#include "SpoutBackendFactory.hpp"

#include "SpoutDX12Backend.hpp"

#ifdef GODOT_SPOUT_ENABLE_OPENGL
#include "SpoutOpenGLBackend.hpp"
#endif

#ifdef GODOT_SPOUT_ENABLE_VULKAN
#include "SpoutVulkanBackend.hpp"
#endif

#include <godot_cpp/core/error_macros.hpp>

using namespace godot;

SpoutBackend *godot::spout_backend_create(const String &p_driver_name,
                                          const char *p_owner_name) {
  if (p_driver_name == "d3d12") {
    return new SpoutDX12Backend();
  }

#ifdef GODOT_SPOUT_ENABLE_OPENGL
  if (p_driver_name == "opengl3") {
    return new SpoutOpenGLBackend();
  }
#endif

#ifdef GODOT_SPOUT_ENABLE_VULKAN
  if (p_driver_name == "vulkan") {
    return new SpoutVulkanBackend();
  }
#endif

  ERR_PRINT(String(p_owner_name) +
            ": Unsupported rendering driver for Spout: " + p_driver_name);
  return nullptr;
}
