#include "SpoutVulkanGpu.hpp"

#ifdef GODOT_SPOUT_ENABLE_VULKAN

#include "SpoutD3D11.hpp"
#include "SpoutShare.hpp"

#include <chrono>
#include <cstring>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d11.h>

#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <vulkan/vulkan.h>

using namespace godot;

// Slots: textures created by our D3D11 device on Godot's GPU and imported
// into Vulkan as RD textures (D3D11_TEXTURE_KMT external memory).
//
// Sender: the Spout shared texture is a slot Godot writes in its own queue,
// like the D3D12 sender. (Copying it through D3D11 instead updated it later
// in the refresh and made D3D12 receivers repeat frames.)
//
// Receiver: D3D11 copies the sender's texture into a slot under the access
// mutex right after the previous frame (prefetch). Godot reads the slot once
// the copy's query is done and releases it once that frame's fence was
// waited for (spout_render_frame_done).
//
// Limit: Godot doesn't enable VK_KHR_external_memory_win32, so the import is
// technically outside the spec; it works on drivers that don't enforce it.

namespace {

// Receivers hold the access mutex only while they submit a copy.
constexpr uint32_t MUTEX_TIMEOUT_MS = 4;
// Bounded wait for a prefetched copy that isn't done yet.
constexpr int64_t RECEIVE_WAIT_USEC = 2000;

// Read by Godot (up to get_frame_delay()), being copied, ready.
uint32_t slot_count(RenderingDevice *p_rd) {
  return p_rd->get_frame_delay() + 2;
}

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

// On Godot's GPU: memory can only be imported on the GPU that created it.
std::shared_ptr<SpoutD3D11Device> get_d3d11(RenderingDevice *p_rd) {
  VkContext vk = get_context(p_rd);
  uint64_t luid = 0;
  if (!vk.physical || !vk.device) {
    spout_error("Vulkan device handles not available.");
    return nullptr;
  }
  if (!get_luid(vk.physical, luid)) {
    spout_error("The Vulkan device has no LUID; can't find its DXGI adapter.");
    return nullptr;
  }
  return SpoutD3D11Device::get_for_luid(luid);
}

template <typename T> void safe_release(T *&p) {
  if (p) {
    p->Release();
    p = nullptr;
  }
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

} // namespace

// A texture shared between our D3D11 device and Godot (Vulkan).
struct godot::SpoutVulkanSlot {
  enum State {
    FREE,
    GODOT, // Godot reads it in `frame`
    D3D11, // D3D11 copy into it in flight (`query`)
    READY, // D3D11 copy done, not read yet
  };

