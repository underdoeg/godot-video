#pragma once
#include "av_wrapper/av_player.h"
#include "gav_log.h"
#include "gav_settings.h"
#include "gav_texture.h"

#include <functional>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/texture2d.hpp>
#include <godot_cpp/classes/video_stream_playback.hpp>
#include <godot_cpp/variant/string.hpp>
#include <memory>
#include <thread>

class GAVPlayback : public godot::VideoStreamPlayback {
	struct Callbacks {
		std::function<void()> ended;
		std::function<void()> failed;
		std::function<void()> first_frame;
	};

	GDCLASS(GAVPlayback, VideoStreamPlayback)
	static void _bind_methods();


	GAVLog log = GAVLog("GAVPlayback");
	std::shared_ptr<AvPlayer> av;
	std::shared_ptr<GAVTexture> texture;

	bool threaded = gav_settings::use_threads();
	std::thread thread;
	std::atomic_bool thread_keep_running;
	std::atomic_bool video_finished;
	std::mutex audio_mutex;
	std::mutex video_mutex;
	std::optional<AvVideoFrame> video_frame_thread;
	std::deque<AvAudioFrame> audio_frames_thread;
	godot::Ref<godot::ImageTexture> thumb_texture;

	godot::PackedFloat32Array audio_buffer;

	void on_video_frame(const AvVideoFrame &frame) const;
	void on_audio_frame(const AvAudioFrame &frame);
	void set_file_info(const AvFileInfo &info);

public:
	GAVPlayback();
	~GAVPlayback() override;

	Callbacks callbacks;

	bool load(const godot::String &p_path);

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
};
