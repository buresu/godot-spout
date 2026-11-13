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

// Slots: D3D11 shared textures on Godot's GPU imported into Vulkan (KMT
// handles, or NT handles if the driver only imports those, e.g. Intel).
// Sender: Godot writes the Spout shared texture in its own queue (a D3D11 copy
// lands later and makes D3D12 receivers repeat frames). With NT handles, D3D11
// copies each slot to the Spout texture once Godot's frame is done.
// Receiver: D3D11 copies the sender's texture into a slot right after the
// previous frame (prefetch); Godot reads it once the copy's query is done.
// Godot doesn't enable VK_KHR_external_memory_win32; drivers don't enforce it.

namespace {

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

// r_dedicated: the import needs a dedicated allocation.
bool import_supported(VkPhysicalDevice p_physical, VkFormat p_format,
                      VkImageUsageFlags p_usage,
                      VkExternalMemoryHandleTypeFlagBits p_type,
                      bool &r_dedicated) {
  VkPhysicalDeviceExternalImageFormatInfo external = {};
  external.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO;
  external.handleType = p_type;
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

// KMT first: only those can be the Spout texture. STORAGE is optional.
bool choose_import(VkPhysicalDevice p_physical, VkFormat p_format,
                   VkExternalMemoryHandleTypeFlagBits &r_type,
                   VkImageUsageFlags &r_usage, bool &r_dedicated) {
  const VkImageUsageFlags base = VK_IMAGE_USAGE_SAMPLED_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  for (VkExternalMemoryHandleTypeFlagBits type :
       {VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT,
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT}) {
    for (VkImageUsageFlags usage : {base | VK_IMAGE_USAGE_STORAGE_BIT, base}) {
      if (import_supported(p_physical, p_format, usage, type, r_dedicated)) {
        r_type = type;
        r_usage = usage;
        return true;
      }
    }
  }
  return false;
}

} // namespace

// A texture shared between our D3D11 device and Godot (Vulkan).
struct godot::SpoutVulkanSlot {
  enum State {
    FREE,
    GODOT, // Godot uses it in `frame`
    D3D11, // D3D11 copy in flight (`query`)
    READY, // ready for the next step
  };

