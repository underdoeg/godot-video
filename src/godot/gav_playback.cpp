#include "gav_playback.h"
#include "gav_settings.h"

#include <filesystem>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/file_access.hpp>
#include <godot_cpp/classes/project_settings.hpp>

using namespace godot;

void GAVPlayback::_bind_methods() {
}

GAVPlayback::GAVPlayback() {
	if (gav_settings::verbose_logging()) {
		log.set_level(GAVLog::VERBOSE);
	}
}

bool GAVPlayback::load(const String &p_path) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}

	auto path_global = ProjectSettings::get_singleton()->globalize_path(p_path);
	auto splits = path_global.split("/");
	log.set_name(splits[splits.size() - 1]);

	// create av playback
	av = std::make_shared<AvPlayer>(AvWrapperLog{
			[&](const char *m) { log.verbose(m); },
			[&](const char *m) { log.info(m); },
			[&](const char *m) { log.warn(m); },
			[&](const char *m) { log.error(m); },
	});

	AvPlayerLoadSettings settings;
	settings.events.video_frame = std::bind(&GAVPlayback::on_video_frame, this, std::placeholders::_1);
	settings.events.audio_frame = std::bind(&GAVPlayback::on_audio_frame, this, std::placeholders::_1);

	const auto res = av->load(path_global.ptr(), settings);
	if (!res) {
		log.error("Failed to load {}", path_global.ptr());
	}
	return res;
}

void GAVPlayback::on_video_frame(const AvVideoFrame &frame) {
	log.info("video");
}
void GAVPlayback::on_audio_frame(const AvAudioFrame &frame) {
	audio_buffer.resize(frame.byte_size / sizeof(float));
	memcpy(audio_buffer.ptrw(), frame.frame->data[0], frame.byte_size);
	mix_audio(frame.frame->nb_samples, audio_buffer, 0);
}

void GAVPlayback::_stop() {
	if (!av)
		return;
	// av->stop();
}
void GAVPlayback::_play() {
	if (!av)
		return;
	av->play();
}

void GAVPlayback::_set_paused(bool p_paused) {
}

bool GAVPlayback::_is_paused() const {
	if (!av)
		return false;
	return av->is_paused();
}

bool GAVPlayback::_is_playing() const {
	if (!av)
		return false;
	return av->is_playing();
}

double GAVPlayback::_get_length() const {
	return 0.0;
}

double GAVPlayback::_get_playback_position() const {
	return 0.0;
}

void GAVPlayback::_seek(double p_time) {
}
void GAVPlayback::_set_audio_track(int32_t p_idx) {
}

Ref<Texture2D> GAVPlayback::_get_texture() const {
	return VideoStreamPlayback::_get_texture();
}

int32_t GAVPlayback::_get_channels() const {
	return VideoStreamPlayback::_get_channels();
}

int32_t GAVPlayback::_get_mix_rate() const {
	return VideoStreamPlayback::_get_mix_rate();
}

void GAVPlayback::_update(double p_delta) {
	if (!av)
		return;
	av->process();
}