//
// Created by phwhitfield on 05.08.25.
//

#pragma once

extern "C" {
#include <libavutil/error.h>
}

#include <godot_cpp/variant/utility_functions.hpp>

inline bool ff_ok(int res) {
	if (res < 0) {
		char error_str[1024];
		av_make_error_string(error_str, sizeof(error_str), res);
		godot::UtilityFunctions::printerr("ffmpeg error: ", error_str);
		return false;
	}
	return true;
}