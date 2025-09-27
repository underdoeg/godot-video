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
	godot::String prefix;
	godot::String prefix_info;
	Level level = INFO;

public:
	explicit GAVLog(const godot::String &name, Level level = INFO) {
		set_name(name);
		set_level(level);
	}

	void set_name(const godot::String &p_name) {
		name = p_name;
		prefix = "[" + name + "] ";
	}

	const godot::String &get_name() const {
		return name;
	}

	void set_level(Level p_level) {
		this->level = p_level;
	}

	Level get_level() const {
		return level;
	}

	template <typename... Args>
	void verbose(const godot::Variant &p_arg1, const Args &...p_args) const {
		if (level != VERBOSE)
			return;
		godot::UtilityFunctions::print(prefix, p_arg1, p_args...);
	}
	template <typename... Args>
	void info(const godot::Variant &p_arg1, const Args &...p_args) const {
		if (level > INFO)
			return;
		godot::UtilityFunctions::print(prefix, p_arg1, p_args...);
	}
	template <typename... Args>
	void warn(const godot::Variant &p_arg1, const Args &...p_args) const {
		if (level > WARNING)
			return;
		godot::UtilityFunctions::print(prefix, p_arg1, p_args...);
	}
	template <typename... Args>
	void error(const godot::Variant &p_arg1, const Args &...p_args) const {
		godot::UtilityFunctions::printerr(prefix, p_arg1, p_args...);
	}
};