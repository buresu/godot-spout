#include "SpoutVulkanGpu.hpp"

#ifdef GODOT_SPOUT_ENABLE_VULKAN

#include "SpoutD3D11.hpp"
#include "SpoutShare.hpp"

#include <godot_cpp/classes/engine.hpp>

#include <cstring>
#include <mutex>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

using namespace godot;

// How the Vulkan path works
// =========================
//
// Spout shares textures as D3D11 legacy (KMT) shared handles. Vulkan can bind
// such a texture's memory to a VkImage (external memory, handle type
// D3D11_TEXTURE_KMT). We wrap that VkImage as an RD texture with
// RenderingDevice::texture_create_from_extension(), so Godot treats it like
// any other texture:
//
//   * Sender: Godot copies the viewport into the imported image itself, inside
//     its own frame graph, with its own barriers. No private command buffers,
//     no vkQueueSubmit from us, no vkDeviceWaitIdle / fence waits (the old
//     code did both every frame, ~5 ms; plan bugs B7/B8).
//   * Receiver: Godot copies (or samples) the imported image of the sender's
//     texture into the texture materials use.
//
// Limits (documented in the README):
//   * Godot doesn't enable VK_KHR_external_memory_win32 on its device, so the
//     import is technically outside the spec. It works on drivers that don't
//     enforce this; spoutVK relied on the same thing.
//   * There is no GPU-side sync with the other process (that needs a keyed
//     mutex, which Godot doesn't enable either). New-frame signalling happens
//     after Godot submitted the frame, as with Spout's own Vulkan example.

struct godot::SpoutVulkanImport {
  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  RID rid;
  RenderingDevice::DataFormat rd_format = RenderingDevice::DATA_FORMAT_MAX;
  bool storage = false;
  bool copy = false;
};

namespace {

VkFormat rd_to_vk(RenderingDevice::DataFormat p) {
  switch (p) {
  case RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM:
    return VK_FORMAT_R8G8B8A8_UNORM;
  case RenderingDevice::DATA_FORMAT_B8G8R8A8_UNORM:
    return VK_FORMAT_B8G8R8A8_UNORM;
  case RenderingDevice::DATA_FORMAT_A2B10G10R10_UNORM_PACK32:
    return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_SFLOAT:
    return VK_FORMAT_R16G16B16A16_SFLOAT;
  case RenderingDevice::DATA_FORMAT_R16G16B16A16_UNORM:
    return VK_FORMAT_R16G16B16A16_UNORM;
  case RenderingDevice::DATA_FORMAT_R32G32B32A32_SFLOAT:
    return VK_FORMAT_R32G32B32A32_SFLOAT;
  default:
    return VK_FORMAT_UNDEFINED;
  }
}

constexpr VkExternalMemoryHandleTypeFlagBits HANDLE_TYPE =
    VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;

struct VkContext {
  VkPhysicalDevice physical = VK_NULL_HANDLE;
  VkDevice device = VK_NULL_HANDLE;
};

VkContext get_context(RenderingDevice *p_rd) {
  VkContext c;
  c.physical = reinterpret_cast<VkPhysicalDevice>(p_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE, RID(), 0));
  c.device = reinterpret_cast<VkDevice>(p_rd->get_driver_resource(
      RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, RID(), 0));
  return c;
}

bool get_luid(VkPhysicalDevice p_physical, uint64_t &r_luid) {
  VkPhysicalDeviceIDProperties id = {};
  id.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;
  VkPhysicalDeviceProperties2 props = {};
  props.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
  props.pNext = &id;
  vkGetPhysicalDeviceProperties2(p_physical, &props);
  if (!id.deviceLUIDValid) {
    return false;
  }
  // VK_LUID_SIZE bytes, laid out like Windows' LUID {DWORD Low; LONG High}.
  uint32_t low = 0, high = 0;
  std::memcpy(&low, id.deviceLUID, 4);
  std::memcpy(&high, id.deviceLUID + 4, 4);
  r_luid = (static_cast<uint64_t>(high) << 32) | low;
  return true;
}

String device_name(VkPhysicalDevice p_physical) {
  VkPhysicalDeviceProperties props = {};
  vkGetPhysicalDeviceProperties(p_physical, &props);
  return String::utf8(props.deviceName);
}

// Images whose RD wrapper was freed but which the GPU may still use. RD
// defers the free of its own objects by a few frames; the VkImage and memory
// belong to us, so we keep them around for longer than Godot's frame queue.
struct Garbage {
  VkDevice device;
  VkImage image;
  VkDeviceMemory memory;
  int64_t safe_after_frame;
};
std::mutex s_garbage_lock;
std::vector<Garbage> s_garbage;
int s_live_backends = 0;
constexpr int GARBAGE_DELAY_FRAMES = 4;

