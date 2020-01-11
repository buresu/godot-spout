#include <Godot.hpp>

#include "GDSpoutSender.hpp"
#include "GDSpoutTexture.hpp"

using namespace godot;

// GDNative Singleton
extern "C" void GDN_EXPORT godot_gdnative_singleton() {}

// GDNative Initialize
extern "C" void GDN_EXPORT godot_gdnative_init(godot_gdnative_init_options *o) {
  Godot::gdnative_init(o);
}

// GDNative Terminate
extern "C" void GDN_EXPORT
godot_gdnative_terminate(godot_gdnative_terminate_options *o) {
  Godot::gdnative_terminate(o);
}

// NativeScript Initialize
extern "C" void GDN_EXPORT godot_nativescript_init(void *handle) {
  Godot::nativescript_init(handle);
  register_class<GDSpoutTexture>();
  register_class<GDSpoutSender>();
}

// NativeScript Terminate
extern "C" void GDN_EXPORT godot_nativescript_terminate(void *handle) {
  Godot::nativescript_terminate(handle);
}
