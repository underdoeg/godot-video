#include "register_types.h"

#include "gav_loader.h"

#include <gdextension_interface.h>
#include <godot_cpp/classes/resource_loader.hpp>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "gav_settings.h"

using namespace godot;

static Ref<GAVLoader> gav_loader;

void initialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}

	UtilityFunctions::print("initialize godot av types");

	gav_settings::init();

	// GDREGISTER_CLASS(AvVideo);
	GDREGISTER_CLASS(GAVStream);
	GDREGISTER_CLASS(GAVLoader);
	GDREGISTER_CLASS(GAVPlayback);

	// Dictionary threaded_property;
	// threaded_property.set("name", "video_player/use_threads");
	// threaded_property.set("type", Variant::BOOL);
	// UtilityFunctions::print("------------------------------------------ ", threaded_property.has("name"));
	// threaded_property["value"] = false;

	// {"hint", PROPERTY_HINT_TYPE_STRING},
	// {"hint_string", "one,two,three"}
	// };

	gav_loader.instantiate();
	ResourceLoader::get_singleton()->add_resource_format_loader(gav_loader);
}

void uninitialize_gdextension_types(ModuleInitializationLevel p_level) {
	if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
		return;
	}
	ResourceLoader::get_singleton()->remove_resource_format_loader(gav_loader);
	gav_loader.unref();
}

extern "C" {
// Initialization
GDExtensionBool GDE_EXPORT godot_av_init(GDExtensionInterfaceGetProcAddress p_get_proc_address, GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
	GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);
	init_obj.register_initializer(initialize_gdextension_types);
	init_obj.register_terminator(uninitialize_gdextension_types);
	init_obj.set_minimum_library_initialization_level(MODULE_INITIALIZATION_LEVEL_SCENE);

	return init_obj.init();
}
}