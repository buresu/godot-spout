#pragma once

#ifdef GODOT_SPOUT_ENABLE_VULKAN

#include "SpoutBackend.hpp"

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/variant/rid.hpp>

class spoutVK;

namespace godot {

class SpoutVulkanBackend : public SpoutBackend {
public:
  SpoutVulkanBackend();
  ~SpoutVulkanBackend() override;

  bool initialize(RenderingDevice *p_rd) override;
  void release() override;
  bool is_initialized() const override;

  bool send(const RID &p_texture, uint32_t p_width, uint32_t p_height,
            const char *p_sender_name) override;
  bool update_receiver(const char *p_sender_name) override;
  bool receive(const RID &p_texture, uint32_t p_width, uint32_t p_height,
               const char *p_sender_name) override;

  uint32_t get_sender_width() const override;
  uint32_t get_sender_height() const override;

  static VkImageLayout get_texture_layout(const Ref<RDTextureFormat> &p_format);

private:
  bool create_command_pool();
  bool run_commands(const std::function<bool(VkCommandBuffer)> &p_record);
  VkImage get_texture_image(const RID &p_texture) const;
  VkFormat get_texture_format(const RID &p_texture) const;
  VkImageLayout get_texture_layout(const RID &p_texture) const;

  RenderingDevice *_rd = nullptr;
  spoutVK *_spout = nullptr;
  VkPhysicalDevice _physical_device = VK_NULL_HANDLE;
  VkDevice _device = VK_NULL_HANDLE;
  VkQueue _queue = VK_NULL_HANDLE;
  uint32_t _queue_family_index = 0;
  VkCommandPool _command_pool = VK_NULL_HANDLE;
};

} // namespace godot

#endif // GODOT_SPOUT_ENABLE_VULKAN
