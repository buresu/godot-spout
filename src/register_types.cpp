#include "register_types.hpp"

#include <godot_cpp/core/class_db.hpp>

#include "SpoutCommon.hpp"
#include "SpoutCore.hpp"
#include "SpoutInput.hpp"
#include "SpoutOutput.hpp"
#include "SpoutTexture.hpp"

void initialize_spout_module(ModuleInitializationLevel p_level) {

  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  spout_diagnostics_init();

  ClassDB::register_class<SpoutOutput>();
  ClassDB::register_class<SpoutInput>();
  ClassDB::register_class<SpoutTexture>();
}

void uninitialize_spout_module(ModuleInitializationLevel p_level) {

  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  // Release whatever is still alive (e.g. SpoutTexture resources held until
  // exit) while the RenderingServer still exists: unregisters the senders and
  // frees the shared D3D11 devices before Godot destroys its GPU device.
  if (spout_rendering_available()) {
    SpoutCoreBase::release_all();
    spout_deferred_release_shutdown();
  }
}

extern "C" {

GDExtensionBool GDE_EXPORT
spout_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                   const GDExtensionClassLibraryPtr p_library,
                   GDExtensionInitialization *r_initialization) {

  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                                 r_initialization);

  init_obj.register_initializer(initialize_spout_module);
  init_obj.register_terminator(uninitialize_spout_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);

  return init_obj.init();
}
}
