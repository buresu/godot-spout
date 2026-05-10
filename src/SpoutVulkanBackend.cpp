#include "SpoutVulkanBackend.hpp"

#ifdef GODOT_SPOUT_ENABLE_VULKAN

#include <godot_cpp/core/error_macros.hpp>

#include <SpoutDX/SpoutSenderNames.h>
#include <SpoutVK.h>

using namespace godot;

SpoutVulkanBackend::SpoutVulkanBackend() = default;

SpoutVulkanBackend::~SpoutVulkanBackend() { release(); }

bool SpoutVulkanBackend::initialize(RenderingDevice *p_rd) {
  if (is_initialized()) {
    return true;
  }
  if (!p_rd) {
    return false;
  }

  _rd = p_rd;
  _physical_device = reinterpret_cast<VkPhysicalDevice>(
      _rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE,
                               RID(), 0));
  _device = reinterpret_cast<VkDevice>(
      _rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE,
                               RID(), 0));
  _queue = reinterpret_cast<VkQueue>(
      _rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE,
                               RID(), 0));
  _queue_family_index = static_cast<uint32_t>(
      _rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_QUEUE_FAMILY,
                               RID(), 0));

  if (!_physical_device || !_device || !_queue) {
    release();
    return false;
  }

  _spout = new spoutVK();
  if (!create_command_pool()) {
    release();
    return false;
  }

  return true;
}

void SpoutVulkanBackend::release() {
  if (_device && _command_pool) {
    vkDeviceWaitIdle(_device);
    vkDestroyCommandPool(_device, _command_pool, nullptr);
  }
  _command_pool = VK_NULL_HANDLE;

  if (_spout) {
    _spout->ReleaseReceiver();
    _spout->ReleaseSender();
    delete _spout;
    _spout = nullptr;
  }

  _rd = nullptr;
  _physical_device = VK_NULL_HANDLE;
  _device = VK_NULL_HANDLE;
  _queue = VK_NULL_HANDLE;
  _queue_family_index = 0;
}

bool SpoutVulkanBackend::is_initialized() const {
  return _spout && _device && _queue && _command_pool;
}

bool SpoutVulkanBackend::send(const RID &p_texture, uint32_t p_width,
                              uint32_t p_height, const char *p_sender_name) {
  if (!is_initialized() || !p_texture.is_valid()) {
    return false;
  }

  if (p_sender_name && p_sender_name[0]) {
    _spout->SetSenderName(p_sender_name);
  }

  VkImage image = get_texture_image(p_texture);
  VkFormat format = get_texture_format(p_texture);
  VkImageLayout layout = get_texture_layout(p_texture);
  if (!image || format == VK_FORMAT_UNDEFINED) {
    return false;
  }

  return run_commands([&](VkCommandBuffer p_command_buffer) {
    return _spout->SendImage(_physical_device, _device, p_command_buffer,
                             image, layout, p_width, p_height, format);
  });
}

bool SpoutVulkanBackend::update_receiver(const char *p_sender_name) {
  if (!is_initialized()) {
    return false;
  }

  if (p_sender_name && p_sender_name[0]) {
    spoutSenderNames sender_names;
    sender_names.SetActiveSender(p_sender_name);
  }

  return _spout->ReceiveSenderTexture(_physical_device, _device) != nullptr;
}

bool SpoutVulkanBackend::receive(const RID &p_texture, uint32_t p_width,
                                 uint32_t p_height, const char *p_sender_name) {
  if (!is_initialized() || !p_texture.is_valid()) {
    return false;
  }

  if (p_sender_name && p_sender_name[0]) {
    spoutSenderNames sender_names;
    sender_names.SetActiveSender(p_sender_name);
  }

  VkImage image = get_texture_image(p_texture);
  VkFormat format = get_texture_format(p_texture);
  VkImageLayout layout = get_texture_layout(p_texture);
  if (!image || format == VK_FORMAT_UNDEFINED) {
    return false;
  }

  return run_commands([&](VkCommandBuffer p_command_buffer) {
    return _spout->ReceiveImage(_physical_device, _device, p_command_buffer,
                                image, layout, format, p_width, p_height);
  });
}

uint32_t SpoutVulkanBackend::get_sender_width() const {
  return _spout ? _spout->GetSenderWidth() : 0;
}

uint32_t SpoutVulkanBackend::get_sender_height() const {
  return _spout ? _spout->GetSenderHeight() : 0;
}

VkImageLayout
SpoutVulkanBackend::get_texture_layout(const Ref<RDTextureFormat> &p_format) {
  if (p_format.is_valid() &&
      (p_format->get_usage_bits() &
       RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT)) {
    return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
  }
  return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool SpoutVulkanBackend::create_command_pool() {
  VkCommandPoolCreateInfo pool_info{};
  pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
  pool_info.queueFamilyIndex = _queue_family_index;

  return vkCreateCommandPool(_device, &pool_info, nullptr, &_command_pool) ==
         VK_SUCCESS;
}

bool SpoutVulkanBackend::run_commands(
    const std::function<bool(VkCommandBuffer)> &p_record) {
  VkCommandBufferAllocateInfo allocate_info{};
  allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocate_info.commandPool = _command_pool;
  allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocate_info.commandBufferCount = 1;

  VkCommandBuffer command_buffer = VK_NULL_HANDLE;
  if (vkAllocateCommandBuffers(_device, &allocate_info, &command_buffer) !=
      VK_SUCCESS) {
    return false;
  }

  VkCommandBufferBeginInfo begin_info{};
  begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  begin_info.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  bool ok = vkBeginCommandBuffer(command_buffer, &begin_info) == VK_SUCCESS &&
            p_record(command_buffer) &&
            vkEndCommandBuffer(command_buffer) == VK_SUCCESS;

  if (ok) {
    VkFenceCreateInfo fence_info{};
    fence_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;

    VkFence fence = VK_NULL_HANDLE;
    ok = vkCreateFence(_device, &fence_info, nullptr, &fence) == VK_SUCCESS;
    if (ok) {
      VkSubmitInfo submit_info{};
      submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
      submit_info.commandBufferCount = 1;
      submit_info.pCommandBuffers = &command_buffer;

      ok = vkQueueSubmit(_queue, 1, &submit_info, fence) == VK_SUCCESS &&
           vkWaitForFences(_device, 1, &fence, VK_TRUE, UINT64_MAX) ==
               VK_SUCCESS;
      vkDestroyFence(_device, fence, nullptr);
    }
  }

  vkFreeCommandBuffers(_device, _command_pool, 1, &command_buffer);
  return ok;
}

VkImage SpoutVulkanBackend::get_texture_image(const RID &p_texture) const {
  return reinterpret_cast<VkImage>(_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_TEXTURE, p_texture, 0));
}

VkFormat SpoutVulkanBackend::get_texture_format(const RID &p_texture) const {
  auto format = _rd->texture_get_format(p_texture);
  if (!format.is_valid()) {
    return VK_FORMAT_UNDEFINED;
  }

  switch (format->get_format()) {
  case RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case RenderingDevice::DATA_FORMAT_A2B10G10R10_UNORM_PACK32:
    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_UNORM:
    return VK_FORMAT_R16G16B16A16_UNORM;
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  default:
    ERR_PRINT("SpoutVulkanBackend: Unsupported Vulkan texture format.");
    return VK_FORMAT_UNDEFINED;
  }
}

VkImageLayout
SpoutVulkanBackend::get_texture_layout(const RID &p_texture) const {
  return get_texture_layout(_rd->texture_get_format(p_texture));
}

#endif // GODOT_SPOUT_ENABLE_VULKAN