  ID3D11Texture2D *texture = nullptr; // owns the memory
  uint64_t handle = 0;                // legacy shared handle, 0 if `nt`
  bool nt = false;
  bool written = false; // sender: published
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

std::unique_ptr<Slot> create_slot(RenderingDevice *p_rd,
                                  SpoutD3D11Device &p_d3d11, uint32_t p_width,
                                  uint32_t p_height, uint32_t p_dxgi_format,
                                  String &r_error) {
  const VkContext vk = get_context(p_rd);
  const RenderingDevice::DataFormat rd_format = spout_dxgi_to_rd(p_dxgi_format);
  const VkFormat format = rd_to_vk(rd_format);
  if (format == VK_FORMAT_UNDEFINED) {
    r_error = "unsupported format";
    return nullptr;
  }
  VkExternalMemoryHandleTypeFlagBits handle_type =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_KMT_BIT;
  VkImageUsageFlags usage = 0;
  bool dedicated = false;
  if (!choose_import(vk.physical, format, handle_type, usage, dedicated)) {
    r_error = "this GPU/driver can't import D3D11 shared textures into "
              "Vulkan for this format";
    return nullptr;
  }

  auto slot = std::make_unique<Slot>();
  slot->device = vk.device;
  slot->storage = (usage & VK_IMAGE_USAGE_STORAGE_BIT) != 0;
  slot->nt = handle_type == VK_EXTERNAL_MEMORY_HANDLE_TYPE_D3D11_TEXTURE_BIT;

  uint64_t handle = 0;
  if (!p_d3d11.create_shared_texture(p_width, p_height, p_dxgi_format,
                                     &slot->texture, &handle, slot->nt)) {
    r_error = "could not create a D3D11 texture";
    return nullptr;
  }
  // Not owned by Vulkan after import.
  struct NtHandleCloser {
    HANDLE handle;
    ~NtHandleCloser() {
      if (handle) {
        CloseHandle(handle);
      }
    }
  } nt_closer{slot->nt ? reinterpret_cast<HANDLE>(handle) : nullptr};
  if (!slot->nt) {
    slot->handle = handle;
  }
  D3D11_QUERY_DESC query_desc = {D3D11_QUERY_EVENT, 0};
  if (FAILED(p_d3d11.device()->CreateQuery(&query_desc, &slot->query))) {
    spout_release(slot->texture);
    r_error = "could not create a D3D11 query";
    return nullptr;
  }

  VkExternalMemoryImageCreateInfo external = {};
  external.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO;
  external.handleTypes = handle_type;
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
    spout_release(slot->query);
    spout_release(slot->texture);
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
    if (get_handle_props(vk.device, handle_type,
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
  import_info.handleType = handle_type;
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
      RenderingDevice::TEXTURE_TYPE_2D, rd_format,
      RenderingDevice::TEXTURE_SAMPLES_1, rd_usage,
      reinterpret_cast<uint64_t>(slot->image), p_width, p_height, 1, 1);
  if (!slot->rid.is_valid()) {
    return fail("texture_create_from_extension failed");
  }
  return slot;
}

bool create_slots(RenderingDevice *p_rd, SpoutD3D11Device &p_d3d11,
                  uint32_t p_width, uint32_t p_height, uint32_t p_dxgi_format,
                  SlotList &r_slots, String &r_error) {
  const uint32_t count = slot_count(p_rd);
  for (uint32_t i = 0; i < count; i++) {
    std::unique_ptr<Slot> slot =
        create_slot(p_rd, p_d3d11, p_width, p_height, p_dxgi_format, r_error);
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
  return _slots.empty() ? RID() : _slots[_write]->rid;
}

uint64_t SpoutVulkanSender::get_share_handle() const {
  if (copying()) {
    return _spout_handle;
  }
  return _slots.empty() ? 0 : _slots[0]->handle;
}

bool SpoutVulkanSender::target_supports_storage() const {
  return !_slots.empty() && _slots[_write]->storage;
}

String SpoutVulkanSender::get_adapter_name() const {
  return _d3d11 ? _d3d11->adapter_name() : String();
}

bool SpoutVulkanSender::ensure(uint32_t p_width, uint32_t p_height,
                               SpoutSendFormat p_format) {
  if (!_usable) {
    return false;
  }
  if (!_slots.empty() && p_width == _width && p_height == _height &&
      p_format == _format) {
    if (copying()) {
      next_write_slot();
    }
    return true;
  }
  release();

  _dxgi_format = spout_send_format_to_dxgi(p_format);
  String error;
  std::unique_ptr<Slot> first =
      create_slot(_rd, *_d3d11, p_width, p_height, _dxgi_format, error);
  const bool nt = first && first->nt;
  if (first) {
    _slots.push_back(std::move(first));
  }
  if (nt) {
    // Written, in flight, copied, spare.
    const uint32_t count = _rd->get_frame_delay() + 3;
    while (_slots.size() < count) {
      std::unique_ptr<Slot> slot =
          create_slot(_rd, *_d3d11, p_width, p_height, _dxgi_format, error);
      if (!slot) {
        break;
      }
      _slots.push_back(std::move(slot));
    }
    if (_slots.size() < count ||
        !_d3d11->create_shared_texture(p_width, p_height, _dxgi_format,
                                       &_spout, &_spout_handle)) {
      if (error.is_empty()) {
        error = "could not create the Spout texture";
      }
      destroy_slots(_rd, _slots);
    }
  }
  if (_slots.empty()) {
    spout_error("Vulkan sender: " + error + ".");
    _usable = false; // don't retry (and fail) every frame
    return false;
  }
  if (nt) {
    spout_log("Vulkan sender: the driver can't import legacy shared handles; "
              "copying finished frames (+" +
              String::num_int64(_rd->get_frame_delay()) +
              " frames of latency).");
    next_write_slot();
  }
  _width = p_width;
  _height = p_height;
  _format = p_format;
  _generation++;
  return true;
}

void SpoutVulkanSender::update() {
  for (auto &slot : _slots) {
    if (slot->state == Slot::GODOT &&
        spout_render_frame_done(_rd, slot->frame)) {
      slot->state = slot->written ? Slot::READY : Slot::FREE;
    }
    if (slot->state == Slot::D3D11 && query_done(*_d3d11, slot->query)) {
      slot->state = Slot::FREE;
    }
  }
}

void SpoutVulkanSender::next_write_slot() {
  update();
  // Oldest free, else oldest uncopied, else oldest in flight. Never one being
  // copied.
  int pick = -1;
  for (Slot::State state : {Slot::FREE, Slot::READY, Slot::GODOT}) {
    for (int i = 0; i < int(_slots.size()); i++) {
      if (_slots[i]->state == state &&
          (pick < 0 || _slots[i]->frame < _slots[pick]->frame)) {
        pick = i;
      }
    }
    if (pick >= 0) {
      break;
    }
  }
  if (pick < 0) {
    pick = _write;
  }
  _write = pick;
  Slot &slot = *_slots[_write];
  slot.state = Slot::GODOT;
  slot.frame = spout_render_frame();
  slot.written = false;
}

bool SpoutVulkanSender::deliver(SpoutShareSender &p_share) {
  update();
  int newest = -1;
  for (int i = 0; i < int(_slots.size()); i++) {
    Slot &slot = *_slots[i];
    if (slot.state != Slot::READY) {
      continue;
    }
    if (newest < 0 || slot.frame > _slots[newest]->frame) {
      if (newest >= 0) {
        _slots[newest]->state = Slot::FREE;
      }
      newest = i;
    } else {
      slot.state = Slot::FREE;
    }
  }
  if (newest < 0 || !p_share.lock()) {
    return false;
  }
  Slot &slot = *_slots[newest];
  d3d11_copy(*_d3d11, _spout, slot.texture, slot.query);
  p_share.unlock();
  slot.state = Slot::D3D11;
  return true;
}

bool SpoutVulkanSender::publish(SpoutGodotAccess p_access,
                                SpoutShareSender &p_share) {
  if (_slots.empty() || p_access == SPOUT_ACCESS_NONE) {
    return false;
  }
  if (!copying()) {
    // Godot wrote the shared texture in the frame it just submitted.
    return true;
  }
  _slots[_write]->written = true;
  return deliver(p_share);
}

bool SpoutVulkanSender::poll(SpoutShareSender &p_share) {
  return copying() && !_slots.empty() && deliver(p_share);
}

void SpoutVulkanSender::release() {
  destroy_slots(_rd, _slots);
  if (_spout) {
    // D3D11 may still be copying into it.
    ID3D11Texture2D *spout = _spout;
    spout_defer_release([spout]() { spout->Release(); });
    _spout = nullptr;
  }
  _spout_handle = 0;
  _write = 0;
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
  uint32_t width = 0, height = 0, dxgi = 0;
  if (!_d3d11->open_shared_texture(p_info.share_handle, &_shared, width, height,
                                   dxgi, r_error)) {
    return false;
  }
  if (!create_slots(_rd, *_d3d11, width, height, dxgi, _slots, r_error)) {
    close();
    return false;
  }
  _source_format = spout_dxgi_to_rd(dxgi);
  _current = 0;
  return true;
}

void SpoutVulkanReceiver::close() {
  destroy_slots(_rd, _slots);
  spout_release(_shared);
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
  if (target < 0 || !p_share.lock()) {
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