void destroy_now(const Garbage &p) {
  if (p.image) {
    vkDestroyImage(p.device, p.image, nullptr);
  }
  if (p.memory) {
    vkFreeMemory(p.device, p.memory, nullptr);
  }
}

void collect_garbage(bool p_force) {
  std::lock_guard<std::mutex> guard(s_garbage_lock);
  if (s_garbage.empty()) {
    return;
  }
  const int64_t frame = Engine::get_singleton()->get_frames_drawn();
  if (p_force) {
    // Last Vulkan backend is going away: nobody will pump this list again.
    // Wait once (scene change / quit only) rather than leak.
    vkDeviceWaitIdle(s_garbage.front().device);
  }
  for (size_t i = 0; i < s_garbage.size();) {
    if (p_force || frame > s_garbage[i].safe_after_frame) {
      destroy_now(s_garbage[i]);
      s_garbage[i] = s_garbage.back();
      s_garbage.pop_back();
    } else {
      i++;
    }
  }
}

void discard(RenderingDevice *p_rd, std::unique_ptr<SpoutVulkanImport> &p) {
  if (!p) {
    return;
  }
  if (p->rid.is_valid()) {
    p_rd->free_rid(p->rid); // frees Godot's view; the image stays ours
  }
  if (p->image || p->memory) {
    std::lock_guard<std::mutex> guard(s_garbage_lock);
    s_garbage.push_back(
        {p->device, p->image, p->memory,
         Engine::get_singleton()->get_frames_drawn() + GARBAGE_DELAY_FRAMES});
  }
  p.reset();
}

// Does the driver let us import a D3D11 texture of this format with this
// usage? Fills r_dedicated if the import needs a dedicated allocation.
bool import_supported(VkPhysicalDevice p_physical, VkFormat p_format,
                      VkImageUsageFlags p_usage, bool &r_dedicated) {
  VkPhysicalDeviceExternalImageFormatInfo external = {};
  external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
  external.handleType = HANDLE_TYPE;
  VkPhysicalDeviceImageFormatInfo2 info = {};
  info.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2;
  info.pNext = &external;
  info.format = p_format;
  info.type = VK_IMAGE_TYPE_2D;
  info.tiling = VK_IMAGE_TILING_OPTIMAL;
  info.usage = p_usage;

  VkExternalImageFormatProperties external_props = {};
  external_props.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES;
  VkImageFormatProperties2 props = {};
  props.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2;
  props.pNext = &external_props;
  if (vkGetPhysicalDeviceImageFormatProperties2(p_physical, &info, &props) !=
      VK_SUCCESS) {
    return false;
  }
  const VkExternalMemoryFeatureFlags features =
      external_props.externalMemoryProperties.externalMemoryFeatures;
  r_dedicated = (features & VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) != 0;
  return (features & VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT) != 0;
}

