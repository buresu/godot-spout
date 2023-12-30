#include "register_types.hpp"

#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "GDSpoutSender.hpp"
#include "GDSpoutTexture.hpp"

using namespace godot;

void initialize_spout_module(ModuleInitializationLevel p_level) {

  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  ClassDB::register_class<GDSpoutSender>();
  ClassDB::register_class<GDSpoutTexture>();
}

void uninitialize_spout_module(ModuleInitializationLevel p_level) {

  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }
}

extern "C" {

GDExtensionBool GDE_EXPORT
spout_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                     const GDExtensionClassLibraryPtr p_library,
                     GDExtensionInitialization *r_initialization) {

  printf("spout_library_init\n");

  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                                 r_initialization);

  init_obj.register_initializer(initialize_spout_module);
  init_obj.register_terminator(uninitialize_spout_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);

  return init_obj.init();
}
}