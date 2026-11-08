#include "SpoutShare.hpp"

#include "SpoutCommon.hpp"

#include <mutex>
#include <set>
#include <string>

#include <SpoutFrameCount.h>
#include <SpoutSenderNames.h>

using namespace godot;

namespace {

// Spout's names for the per-sender access mutex. Must match
// spoutFrameCount::CreateAccessMutex so other Spout applications use the same
// object.
std::string access_mutex_name(const char *p_sender) {
  return std::string(p_sender) + "_SpoutAccessMutex";
}

bool wait_mutex(HANDLE p_mutex, uint32_t p_timeout_ms) {
  if (!p_mutex) {
    // Senders without a mutex exist (old SDKs); behave like Spout and don't
    // block.
    return true;
  }
  switch (WaitForSingleObject(p_mutex, p_timeout_ms)) {
  case WAIT_OBJECT_0:
    return true;
  case WAIT_ABANDONED:
    // The previous owner exited without releasing it. We own it now.
    return true;
  default:
    return false;
  }
}

} // namespace

PackedStringArray godot::spout_get_sender_list() {
  // spoutSenderNames keeps no per-instance state that matters for reading the
  // name list, but constructing it reads the registry; keep one around.
  static std::mutex s_lock;
  static spoutSenderNames *s_names = nullptr;
  std::lock_guard<std::mutex> guard(s_lock);
  if (!s_names) {
    s_names = new spoutSenderNames();
  }
  std::set<std::string> names;
  PackedStringArray result;
  if (s_names->GetSenderNames(&names)) {
    for (const std::string &n : names) {
      result.push_back(String::utf8(n.c_str()));
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// SpoutShareSender

struct SpoutShareSender::Impl {
  spoutSenderNames names;
  spoutFrameCount frame;
  HANDLE mutex = nullptr;
  char name[256] = {};
};

SpoutShareSender::SpoutShareSender() : _impl(std::make_unique<Impl>()) {}

SpoutShareSender::~SpoutShareSender() { unregister(); }

bool SpoutShareSender::publish(const String &p_requested_name,
                               uint64_t p_share_handle, uint32_t p_width,
                               uint32_t p_height, uint32_t p_dxgi_format) {
  HANDLE handle = reinterpret_cast<HANDLE>(p_share_handle);

  if (_registered && p_requested_name == _requested_name) {
    // Same sender, new texture (resize or format change). Receivers notice the
    // new share handle in the shared memory and re-open it; the sender stays
    // in the list, so Resolume keeps the source selected.
    return _impl->names.UpdateSender(_impl->name, p_width, p_height, handle,
                                     p_dxgi_format);
  }

  unregister();

  CharString utf8 = p_requested_name.utf8();
  if (utf8.length() == 0 || utf8.length() >= 255) {
    spout_error("Sender name must be 1-254 bytes long: \"" + p_requested_name +
                "\"");
    return false;
  }
  strcpy_s(_impl->name, sizeof(_impl->name), utf8.get_data());

  // CreateSender appends "_1", "_2", ... when the name is taken (for example
  // by a sender left over from a crashed process) and writes the final name
  // back into _impl->name.
  if (!_impl->names.CreateSender(_impl->name, p_width, p_height, handle,
                                 p_dxgi_format)) {
    spout_error("Could not register Spout sender \"" + p_requested_name +
                "\" (too many senders?)");
    _impl->name[0] = 0;
    return false;
  }

  _impl->mutex =
      CreateMutexA(nullptr, FALSE, access_mutex_name(_impl->name).c_str());
  // Creates the "<name>_Count_Semaphore" receivers use for new-frame
  // detection and fps (only if frame counting is enabled in SpoutSettings).
  _impl->frame.EnableFrameCount(_impl->name);

  _requested_name = p_requested_name;
  _registered = true;
  _frames = 0;

  if (get_name() != p_requested_name) {
    spout_warn("Spout sender name \"" + p_requested_name +
               "\" is taken; registered as \"" + get_name() + "\".");
  }
  spout_log("Registered sender \"" + get_name() + "\" " +
            String::num_int64(p_width) + "x" + String::num_int64(p_height) +
            " " + spout_dxgi_format_name(p_dxgi_format));
  return true;
}

void SpoutShareSender::unregister() {
  if (!_registered) {
    return;
  }
  spout_log("Unregistered sender \"" + get_name() + "\"");
  _impl->frame.CleanupFrameCount();
  if (_impl->mutex) {
    CloseHandle(_impl->mutex);
    _impl->mutex = nullptr;
  }
  _impl->names.ReleaseSenderName(_impl->name);
  _impl->name[0] = 0;
  _registered = false;
}

String SpoutShareSender::get_name() const { return String::utf8(_impl->name); }

bool SpoutShareSender::lock(uint32_t p_timeout_ms) {
  return wait_mutex(_impl->mutex, p_timeout_ms);
}

void SpoutShareSender::unlock() {
  if (_impl->mutex) {
    ReleaseMutex(_impl->mutex);
  }
}

void SpoutShareSender::signal_frame() {
  _impl->frame.SetNewFrame();
  _frames++;
}

// ---------------------------------------------------------------------------
// SpoutShareReceiver

struct SpoutShareReceiver::Impl {
  spoutSenderNames names;
  spoutFrameCount frame;
  HANDLE mutex = nullptr;
};

SpoutShareReceiver::SpoutShareReceiver() : _impl(std::make_unique<Impl>()) {}

SpoutShareReceiver::~SpoutShareReceiver() { disconnect(); }

SpoutShareReceiver::PollResult
SpoutShareReceiver::poll(const String &p_name, SpoutSenderInfo &r_info) {
  char name[256] = {};
  if (p_name.is_empty()) {
    // Follow whatever SpoutSettings / the last started sender made active.
    // Reading the active sender never changes it (unlike spoutVK receivers).
    if (!_impl->names.GetActiveSender(name)) {
      return POLL_NO_SENDER;
    }
  } else {
    CharString utf8 = p_name.utf8();
    if (utf8.length() >= 255) {
      return POLL_NO_SENDER;
    }
    strcpy_s(name, sizeof(name), utf8.get_data());
  }

  SharedTextureInfo shared = {};
  if (!_impl->names.getSharedInfo(name, &shared)) {
    return POLL_NO_SENDER;
  }
  if (shared.shareHandle == 0) {
    return POLL_CPU_SENDER;
  }

  SpoutSenderInfo info;
  info.name = String::utf8(name);
  // The handle is stored as 32 bits and sign-extended back, exactly like
  // spoutDX::ReceiveSenderData does (LongToHandle). Legacy shared handles fit
  // in 32 bits by design so 32- and 64-bit processes can share them.
  info.share_handle = reinterpret_cast<uint64_t>(
      LongToHandle(static_cast<long>(shared.shareHandle)));
  info.width = shared.width;
  info.height = shared.height;
  info.dxgi_format = shared.format;

  if (info.width == 0 || info.height == 0) {
    return POLL_NO_SENDER;
  }

  const bool same_sender = _connected && info.name == _info.name;
  if (same_sender && info.same_texture(_info)) {
    r_info = _info;
    return POLL_SAME;
  }
  if (!same_sender) {
    open_sync_objects(info.name);
  }
  _info = info;
  _connected = true;
  r_info = _info;
  return POLL_CHANGED;
}

void SpoutShareReceiver::open_sync_objects(const String &p_name) {
  _impl->frame.CleanupFrameCount();
  if (_impl->mutex) {
    CloseHandle(_impl->mutex);
    _impl->mutex = nullptr;
  }
  CharString utf8 = p_name.utf8();
  // Open, don't create: a receiver must not own the sender's mutex object.
  _impl->mutex = OpenMutexA(SYNCHRONIZE | MUTEX_MODIFY_STATE, FALSE,
                            access_mutex_name(utf8.get_data()).c_str());
  _impl->frame.EnableFrameCount(utf8.get_data());
}

void SpoutShareReceiver::disconnect() {
  if (!_connected) {
    return;
  }
  // Note: no Sleep() here. spoutDX::ReleaseReceiver waits 67 ms "in case the
  // same sender opens again"; we simply keep polling instead.
  _impl->frame.CleanupFrameCount();
  if (_impl->mutex) {
    CloseHandle(_impl->mutex);
    _impl->mutex = nullptr;
  }
  _info = SpoutSenderInfo();
  _connected = false;
}

bool SpoutShareReceiver::lock(uint32_t p_timeout_ms) {
  return wait_mutex(_impl->mutex, p_timeout_ms);
}

void SpoutShareReceiver::unlock() {
  if (_impl->mutex) {
    ReleaseMutex(_impl->mutex);
  }
}

bool SpoutShareReceiver::is_new_frame() { return _impl->frame.GetNewFrame(); }

double SpoutShareReceiver::get_sender_fps() const {
  return _impl->frame.GetSenderFps();
}

int64_t SpoutShareReceiver::get_sender_frame() const {
  return _impl->frame.GetSenderFrame();
}
