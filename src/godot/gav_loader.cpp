//
// Created by phwhitfield on 05.08.25.
//

#include "gav_loader.h"

#include <filesystem>

using namespace godot;

void GAVLoader::_bind_methods() {
}
godot::Variant GAVLoader::_load(const godot::String &p_path, const godot::String &p_original_path, bool p_use_sub_threads, int32_t p_cache_mode) const {
	// UtilityFunctions::print("GAVLoader::_load ", p_path);
	auto stream = memnew(GAVStream);
	stream->set_file(p_path);
	return stream;
}

PackedStringArray GAVLoader::_get_recognized_extensions() const {
	static const PackedStringArray extensions = {
		"mkv", "avi", "gif", "mp4", "mov"
	};
	return extensions;
}
String GAVLoader::_get_resource_type(const String &p_extension) const {
	return "GAVStream";
}

bool GAVLoader::_handles_type(const StringName &p_type) const {
	if (p_type.nocasecmp_to("VideoStream") == 0) {
		return true;
	}
	// This should work also, though?
	return ClassDB::is_parent_class(p_type, _get_resource_type(""));
}
