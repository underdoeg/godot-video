//
// Created by phwhitfield on 05.08.25.
//

#pragma once

#include "gav_stream.h"

#include <godot_cpp/classes/resource_format_loader.hpp>

class GAVLoader : public godot::ResourceFormatLoader {
	GDCLASS(GAVLoader, ResourceFormatLoader)
	static void _bind_methods();

public:
	godot::Variant _load(const godot::String &p_path, const godot::String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const override;
	godot::PackedStringArray _get_recognized_extensions() const override;
	godot::String _get_resource_type(const godot::String &p_extension) const override;
	bool _handles_type(const godot::StringName &p_type) const override;
};
