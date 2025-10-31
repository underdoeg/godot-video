#pragma once

#include "godot_cpp/godot.hpp"
#include <godot_cpp/core/defs.hpp>

void initialize_gdextension_types(godot::ModuleInitializationLevel p_level = godot::MODULE_INITIALIZATION_LEVEL_SCENE);
void uninitialize_gdextension_types(godot::ModuleInitializationLevel p_level = godot::MODULE_INITIALIZATION_LEVEL_SCENE);
