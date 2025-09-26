//
// Created by phwhitfield on 26.09.25.
//

#include "gav_settings.h"

#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/core/property_info.hpp"

using namespace godot;

namespace gav_settings {

auto prefix = "gav_player";
auto threads = "use_threads";
auto verbose = "verbose_logging";
auto k_frame_buffer_size = "k_frame_buffer_size";

inline godot::String p(const String &str) {
	return prefix + String("/") + str;
}

void init() {
	auto ps = ProjectSettings::get_singleton();

	{
		const auto key = p(threads);
		if (!ps->has_setting(key)) {
			ps->set_setting(key, false);
		}
		ps->set_initial_value(key, false);
		Dictionary info;
		info.set("name", key);
		info.set("type", Variant::BOOL);
		ProjectSettings::get_singleton()->add_property_info(info);
		ps->set_as_basic(key, true);
	}

	{
		const auto key = p(verbose);
		if (!ps->has_setting(key)) {
			ps->set_setting(key, false);
		}
		ps->set_initial_value(key, false);
		Dictionary info;
		info.set("name", key);
		info.set("type", Variant::BOOL);
		ProjectSettings::get_singleton()->add_property_info(info);
		ps->set_as_basic(key, true);
	}

	{
		const auto key = p(k_frame_buffer_size);
		if (!ps->has_setting(key)) {
			ps->set_setting(key, 10);
		}
		ps->set_initial_value(key, 10);
		Dictionary info;
		info.set("name", key);
		info.set("type", Variant::INT);
		ProjectSettings::get_singleton()->add_property_info(info);
		ps->set_as_basic(key, true);
	}
}
bool use_threads() {
	return ProjectSettings::get_singleton()->get_setting(p(threads), false);
}
bool verbose_logging() {
	return ProjectSettings::get_singleton()->get_setting(p(verbose), false);
}
int frame_buffer_size() {
	return ProjectSettings::get_singleton()->get_setting(p(k_frame_buffer_size), 10);
}

} //namespace gav_settings