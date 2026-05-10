#pragma once

#include <godot_cpp/variant/string.hpp>

namespace godot {

class SpoutBackend;

SpoutBackend *spout_backend_create(const String &p_driver_name,
                                   const char *p_owner_name);

} // namespace godot
