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
	AVPixelFormat pixel_format = AV_PIX_FMT_NONE;
	int width = 0;
	int height = 0;
	double frame_rate = 0.0;
	std::string codec_name = "";
	// std::string codec;
};

struct AvAudioInfo {
	AVSampleFormat sample_format = AV_SAMPLE_FMT_NONE;
	int num_channels = 0;
	int sample_rate = 0;
	std::string codec_name = "";
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
	HW_BUFFER,
	SW_BUFFER,
};

struct AvBaseFrame {
	AvFramePtr frame;
	std::chrono::milliseconds millis;
};

struct AvVideoFrame : AvBaseFrame {
	AvVideoFrameType type;
	AVColorSpace color_space = AVCOL_SPC_UNSPECIFIED;
};

struct AvAudioFrame : AvBaseFrame {
	int byte_size = 0;
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
	std::function<void(const AvFileInfo &)> file_info;
	std::function<void()> end;
};

struct AvPlayerLoadSettings {
	std::string file_path;
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
	AvPacketPtr packet = av_packet_ptr();

	void reset();
	void fill_file_info();

	bool waiting_for_init = false;
	bool init();
	bool init_video();
	bool init_audio();

	void read_next_frames();
	void frame_received(const AvFramePtr &frame, int stream_index);
	void audio_frame_received(const AvFramePtr &frame);
	void video_frame_received(const AvFramePtr &frame);

	bool frame_needs_emit(const AvBaseFrame &f) const;
	void emit_video_frame(const AvVideoFrame &frame) const;
	void emit_audio_frame(const AvAudioFrame &frame) const;
	void emit_frames();

	std::atomic_bool playing = false;
	std::atomic_bool paused = false;
	bool is_eof = false;

	std::atomic_bool ready_for_playback = false;
	std::optional<Clock::time_point> start_time = std::nullopt;

	[[nodiscard]] bool has_started() const {
		return start_time.has_value();
	}

	[[nodiscard]] size_t buffer_size() const {
		if (video_stream_index && audio_stream_index) {
			return std::min(video_frames.size(), audio_frames.size());
		}
		return video_frames.size();
	}

public:
	AvWrapperLog log;

	explicit AvPlayer(AvWrapperLog log = AvWrapperLog()) :
			log(std::move(log)) {};

	~AvPlayer();

	void stop();
	void play() {
		playing = true;
		paused = false;
	}
	void set_paused(bool state) {
		paused = state;
	}
	[[nodiscard]] bool is_playing() const {
		return playing && !paused;
	}
	[[nodiscard]] bool is_paused() const {
		return playing && paused;
	};

	bool load(const AvPlayerLoadSettings &settings = {});

	void fill_buffers();

	void process();
};