  ID3D11Texture2D *texture = nullptr; // owns the memory
  uint64_t handle = 0;                // legacy shared handle of `texture`
  ID3D11Query *query = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  RID rid;
  bool storage = false;
  State state = FREE;
  uint64_t frame = 0; // GODOT: render frame; D3D11 / READY: copy sequence
};

namespace {

using Slot = SpoutVulkanSlot;
using SlotList = std::vector<std::unique_ptr<Slot>>;

bool query_done(SpoutD3D11Device &p_d3d11, ID3D11Query *p_query) {
  BOOL done = FALSE;
  return p_d3d11.context()->GetData(p_query, &done, sizeof(done),
                                    D3D11_ASYNC_GETDATA_DONOTFLUSH) == S_OK &&
         done;
}

void d3d11_copy(SpoutD3D11Device &p_d3d11, ID3D11Resource *p_dst,
                ID3D11Resource *p_src, ID3D11Query *p_query) {
  ID3D11DeviceContext *ctx = p_d3d11.context();
  ctx->CopyResource(p_dst, p_src);
  ctx->End(p_query);
  ctx->Flush();
}

// Deferred past Godot's frames in flight. The D3D11 texture owns the memory,
// so it goes last.
void destroy_slot(RenderingDevice *p_rd, std::unique_ptr<Slot> &p_slot) {
  if (!p_slot) {
    return;
  }
  if (p_slot->rid.is_valid()) {
    p_rd->free_rid(p_slot->rid); // frees Godot's view; the image stays ours
  }
  const VkDevice device = p_slot->device;
  const VkImage image = p_slot->image;
  const VkDeviceMemory memory = p_slot->memory;
  ID3D11Texture2D *texture = p_slot->texture;
  ID3D11Query *query = p_slot->query;
  spout_defer_release([device, image, memory, texture, query]() {
    if (image) {
      vkDestroyImage(device, image, nullptr);
    }
    if (memory) {
      vkFreeMemory(device, memory, nullptr);
    }
    if (query) {
      query->Release();
    }
    if (texture) {
      texture->Release();
    }
  });
  p_slot.reset();
}

void destroy_slots(RenderingDevice *p_rd, SlotList &p_slots) {
  for (auto &slot : p_slots) {
    destroy_slot(p_rd, slot);
  }
  p_slots.clear();
}

// Creates a D3D11 shared texture and imports it into Vulkan as an RD texture.
std::unique_ptr<Slot> create_slot(RenderingDevice *p_rd,
                                  SpoutD3D11Device &p_d3d11, uint32_t p_width,
                                  uint32_t p_height, uint32_t p_dxgi_format,
                                  RenderingDevice::DataFormat p_rd_format,
                                  String &r_error) {
  const VkContext vk = get_context(p_rd);
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
  if (!import_supported(vk.physical, format, usage, dedicated)) {
    usage = base;
    if (!import_supported(vk.physical, format, usage, dedicated)) {
      r_error = "this GPU/driver can't import D3D11 shared textures into "
                "Vulkan for this format";
      return nullptr;
    }
  }

  auto slot = std::make_unique<Slot>();
  slot->device = vk.device;
  slot->storage = (usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;

  uint64_t &handle = slot->handle;
  if (!p_d3d11.create_shared_texture(p_width, p_height, p_dxgi_format,
                                     &slot->texture, &handle)) {
    r_error = "could not create a D3D11 texture";
    return nullptr;
  }
  D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
  if (FAILED(p_d3d11.device()->CreateQuery(&query_desc, &slot->query))) {
    safe_release(slot->texture);
    r_error = "could not create a D3D11 query";
    return nullptr;
  }

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
  auto fail = [&](const char *p_error) {
    if (slot->memory) {
      vkFreeMemory(vk.device, slot->memory, nullptr);
    }
    if (slot->image) {
      vkDestroyImage(vk.device, slot->image, nullptr);
    }
    safe_release(slot->query);
    safe_release(slot->texture);
    r_error = p_error;
    return nullptr;
  };
  if (vkCreateImage(vk.device, &image_info, nullptr, &slot->image) !=
      VK_SUCCESS) {
    slot->image = VK_NULL_HANDLE;
    return fail("vkCreateImage failed");
  }

  VkMemoryRequirements req = {};
  vkGetImageMemoryRequirements(vk.device, slot->image, &req);
  uint32_t type_bits = req.memoryTypeBits;
  // Narrow to the types the handle can live in, if the driver tells us.
  auto get_handle_props =
      reinterpret_cast<PFN_vkGetMemoryWin32HandlePropertiesKHR>(
          vkGetDeviceProcAddr(vk.device,
                              "vkGetMemoryWin32HandlePropertiesKHR"));
  if (get_handle_props) {
    VkMemoryWin32HandlePropertiesKHR handle_props = {};
    handle_props.sType = VK_STRUCTURE_TYPE_MEMORY_WIN32_HANDLE_PROPERTIES_KHR;
    if (get_handle_props(vk.device, HANDLE_TYPE,
                         reinterpret_cast<HANDLE>(handle),
                         &handle_props) == VK_SUCCESS &&
        (handle_props.memoryTypeBits & type_bits)) {
      type_bits &= handle_props.memoryTypeBits;
    }
  }
  VkPhysicalDeviceMemoryProperties mem_props = {};
  vkGetPhysicalDeviceMemoryProperties(vk.physical, &mem_props);
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
  import_info.handle = reinterpret_cast<HANDLE>(handle);
  VkMemoryDedicatedAllocateInfo dedicated_info = {};
  dedicated_info.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO;
  dedicated_info.pNext = &import_info;
  dedicated_info.image = slot->image;
  VkMemoryAllocateInfo alloc = {};
  alloc.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  alloc.pNext = dedicated ? static_cast<const void *>(&dedicated_info)
                          : static_cast<const void *>(&import_info);
  alloc.allocationSize = req.size;
  alloc.memoryTypeIndex = type_index;
  if (type_index == UINT32_MAX ||
      vkAllocateMemory(vk.device, &alloc, nullptr, &slot->memory) !=
          VK_SUCCESS) {
    slot->memory = VK_NULL_HANDLE;
    return fail("could not import a D3D11 texture into Vulkan");
  }
  if (vkBindImageMemory(vk.device, slot->image, slot->memory, 0) !=
      VK_SUCCESS) {
    return fail("vkBindImageMemory failed");
  }

  // The RD usage must describe what the VkImage was created with.
  BitField<RenderingDevice::TextureUsageBits> rd_usage =
      RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
      RenderingDevice::TEXTURE_USAGE_CAN_COPY_TO_BIT;
  if (slot->storage) {
    rd_usage.set_flag(RenderingDevice::TEXTURE_USAGE_STORAGE_BIT);
  }
  slot->rid = p_rd->texture_create_from_extension(
      RenderingDevice::TEXTURE_TYPE_2D, p_rd_format,
      RenderingDevice::TEXTURE_SAMPLES_1, rd_usage,
      reinterpret_cast<uint64_t>(slot->image), p_width, p_height, 1, 1);
  if (!slot->rid.is_valid()) {
    return fail("texture_create_from_extension failed");
  }
  return slot;
}

bool create_slots(RenderingDevice *p_rd, SpoutD3D11Device &p_d3d11,
                  uint32_t p_width, uint32_t p_height, uint32_t p_dxgi_format,
                  RenderingDevice::DataFormat p_rd_format, SlotList &r_slots,
                  String &r_error) {
  const uint32_t count = slot_count(p_rd);
  for (uint32_t i = 0; i < count; i++) {
    std::unique_ptr<Slot> slot = create_slot(
        p_rd, p_d3d11, p_width, p_height, p_dxgi_format, p_rd_format, r_error);
    if (!slot) {
      destroy_slots(p_rd, r_slots);
      return false;
    }
    r_slots.push_back(std::move(slot));
  }
  return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Sender

SpoutVulkanSender::SpoutVulkanSender(RenderingDevice *p_rd)
    : _rd(p_rd), _d3d11(get_d3d11(p_rd)) {
  _usable = _d3d11 != nullptr;
}

SpoutVulkanSender::~SpoutVulkanSender() { release(); }

RID SpoutVulkanSender::get_target() const {
  return _shared ? _shared->rid : RID();
}

uint64_t SpoutVulkanSender::get_share_handle() const {
  return _shared ? _shared->handle : 0;
}

bool SpoutVulkanSender::target_supports_storage() const {
  return _shared && _shared->storage;
}

String SpoutVulkanSender::get_adapter_name() const {
  return _d3d11 ? _d3d11->adapter_name() : String();
}

bool SpoutVulkanSender::ensure(uint32_t p_width, uint32_t p_height,
                               SpoutSendFormat p_format) {
  if (!_usable) {
    return false;
  }
  if (_shared && p_width == _width && p_height == _height &&
      p_format == _format) {
    return true;
  }
  release();

  _dxgi_format = spout_send_format_to_dxgi(p_format);
  String error;
  _shared = create_slot(_rd, *_d3d11, p_width, p_height, _dxgi_format,
                        spout_send_format_to_rd(p_format), error);
  if (!_shared) {
    spout_error("Vulkan sender: " + error + ".");
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
  // Godot already wrote the shared texture in the frame it just submitted.
  // (Holding the access mutex here would not order anything on the GPU.)
  return _shared != nullptr && p_access != SPOUT_ACCESS_NONE;
}

void SpoutVulkanSender::release() {
  destroy_slot(_rd, _shared);
  _width = _height = 0;
}

// ---------------------------------------------------------------------------
// Receiver

SpoutVulkanReceiver::SpoutVulkanReceiver(RenderingDevice *p_rd)
    : _rd(p_rd), _d3d11(get_d3d11(p_rd)) {
  VkContext vk = get_context(p_rd);
  if (vk.physical) {
    _adapter_name = device_name(vk.physical);
  }
}

SpoutVulkanReceiver::~SpoutVulkanReceiver() { close(); }

String SpoutVulkanReceiver::get_adapter_name() const { return _adapter_name; }

RID SpoutVulkanReceiver::get_source() const {
  return _slots.empty() ? RID() : _slots[_current]->rid;
}

bool SpoutVulkanReceiver::open(const SpoutSenderInfo &p_info, String &r_error) {
  close();
  if (!_d3d11) {
    r_error = "no D3D11 device on the Vulkan GPU";
    return false;
  }
  if (!_d3d11->open_shared_texture(p_info.share_handle, &_shared, r_error)) {
    return false;
  }
  // Use the format of the texture itself; what senders advertise is
  // sometimes wrong.
  D3D11_TEXTURE2D_DESC desc = {};
  _shared->GetDesc(&desc);
  const uint32_t dxgi = spout_dxgi_normalize(desc.Format);
  _source_format = spout_dxgi_to_rd(dxgi);
  if (_source_format == RenderingDevice::DATA_FORMAT_MAX) {
    r_error =
        "unsupported sender format " + spout_dxgi_format_name(desc.Format);
    close();
    return false;
  }
  if (!create_slots(_rd, *_d3d11, desc.Width, desc.Height, dxgi, _source_format,
                    _slots, r_error)) {
    close();
    return false;
  }
  _current = 0;
  return true;
}

void SpoutVulkanReceiver::close() {
  destroy_slots(_rd, _slots);
  safe_release(_shared);
  _current = 0;
  _source_format = RenderingDevice::DATA_FORMAT_MAX;
}

void SpoutVulkanReceiver::update(bool &r_copying, int &r_newest) {
  r_copying = false;
  r_newest = -1;
  for (int i = 0; i < int(_slots.size()); i++) {
    Slot &slot = *_slots[i];
    if (slot.state == Slot::GODOT && spout_render_frame_done(_rd, slot.frame)) {
      slot.state = Slot::FREE;
    }
    if (slot.state == Slot::D3D11) {
      if (query_done(*_d3d11, slot.query)) {
        slot.state = Slot::READY;
      } else {
        r_copying = true;
      }
    }
    if (slot.state == Slot::READY) {
      if (r_newest < 0 || slot.frame > _slots[r_newest]->frame) {
        if (r_newest >= 0) {
          _slots[r_newest]->state = Slot::FREE;
        }
        r_newest = i;
      } else {
        slot.state = Slot::FREE;
      }
    }
  }
}

bool SpoutVulkanReceiver::start_copy(SpoutShareReceiver &p_share) {
  // Into a slot Godot isn't reading.
  int target = -1;
  for (int i = 0; i < int(_slots.size()); i++) {
    if (_slots[i]->state == Slot::FREE && i != _current) {
      target = i;
      break;
    }
  }
  if (target < 0 || !p_share.lock(MUTEX_TIMEOUT_MS)) {
    return false;
  }
  Slot &slot = *_slots[target];
  d3d11_copy(*_d3d11, slot.texture, _shared, slot.query);
  p_share.unlock();
  slot.state = Slot::D3D11;
  slot.frame = ++_copy_seq;
  return true;
}

void SpoutVulkanReceiver::prefetch(SpoutGodotAccess p_last_access,
                                   bool p_new_frame,
                                   SpoutShareReceiver &p_share) {
  if (_slots.empty()) {
    return;
  }
  bool copying = false;
  int newest = -1;
  update(copying, newest);
  if (p_new_frame || (!copying && newest < 0)) {
    start_copy(p_share);
  }
}

bool SpoutVulkanReceiver::acquire(SpoutGodotAccess p_last_access,
                                  bool p_new_frame,
                                  SpoutShareReceiver &p_share) {
  if (_slots.empty()) {
    return false;
  }
  bool copying = false;
  int newest = -1;
  update(copying, newest);
  if (!copying && newest < 0 && start_copy(p_share)) {
    copying = true; // nothing was prefetched
  }
  if (newest < 0 && copying) {
    // Started by prefetch() a frame earlier, so normally done already.
    const auto until = std::chrono::steady_clock::now() +
                       std::chrono::microseconds(RECEIVE_WAIT_USEC);
    while (newest < 0 && copying && std::chrono::steady_clock::now() < until) {
      std::this_thread::yield();
      update(copying, newest);
    }
  }

  if (newest >= 0) {
    _current = newest;
    _slots[_current]->state = Slot::GODOT;
    _slots[_current]->frame = spout_render_frame();
    return true;
  }
  if (p_last_access == SPOUT_ACCESS_NONE &&
      _slots[_current]->state != Slot::D3D11) {
    // The core primes get_source() with a read this frame.
    _slots[_current]->state = Slot::GODOT;
    _slots[_current]->frame = spout_render_frame();
  }
  return false;
}

#endif // GODOT_SPOUT_ENABLE_VULKAN
