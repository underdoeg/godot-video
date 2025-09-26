#pragma once

#include "av_helpers.h"

#include <atomic>
#include <deque>
#include <filesystem>
#include <format>
#include <functional>
#include <iostream>
#include <utility>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/pixfmt.h>
#include <libswresample/swresample.h>
}

struct AvVideoInfo {
	AVPixelFormat pixel_format;
	int width;
	int height;
	double frame_rate;
	std::string codec_name;
	// std::string codec;
};

struct AvAudioInfo {
	AVSampleFormat sample_format;
	int num_channels;
	int sample_rate;
	std::string codec_name;
};

struct AvFileInfo {
	AvVideoInfo video{};
	AvAudioInfo audio{};
};

struct AvWrapperLog {
	std::function<void(const char *)> _verbose = [](const char *st) { std::cout << st << std::endl; };
	std::function<void(const char *)> _info = [](const char *st) { std::cout << st << std::endl; };
	std::function<void(const char *)> _warn = [](const char *st) { std::cout << st << std::endl; };
	std::function<void(const char *)> _error = [](const char *st) { std::cerr << st << std::endl; };

	template <typename... Args>
	void verbose(const std::format_string<Args...> str, Args &&...args) const {
		_verbose(std::format(str, std::forward<Args>(args)...).c_str());
	}

	template <typename... Args>
	void info(const std::format_string<Args...> str, Args &&...args) const {
		_info(std::format(str, std::forward<Args>(args)...).c_str());
	}

	template <typename... Args>
	void warn(const std::format_string<Args...> str, Args &&...args) const {
		_warn(std::format(str, std::forward<Args>(args)...).c_str());
	}

	template <typename... Args>
	void error(const std::format_string<Args...> str, Args &&...args) const {
		_error(std::format(str, std::forward<Args>(args)...).c_str());
	}
};

enum AvVideoFrameType {
	VK_BUFFER,
	SW_BUFFER,
};

struct AvVideoFrame {
	AvFramePtr frame;
	std::chrono::milliseconds millis;
	AvVideoFrameType type;
};

struct AvAudioFrame {
	AvFramePtr frame;
	int byte_size = 0;
	std::chrono::milliseconds millis;
};

struct AvWrapperOutputSettings {
	bool video_hw_enabled = true;
	AVHWDeviceType video_hw_type = AV_HWDEVICE_TYPE_NONE;
	int frame_buffer_size = 10;
	AVSampleFormat audio_sample_fmt = AV_SAMPLE_FMT_FLT;
	int audio_sample_rate = 0;
};

struct AvPlayerEvents {
	std::function<void(const AvVideoFrame &)> video_frame;
	std::function<void(const AvAudioFrame &)> audio_frame;
};

struct AvPlayerLoadSettings {
	AvWrapperOutputSettings output;
	AvPlayerEvents events;
};

class AvPlayer {
	using Clock = std::chrono::high_resolution_clock;

	[[nodiscard]] bool ff_ok(int result, const std::string &prepend = "") const;

	AVFormatContext *fmt_ctx = nullptr;
	std::optional<int> video_stream_index, audio_stream_index;

	AVStream *video_stream = nullptr;
	AVStream *audio_stream = nullptr;

	AVCodecContext *video_codec = nullptr;
	AVCodecContext *audio_codec = nullptr;

	AVBufferRef *video_hw_frames_ref = nullptr;

	SwrContext *audio_resampler = nullptr;

	std::optional<std::filesystem::path> filepath_loaded;

	AvFileInfo file_info;
	AvPlayerEvents events;

	AvWrapperOutputSettings output_settings, output_settings_requested;

	std::deque<AvVideoFrame> video_frames;
	std::deque<AvAudioFrame> audio_frames;

	void reset();
	bool init_video();
	bool init_audio();

	void read_next_frame();
	void frame_received(const AvFramePtr &frame, int stream_index);
	void audio_frame_received(const AvFramePtr &frame);
	void video_frame_received(const AvFramePtr &frame);

	// template <typename FrameType>
	// void handle_frames(std::deque<FrameType> &frames, std::function<void(const FrameType& callback)>) {
	// 	const auto now = std::chrono::high_resolution_clock::now();
	// 	if (!has_started()) {
	// 		start_time = now;
	// 	}
	// 	std::optional<FrameType> result;
	// 	int frame_drops = -1;
	// 	while (!frames.empty()) {
	// 		if (start_time.value() + frames.front().millis <= now) {
	// 			result = frames.front();
	// 			frames.pop_front();
	// 			frame_drops++;
	// 		} else {
	// 			break;
	// 		}
	// 	}
	// 	if (frame_drops > 0) {
	// 		log.warn("dropped {} frames", frame_drops);
	// 	}
	// 	return result;
	// }

	void emit_video_frame(const AvVideoFrame &frame) const;

	void emit_audio_frame(const AvAudioFrame &frame) const;

	std::atomic_bool playing = false;
	std::atomic_bool paused = false;
	bool is_eof = false;

	std::atomic_bool ready_for_playback = false;
	std::optional<Clock::time_point> start_time = std::nullopt;

	bool has_started() const {
		return start_time.has_value();
	}

	size_t buffer_size() const {
		if (video_stream_index && audio_stream_index) {
			return std::min(video_frames.size(), audio_frames.size());
		}
		return video_frames.size();
	}

public:
	AvWrapperLog log;

	explicit AvPlayer(AvWrapperLog log = AvWrapperLog()) :
			log(std::move(log)) {};

	void play() {
		playing = true;
		paused = false;
	}
	bool is_playing() const {
		return playing && !paused;
	}
	bool is_paused() const {
		return playing && paused;
	};

	bool load(const std::filesystem::path &filepath, const AvPlayerLoadSettings &settings = {});

	void process();
};