// Imports a D3D11 shared texture's memory as a VkImage and wraps it as an RD
// texture. p_want_storage / p_want_copy_to say how Godot will write it.
std::unique_ptr<SpoutVulkanImport>
import_texture(RenderingDevice *p_rd, const VkContext &p_vk, uint64_t p_handle,
               uint32_t p_width, uint32_t p_height,
               RenderingDevice::DataFormat p_rd_format, bool p_for_sender,
               String &r_error) {
  const VkFormat format = rd_to_vk(p_rd_format);
  if (format == VK_FORMAT_UNDEFINED) {
    r_error = "unsupported format";
    return nullptr;
  }

  // Preferred usage first; drop STORAGE (only needed for the optional
  // processing pass) if the driver can't import with it.
  const VkImageUsageFlags base = VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  VkImageUsageFlags usage = base | VK_IMAGE_USAGE_STORAGE_BIT;
  bool dedicated = false;
  if (!import_supported(p_vk.physical, format, usage, dedicated)) {
    usage = base;
    if (!import_supported(p_vk.physical, format, usage, dedicated)) {
      r_error = "this GPU/driver can't import D3D11 shared textures into "
                "Vulkan for this format";
      return nullptr;
    }
  }

  auto result = std::make_unique<SpoutVulkanImport>();
  result->device = p_vk.device;
  result->rd_format = p_rd_format;
  result->storage = (usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
  result->copy = true;

  VkExternalMemoryImageCreateInfo external = {};
  external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  external.handleTypes = HANDLE_TYPE;
  VkImageCreateInfo image_info = {};
  image_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
  image_info.pNext = &external;
  image_info.imageType = VK_IMAGE_TYPE_2D;
  image_info.format = format;
  image_info.extent = {p_width, p_height, 1};
  image_info.mipLevels = 1;
  image_info.arrayLayers = 1;
  image_info.samples = VK_SAMPLE_COUNT_1_BIT;
  image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
  image_info.usage = usage;
  image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
  image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (vkCreateImage(p_vk.device, &image_info, nullptr, &result->image) !=
      VK_SUCCESS) {
    r_error = "vkCreateImage failed";
    return nullptr;
  }

  VkMemoryRequirements req = {};
  vkGetImageMemoryRequirements(p_vk.device, result->image, &req);
  uint32_t type_bits = req.memoryTypeBits;
  // Narrow to the types the handle can live in, if the driver tells us.
  auto get_handle_props =
      reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
          vkGetDeviceProcAddr(p_vk.device,
                              "vkGetMemoryWin32HandlePropertiesKHR"));
  if (get_handle_props) {
    VkMemoryWin32HandlePropertiesKHR handle_props = {};
    handle_props.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    if (get_handle_props(p_vk.device, HANDLE_TYPE,
                         reinterpret_cast<HANDLE>(p_handle),
                         &handle_props) == VK_SUCCESS &&
        (handle_props.memoryTypeBits & type_bits)) {
      type_bits &= handle_props.memoryTypeBits;
    }
  }
  VkPhysicalDeviceMemoryProperties mem_props = {};
  vkGetPhysicalDeviceMemoryProperties(p_vk.physical, &mem_props);
  uint32_t type_index = UINT32_MAX;
  for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
    if ((type_bits & (1u << i)) && (mem_props.memoryTypes[i].propertyFlags &
                                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
      type_index = i;
      break;
    }
  }
  if (type_index == UINT32_MAX) {
    for (uint32_t i = 0; i < mem_props.memoryTypeCount; i++) {
      if (type_bits & (1u << i)) {
        type_index = i;
        break;
      }
    }
  }

  VkImportMemoryWin32HandleInfoKHR import_info = {};
  import_info.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_WIN32_HANDLE_INFO_KHR;
  import_info.handleType = HANDLE_TYPE;
  import_info.handle = reinterpret_cast<HANDLE>(p_handle);
  VkMemoryDedicatedAllocateInfo dedicated_info = {};
  dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicated_info.pNext = &import_info;
  dedicated_info.image = result->image;
  VkMemoryAllocateInfo alloc = {};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.pNext = dedicated ? static_cast<const void *>(&dedicated_info)
                          : static_cast<const void *>(&import_info);
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = type_index;
  if (type_index == UINT32_MAX ||
      vkAllocateMemory(p_vk.device, &alloc, nullptr, &result->memory) !=
          VK_SUCCESS) {
    vkDestroyImage(p_vk.device, result->image, nullptr);
    r_error = p_for_sender
                  ? "could not import the shared texture into Vulkan"
                  : "could not import the sender's texture into Vulkan. The "
                    "sender is probably running on a different GPU than "
                    "Godot, or it just closed.";
    return nullptr;
  }
  if (vkBindImageMemory(p_vk.device, result->image, result->memory, 0) !=
      VK_SUCCESS) {
    vkFreeMemory(p_vk.device, result->memory, nullptr);
    vkDestroyImage(p_vk.device, result->image, nullptr);
    r_error = "vkBindImageMemory failed";
    return nullptr;
  }

  // The RD usage must describe what the VkImage was created with.
  BitField<RenderingDevice::TextureUsageBits> rd_usage =
      RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT;
  if (result->storage) {
    rd_usage.set_flag(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
  }
  result->rid = p_rd->texture_create_from_extension(
      RenderingDevice::TEXTURE_TYPE_2D, p_rd_format,
      RenderingDevice::TEXTURE_SAMPLES_1, rd_usage,
      reinterpret_cast<uint64_t>(result->image), p_width, p_height, 1, 1);
  if (!result->rid.is_valid()) {
    vkFreeMemory(p_vk.device, result->memory, nullptr);
    vkDestroyImage(p_vk.device, result->image, nullptr);
    r_error = "texture_create_from_extension failed";
    return nullptr;
  }
  return result;
}

void backend_created() {
  std::lock_guard<std::mutex> guard(s_garbage_lock);
  s_live_backends++;
}

void backend_destroyed() {
  bool last;
  {
    std::lock_guard<std::mutex> guard(s_garbage_lock);
    last = --s_live_backends == 0;
  }
  collect_garbage(last);
}

} // namespace

// ---------------------------------------------------------------------------
// Sender

