#pragma once

#include "godot_cpp/variant/string.hpp"
#include "godot_cpp/variant/utility_functions.hpp"

class GAVLog {
public:
	enum Level {
		VERBOSE = 0,
		INFO = 1,
		WARNING = 2,
	};

private:
	godot::String name;
	godot::String prefix_info;
	Level level = INFO;

public:
	explicit GAVLog(const godot::String &name, Level level = INFO) {
		set_name(name);
		set_level(level);
	}

	void set_name(const godot::String &p_name) {
		this->name = "[" + p_name + "] ";
	}

	void set_level(Level p_level) {
		this->level = p_level;
	}

	template <typename... Args>
	void verbose(const godot::Variant &p_arg1, const Args &...p_args) {
		if (level != VERBOSE)
			return;
		godot::UtilityFunctions::print(name, p_arg1, p_args...);
	}
	template <typename... Args>
	void info(const godot::Variant &p_arg1, const Args &...p_args) {
		if (level > INFO)
			return;
		godot::UtilityFunctions::print(name, p_arg1, p_args...);
	}
	template <typename... Args>
	void warn(const godot::Variant &p_arg1, const Args &...p_args) {
		if (level > WARNING)
			return;
		godot::UtilityFunctions::print(name, p_arg1, p_args...);
	}
	template <typename... Args>
	void error(const godot::Variant &p_arg1, const Args &...p_args) {
		godot::UtilityFunctions::printerr(name, p_arg1, p_args...);
	}
};