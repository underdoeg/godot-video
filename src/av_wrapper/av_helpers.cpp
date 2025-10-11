//
// Created by phwhitfield on 26.09.25.
//

#include "av_helpers.h"

#include <libavutil/frame.h>

#include <filesystem>
#include <memory>

extern "C" {
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
}

std::string av_error_string(int error) {
	std::string ret;
	ret.resize(1024);
	av_strerror(error, ret.data(), ret.size());
	return ret;
}

bool check_error(const int code, const char *msg) {
	if (code < 0) {
		const auto error = av_error_string(code);
		printf("%s: %s\n", msg, error.c_str());
		return false;
	}
	return true;
};

AvFramePtr av_frame_ptr() {
	return { av_frame_alloc(), [](auto f) {
				av_frame_free(&f);
			} };
}
AvFramePtr av_frame_clone(AvFramePtr src, AvFramePtr dst) {
	if (!dst) {
		dst = av_frame_ptr();
	}else {
		av_frame_unref(dst.get());
	}
	dst->format = src->format;
	dst->width = src->width;
	dst->height = src->height;
	dst->ch_layout = src->ch_layout;
	dst->nb_samples = src->nb_samples;
	av_frame_get_buffer(dst.get(), 32);
	check_error(av_frame_copy_props(dst.get(), src.get()), "av_frame_copy_props");
	check_error(av_frame_copy(dst.get(), src.get()), "av_frame_copy");
	return dst;
}

AvPacketPtr av_packet_ptr() {
	return {
		av_packet_alloc(), [](auto f) {
			av_packet_unref(f);
			av_packet_free(&f);
		}
	};
}
AvCodecContextPtr avcodec_context_ptr(const AVCodec *decoder) {
	return { avcodec_alloc_context3(decoder), [](auto f) {
				avcodec_free_context(&f);
			} };
}

std::chrono::milliseconds av_get_frame_millis(const AvFramePtr &frame, const AVCodecContext* codec) {
	auto pts = static_cast<double>(frame->best_effort_timestamp);
	pts *= av_q2d(codec->time_base);
	auto seconds = std::chrono::duration<double>(pts);
	return std::chrono::duration_cast<std::chrono::milliseconds>(seconds);
}

std::chrono::milliseconds av_get_frame_millis_ptr(const AvFramePtr &frame, const AvCodecContextPtr codec) {
	return av_get_frame_millis(frame, codec.get());
}
std::string av_thumbnail_path(const std::string &video_path) {
	const auto tmp_path = std::filesystem::temp_directory_path() / std::filesystem::path(video_path).filename().replace_extension("jpg");
	printf("create thumbnail of %s at %s\n", video_path.c_str(), tmp_path.string().c_str());
	const std::string cmd = std::format("ffmpeg -i {} -vf \"thumbnail\" -frames:v 1 {} -y -loglevel error", video_path, tmp_path.string());
	std::system(cmd.c_str());
	return tmp_path.string();
}

AvPlaneInfos av_get_plane_infos(const AVPixelFormat &pixel_format, const int width, const int height) {
	std::array line_sizes{ 0, 0, 0, 0 };
	std::array<ptrdiff_t, 4> line_sizes_ptr;
	std::array<size_t, 4> plane_sizes{ 0, 0, 0, 0 };

	if (!check_error(av_image_fill_linesizes(line_sizes.data(), pixel_format, width), "av_image_fill_linesizes")) {
		return {};
	}

	for (int i = 0; i < 4; i++) {
		line_sizes_ptr[i] = line_sizes[i];
	}
	if (!check_error(av_image_fill_plane_sizes(plane_sizes.data(), pixel_format, height, line_sizes_ptr.data()), "av_image_fill_plane_sizes")) {
		return {};
	}

	// auto bpp = av_get_bits_per_pixel(pixel_format);

	AvPlaneInfos ret;
	for (int i = 0; i < 4; i++) {
		auto byte_size = plane_sizes[i];
		auto line_size = line_sizes[i];
		if (byte_size > 0) {
			int w = line_size;

			// TODO: there is probably some ffmpeg function handling the calculation
			if (
					pixel_format == AV_PIX_FMT_P016LE ||
					pixel_format == AV_PIX_FMT_P010LE) {
				w /= 2;
			}
			auto h = byte_size / line_size;
			ret.push_back({ w, static_cast<int>(h), 1, line_size, byte_size });
		}
	}

	return ret;
}
///////////////////////////////////////////////////////
TimeMeasure::~TimeMeasure() {
	const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - start_time);
	printf("[%s] %ldms\n", name.c_str(), (micros.count() / 1000));
}
