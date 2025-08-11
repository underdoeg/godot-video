#pragma once
#include "gav_texture.h"

#include <deque>
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/texture2drd.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <thread>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class GAVPlayback : public godot::VideoStreamPlayback {
	GDCLASS(GAVPlayback, VideoStreamPlayback)
	static void _bind_methods();

	using Clock = std::chrono::high_resolution_clock;

	~GAVPlayback() {
		_stop();
		av_packet_unref(pkt);
		if (thread.joinable()) {
			thread.join();
		}
	}

	AVFormatContext *fmt_ctx = nullptr;
	// AVCodecContext *dec_ctx = nullptr;
	AVCodecContext *video_codec_ctx = nullptr;
	AVPacket *pkt = av_packet_alloc();

	int video_stream_index = -1;
	int audio_stream_index = -1;
	godot::RenderingDevice *rd;

	bool video_ctx_ready = false;

	GAVTexture texture;

	enum State {
		STOPPED,
		PLAYING,
		PAUSED
	};

	State state = State::STOPPED;

	bool init_video();
	bool has_video() { return video_stream_index >= 0; }
	bool init_audio();
	bool has_audio() { return audio_stream_index >= 0; }

	std::thread thread;
	void thread_func();

	bool decode_is_done = false;

	void decode_next_frame();

	bool decode_video_frame(AVPacket *pkt);

	std::atomic_bool request_stop = false;
	int max_frame_buffer_size = 16;

	struct Frame {
		AVFrame *frame = nullptr;
		Clock::time_point timestamp;
	};

	std::deque<Frame> frame_buffer;

	void set_state(State state);
	Clock::time_point start_time;
	bool waiting_for_start_time = false;
	Clock::time_point frame_time(AVFrame *frame);

public:
	void _stop() override;
	void _play() override;
	bool _is_playing() const override;
	void _set_paused(bool p_paused) override;
	bool _is_paused() const override;
	double _get_length() const override;
	double _get_playback_position() const override;
	void _seek(double p_time) override;
	void _set_audio_track(int32_t p_idx) override;
	godot::Ref<godot::Texture2D> _get_texture() const override;
	void _update(double p_delta) override;
	int32_t _get_channels() const override;
	int32_t _get_mix_rate() const override;

	bool load(const godot::String &file_path);
};