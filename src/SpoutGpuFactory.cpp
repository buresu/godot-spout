#include "SpoutGpu.hpp"

#include "SpoutD3D12Gpu.hpp"
#include "SpoutVulkanGpu.hpp"

using namespace godot;

SpoutGpuSender *godot::spout_gpu_sender_create(RenderingDevice *p_rd,
                                               const String &p_driver) {
  if (!p_rd) {
    return nullptr;
  }
  if (p_driver == "d3d12") {
    return new SpoutD3D12Sender(p_rd);
  }
#ifdef GODOT_SPOUT_ENABLE_VULKAN
  if (p_driver == "vulkan") {
    return new SpoutVulkanSender(p_rd);
  }
#endif
  return nullptr;
}

SpoutGpuReceiver *godot::spout_gpu_receiver_create(RenderingDevice *p_rd,
                                                   const String &p_driver) {
  if (!p_rd) {
    return nullptr;
  }
  if (p_driver == "d3d12") {
    return new SpoutD3D12Receiver(p_rd);
  }
#ifdef GODOT_SPOUT_ENABLE_VULKAN
  if (p_driver == "vulkan") {
    return new SpoutVulkanReceiver(p_rd);
  }
#endif
  return nullptr;
}
