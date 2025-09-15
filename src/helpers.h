//
// Created by phwhitfield on 05.08.25.
//

#pragma once

#include <memory>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/frame.h>
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

using AVFramePtr = std::shared_ptr<AVFrame>;

inline AVFramePtr av_frame_ptr() {
	return std::shared_ptr<AVFrame>(av_frame_alloc(), [](auto f) {
		// godot::UtilityFunctions::print("free av frame");
		av_frame_free(&f);
	});
}