SpoutVulkanSender::SpoutVulkanSender(RenderingDevice *p_rd) : _rd(p_rd) {
  backend_created();
  VkContext vk = get_context(p_rd);
  uint64_t luid = 0;
  if (!vk.physical || !vk.device) {
    spout_error("Vulkan device handles not available.");
    return;
  }
  if (!get_luid(vk.physical, luid)) {
    spout_error("The Vulkan device has no LUID; can't find its DXGI adapter.");
    return;
  }
  // The D3D11 device must be on the same GPU as Godot's Vulkan device, or the
  // shared texture's memory can't be imported (plan bug B13).
  _d3d11 = SpoutD3D11Device::get_for_luid(luid);
  _usable = _d3d11 != nullptr;
}

SpoutVulkanSender::~SpoutVulkanSender() {
  release();
  backend_destroyed();
}

RID SpoutVulkanSender::get_target() const {
  return _import ? _import->rid : RID();
}

bool SpoutVulkanSender::target_supports_storage() const {
  return _import && _import->storage;
}

String SpoutVulkanSender::get_adapter_name() const {
  return _d3d11 ? _d3d11->adapter_name() : String();
}

bool SpoutVulkanSender::ensure(uint32_t p_width, uint32_t p_height,
                               SpoutSendFormat p_format) {
  collect_garbage(false);
  if (!_usable) {
    return false;
  }
  if (_import && p_width == _width && p_height == _height &&
      p_format == _format) {
    return true;
  }
  release();

  _dxgi_format = spout_send_format_to_dxgi(p_format);
  if (!_d3d11->create_shared_texture(p_width, p_height, _dxgi_format, &_shared,
                                     &_share_handle)) {
    return false;
  }
  String error;
  _import =
      import_texture(_rd, get_context(_rd), _share_handle, p_width, p_height,
                     spout_send_format_to_rd(p_format), true, error);
  if (!_import) {
    spout_error("Vulkan sender: " + error + ".");
    release();
    _usable = false; // don't retry (and fail) every frame
    return false;
  }
  _width = p_width;
  _height = p_height;
  _format = p_format;
  _generation++;
  return true;
}

bool SpoutVulkanSender::publish(SpoutGodotAccess p_access,
                                SpoutShareSender &p_share) {
  // Godot already wrote the frame into the imported image, which *is* the
  // shared texture's memory, in the frame it just submitted. Nothing to copy.
  // (Holding the access mutex here would not order anything on the GPU.)
  return _import != nullptr && p_access != SPOUT_ACCESS_NONE;
}

void SpoutVulkanSender::release() {
  discard(_rd, _import);
  if (_shared) {
    _shared->Release();
    _shared = nullptr;
  }
  _share_handle = 0;
  _width = _height = 0;
}

// ---------------------------------------------------------------------------
// Receiver

SpoutVulkanReceiver::SpoutVulkanReceiver(RenderingDevice *p_rd) : _rd(p_rd) {
  backend_created();
  VkContext vk = get_context(p_rd);
  if (vk.physical) {
    _adapter_name = device_name(vk.physical);
  }
}

SpoutVulkanReceiver::~SpoutVulkanReceiver() {
  close();
  backend_destroyed();
}

bool SpoutVulkanReceiver::open(const SpoutSenderInfo &p_info, String &r_error) {
  close();
  collect_garbage(false);
  const RenderingDevice::DataFormat format =
      spout_dxgi_to_rd(p_info.dxgi_format);
  if (format == RenderingDevice::DATA_FORMAT_MAX) {
    r_error = "unsupported sender format " +
              spout_dxgi_format_name(p_info.dxgi_format);
    return false;
  }
  _import = import_texture(_rd, get_context(_rd), p_info.share_handle,
                           p_info.width, p_info.height, format, false, r_error);
  return _import != nullptr;
}

void SpoutVulkanReceiver::close() { discard(_rd, _import); }

bool SpoutVulkanReceiver::is_open() const { return _import != nullptr; }

RID SpoutVulkanReceiver::get_source() const {
  return _import ? _import->rid : RID();
}

RenderingDevice::DataFormat SpoutVulkanReceiver::get_source_format() const {
  return _import ? _import->rd_format : RenderingDevice::DATA_FORMAT_MAX;
}

bool SpoutVulkanReceiver::source_supports_copy() const {
  return _import && _import->copy;
}

bool SpoutVulkanReceiver::acquire(SpoutGodotAccess p_last_access,
                                  SpoutShareReceiver &p_share) {
  collect_garbage(false);
  // The imported image aliases the sender's texture, so there is nothing to
  // copy here. Skip the very first frame: Godot's first transition of a new
  // image starts from UNDEFINED, which may discard contents.
  return _import != nullptr && p_last_access != SPOUT_ACCESS_NONE;
}

String SpoutVulkanReceiver::get_adapter_name() const { return _adapter_name; }

#endif // GODOT_SPOUT_ENABLE_VULKAN
