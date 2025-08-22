#pragma once
#include "gav_texture.h"
#include "packet_decoder.h"

#include <deque>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <map>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

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

	AVPacket *pkt = av_packet_alloc();

	int video_stream_index = -1;
	int audio_stream_index = -1;
	static godot::RenderingDevice *decode_rd;
	static godot::RenderingDevice *conversion_rd;

	bool video_ctx_ready = false;
	bool audio_ctx_ready = false;

	GAVTexture texture;

	VideoInfo video_info;

	enum State {
		STOPPED,
		PLAYING,
		PAUSED
	};

	State state = State::STOPPED;

	bool init_video();
	[[nodiscard]] bool has_video() const { return video_stream_index >= 0; }
	bool init_audio();
	[[nodiscard]] bool has_audio() const { return audio_stream_index >= 0; }

	// std::thread thread;
	// void thread_func();

	// if set this frame will be sent to the renderer
	AVFramePtr video_frame_to_show;

	// audio resampling
	SwrContext *audio_resampler = nullptr;
	AVFramePtr audio_frame;

	bool decode_is_done = false;

	void read_next_packet();

	std::atomic_bool request_stop = false;
	int max_frame_buffer_size = 4;

	struct Frame {
		AVFrame *frame = nullptr;
		Clock::time_point timestamp;
	};

	void set_state(State state);
	Clock::time_point start_time;
	bool waiting_for_start_time = false;
	Clock::time_point frame_time(const AVFramePtr &frame);

	// frame handlers for specific stream indices (usually two, one video, one audio)
	std::map<int, PacketDecoder> frame_handlers;

public:
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
};