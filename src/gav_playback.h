#pragma once
#include "gav_texture.h"
#include "packet_decoder.h"

#include <deque>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <map>
#include <optional>
#include <queue>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libswresample/swresample.h>
}

#ifndef GODOT_VULKAN_PATCHED
#define GODOT_VULKAN_PATCHED false
#endif
// #define GODOT_VULKAN_PATCHED false

enum VideoFrameType {
	SW,
	HW,
	VK,
	BUFF
};

struct ActiveVideoFrame {
	AVFramePtr frame;
	std::shared_ptr<GAVTexture::Buffers> buffer = {};
	VideoFrameType type;
	AVPixelFormat pixel_format = AV_PIX_FMT_NONE;
};

class GAVPlayback : public godot::VideoStreamPlayback {
	struct VideoInfo {
		double duration;
		bool has_audio;
	};

	GDCLASS(GAVPlayback, VideoStreamPlayback)
	static void _bind_methods();

	using Clock = std::chrono::high_resolution_clock;
	~GAVPlayback() override;

	AVFormatContext *fmt_ctx = nullptr;
	// AVCodecContext *dec_ctx = nullptr;
	AVCodecContext *video_codec_ctx = nullptr;
	AVCodecContext *audio_codec_ctx = nullptr;
	AVHWDeviceType hw_device_type = AV_HWDEVICE_TYPE_NONE;

	AVPacket *pkt = av_packet_alloc();

	int video_stream_index = -1;
	int audio_stream_index = -1;

	int audio_num_channels = 0;
	int audio_sample_rate = 0;

	static godot::RenderingDevice *decode_rd;
	static godot::RenderingDevice *conversion_rd;

	// set to true if there are too many open videos  at once, it will retry to ini every frame
	bool waiting_for_init = false;

	bool video_ctx_ready = false;
	bool audio_ctx_ready = false;

	godot::RID tex_rid;
	mutable godot::Ref<godot::Texture2DRD> texture_public;

	// TODO: maybe reuse existing texture
	std::shared_ptr<GAVTexture> texture;

	VideoInfo video_info;

	enum State {
		STOPPED,
		PLAYING,
		PAUSED
	};

	std::atomic<State> state = State::STOPPED;

	godot::String filename;
	godot::String file_path_requested;
	godot::String file_path_loaded;

	bool request_init();
	bool init();
	bool init_video();
	[[nodiscard]] bool has_video() const { return video_stream_index != AVERROR_STREAM_NOT_FOUND && video_stream_index >= 0; }
	bool init_audio();
	[[nodiscard]] bool has_audio() const { return audio_stream_index != AVERROR_STREAM_NOT_FOUND && audio_stream_index >= 0; }

	// std::thread thread;
	// void thread_func();
	AVHWDeviceType hw_preferred = AV_HWDEVICE_TYPE_NONE; //AV_HWDEVICE_TYPE_VAAPI //AV_HWDEVICE_TYPE_VDPAU; //AV_HWDEVICE_TYPE_VULKAN; //AV_HWDEVICE_TYPE_NONE; // A
	const AVCodecHWConfig *accel_config = nullptr;

	// if set this frame will be sent to the renderer
	std::optional<ActiveVideoFrame> video_frame_to_show;
	std::optional<ActiveVideoFrame> video_frame_to_show_thread;
	std::atomic_int64_t progress_millis;

	// audio resampling
	SwrContext *audio_resampler = nullptr;
	// AVFramePtr audio_frame;

	std::atomic_bool decode_is_done = false;
	bool read_next_packet();
	void read_packets(int amount = 20);

	bool show_active_video_frame();

	std::atomic_bool request_stop = false;
	int max_frame_buffer_size = 4;

	void set_state(State state);
	Clock::time_point start_time;
	bool waiting_for_start_time = false;
	Clock::time_point frame_time(const AVFramePtr &frame, AVRational time_base);

	// frame handlers for specific stream indices (usually two, one video, one audio)
	std::map<int, PacketDecoder> frame_handlers;

	void cleanup(bool with_format_ctx);

	std::thread decoder_thread;
	bool decoder_threaded = false;
	void decoder_threaded_func();
	std::mutex decoder_mtx;

	std::mutex audio_mtx;
	std::queue<AVFramePtr> audio_frames;
	godot::PackedFloat32Array audio_buff;
	void output_audio_frames();

public:
	GAVPlayback();

	void _stop() override;
	void _play() override;
	[[nodiscard]] bool _is_playing() const override;
	void _set_paused(bool p_paused) override;
	[[nodiscard]] bool _is_paused() const override;
	[[nodiscard]] double _get_length() const override;
	[[nodiscard]] double _get_playback_position() const override;
	void _seek(double p_time) override;
	void _set_audio_track(int32_t p_idx) override;
	[[nodiscard]] godot::Ref<godot::Texture2D> _get_texture() const override;
	void _update(double p_delta) override;
	[[nodiscard]] int32_t _get_channels() const override;
	[[nodiscard]] int32_t _get_mix_rate() const override;

	bool load(const godot::String &file_path);

	bool do_loop = false;
	std::function<void()> finished_callback;
};