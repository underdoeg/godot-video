#include "gav_playback.h"
#include "gav_settings.h"

#include <condition_variable>
#include <filesystem>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

void GAVPlayback::_bind_methods() {
}

GAVPlayback::GAVPlayback() {
	if (gav_settings::verbose_logging()) {
		log.set_level(GAVLog::VERBOSE);
	}
	AvPlayer::codecs.set_reusing_enabled(gav_settings::reuse_decoders());
}
GAVPlayback::~GAVPlayback() {
	if (av) {
		log.info("destruct");
		av->shutdown();
	}
	thread_keep_running = false;
	if (thread.joinable()) {
		thread.join();
	}
}

bool GAVPlayback::load(const String &p_path) {
	thread_keep_running = false;
	if (thread.joinable()) {
		thread.join();
	}

	keep_processing = true;

	auto path_global = ProjectSettings::get_singleton()->globalize_path(p_path);
	auto splits = path_global.split("/");
	log.set_name(splits[splits.size() - 1]);

	if (Engine::get_singleton()->is_editor_hint()) {
		auto thumb_path = av_thumbnail_path(path_global.ascii().ptr());
		auto img = Image::load_from_file(thumb_path.c_str());
		// img->load_from_file(thumb_path.c_str());
		thumb_texture = ImageTexture::create_from_image(img);
		return false;
	}

	// create av playback
	av = std::make_shared<AvPlayer>(AvWrapperLog{
			[&](const char *m) { log.verbose(m); },
			[&](const char *m) { log.info(m); },
			[&](const char *m) { log.warn(m); },
			[&](const char *m) { log.error(m); },
	});

	AvPlayerLoadSettings settings;
	settings.file_path = path_global.ascii().ptr();
	settings.output.frame_buffer_size = gav_settings::frame_buffer_size();

	settings.events.end = [&]() {
		log.verbose("end callback");
		video_finished = true;
	};

	bool result = false;
	if (threaded) {
		std::mutex mtx;
		std::condition_variable cv;
		std::optional<AvFileInfo> info_from_thread;
		settings.events.video_frame = [&](const AvVideoFrame &frame) {
			auto copy = frame.copy();
			std::scoped_lock lock(video_mutex);
			video_frame_thread = copy;
		};

		settings.events.audio_frame = [&](const AvAudioFrame &frame) {
			auto copy = frame.copy();
			std::scoped_lock lock(audio_mutex);
			audio_frames_thread.push_back(copy);
		};

		settings.events.file_info = [&](const auto &info) {
			std::unique_lock<std::mutex> lck(mtx);
			info_from_thread = info;
			cv.notify_all();
		};

		thread_keep_running = true;
		thread = std::thread([&]() {
			auto res = av->load(settings);
			if (!res) {
				cv.notify_all();
			}
			while (thread_keep_running) {
				auto sleep_until = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(1000 / 120);
				av->process();
				std::this_thread::sleep_until(sleep_until);
			}
		});

		std::unique_lock lck(mtx);
		cv.wait(lck);
		if (info_from_thread) {
			set_file_info(info_from_thread.value());
			result = true;
		} else {
			result = false;
		}
	} else {
		settings.events.video_frame = std::bind(&GAVPlayback::on_video_frame, this, std::placeholders::_1);
		settings.events.audio_frame = std::bind(&GAVPlayback::on_audio_frame, this, std::placeholders::_1);
		settings.events.file_info = std::bind(&GAVPlayback::set_file_info, this, std::placeholders::_1);

		result = av->load(settings);
	}

	if (!result) {
		log.error("Failed to load {}", path_global.ptr());
	}
	return result;
}

void GAVPlayback::set_file_info(const AvFileInfo &info) {
	if (!texture) {
		texture = std::make_shared<GAVTexture>(RenderingServer::get_singleton()->get_rendering_device());
	}
	texture->log.set_level(log.get_level());
	texture->log.set_name(log.get_name() + String(" - texture"));
	texture->setup(info.video);
}

void GAVPlayback::on_video_frame(const AvVideoFrame &frame) const {
	MEASURE;
	// log.info("video");
	if (!texture) {
		log.error("received video frame before texture was created. this should not happen.");
		return;
	}
	texture->update(frame);
}
void GAVPlayback::on_audio_frame(const AvAudioFrame &frame) {
	audio_buffer.resize(frame.byte_size / sizeof(float));
	memcpy(audio_buffer.ptrw(), frame.frame->data[0], frame.byte_size);
	// log.info("audio: {}", frame.byte_size );
	mix_audio(frame.frame->nb_samples, audio_buffer, 0);
}

void GAVPlayback::_stop() {
	if (!av)
		return;

	// if (threaded) {
	// 	thread_keep_running = false;
	// 	if (thread.joinable()) {
	// 		thread.join();
	// 	}
	// }
	av->stop();
}
void GAVPlayback::_play() {
	if (!av)
		return;
	keep_processing = true;
	av->play();
}

void GAVPlayback::_set_paused(bool p_paused) {
	if (!av)
		return;
	keep_processing = true;
	av->set_paused(p_paused);
}

bool GAVPlayback::_is_paused() const {
	if (!av)
		return false;
	return av->is_paused();
}

bool GAVPlayback::_is_playing() const {
	// godot video player is a bit weird here imho. is playing should always be true (except on stop), otherwise _update is not being called
	// if (!av)
	// 	return false;
	// return av->is_playing();
	return keep_processing;
}

double GAVPlayback::_get_length() const {
	if (!av) {
		return 0;
	}
	return av->duration_seconds();
}

double GAVPlayback::_get_playback_position() const {
	return 0.0;
}

void GAVPlayback::_seek(double p_time) {
}
void GAVPlayback::_set_audio_track(int32_t p_idx) {
}

Ref<Texture2D> GAVPlayback::_get_texture() const {
	if (Engine::get_singleton()->is_editor_hint()) {
		return thumb_texture;
	}
	if (!texture) {
		log.error("_get_texture called before GAVTexture was created. This should not happen");
		return nullptr;
	}
	return texture->get_texture();
}

int32_t GAVPlayback::_get_channels() const {
	return VideoStreamPlayback::_get_channels();
}

int32_t GAVPlayback::_get_mix_rate() const {
	return VideoStreamPlayback::_get_mix_rate();
}

void GAVPlayback::_update(double p_delta) {
	MEASURE_N("MAIN");
	if (video_finished) {
		callbacks.ended();
		video_finished = false;
		keep_processing = false;
	}

	if (threaded) {
		std::optional<AvVideoFrame> frame;
		{
			std::scoped_lock lock(video_mutex);
			if (video_frame_thread) {
				frame = video_frame_thread.value();
				video_frame_thread.reset();
			}
		}
		if (frame) {
			on_video_frame(frame.value());
		}
		{
			// TODO: audio
		}
	} else {
		if (!av) {
			return;
		}
		av->process();
	}
}
