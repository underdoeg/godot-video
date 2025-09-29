//
// Created by phwhitfield on 26.09.25.
//

#include "gav_settings.h"

#include "godot_cpp/classes/project_settings.hpp"
#include "godot_cpp/core/property_info.hpp"

using namespace godot;

namespace gav_settings {

auto k_prefix = "gav_player";
auto k_threads = "use_threads";
auto k_verbose = "verbose_logging";
auto k_frame_buffer_size = "frame_buffer_size";
auto k_reuse_decoders = "reuse_decoders";

inline godot::String p(const String &str) {
	return k_prefix + String("/") + str;
}

void init() {
	auto ps = ProjectSettings::get_singleton();

	{
		const auto key = p(k_threads);
		if (!ps->has_setting(key)) {
			ps->set_setting(key, true);
		}
		ps->set_initial_value(key, true);
		Dictionary info;
		info.set("name", key);
		info.set("type", Variant::BOOL);
		ProjectSettings::get_singleton()->add_property_info(info);
		ps->set_as_basic(key, true);
	}

	{
		const auto key = p(k_verbose);
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

	{
		const auto key = p(k_reuse_decoders);
		if (!ps->has_setting(key)) {
			ps->set_setting(key, true);
		}
		ps->set_initial_value(key, true);
		Dictionary info;
		info.set("name", key);
		info.set("type", Variant::BOOL);
		PropertyInfo pino;
		ProjectSettings::get_singleton()->add_property_info(info);
		ps->set_as_basic(key, true);
	}
}
bool use_threads() {
	return ProjectSettings::get_singleton()->get_setting(p(k_threads), false);
}
bool verbose_logging() {
	return ProjectSettings::get_singleton()->get_setting(p(k_verbose), false);
}
int frame_buffer_size() {
	return ProjectSettings::get_singleton()->get_setting(p(k_frame_buffer_size), 10);
}

bool reuse_decoders() {
	return ProjectSettings::get_singleton()->get_setting(p(k_reuse_decoders), true);
}

} //namespace gav_settings