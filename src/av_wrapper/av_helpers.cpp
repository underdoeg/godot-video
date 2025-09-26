//
// Created by phwhitfield on 26.09.25.
//

#include "av_helpers.h"

#include <libavutil/frame.h>

#include <memory>

extern "C" {
#include <libavutil/error.h>
}

std::string av_error_string(int error) {
	std::string ret;
	ret.resize(1024);
	av_strerror(error, ret.data(), ret.size());
	return ret;
}

AvFramePtr av_frame_ptr() {
	return { av_frame_alloc(), [](auto f) {
				av_frame_free(&f);
			} };
}
AvPacketPtr av_packet_ptr() {
	return {
		av_packet_alloc(), [](auto f) {
			av_packet_unref(f);
			av_packet_free(&f);
		}
	};
}

std::chrono::milliseconds get_frame_millis(const AvFramePtr &frame, const AVCodecContext *codec) {
	auto pts = static_cast<double>(frame->best_effort_timestamp);
	pts *= av_q2d(codec->time_base);
	auto seconds = std::chrono::duration<double>(pts);
	return std::chrono::duration_cast<std::chrono::milliseconds>(seconds);
}
