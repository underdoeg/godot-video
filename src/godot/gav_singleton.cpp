//
// Created by philip on 10/2/25.
//

#include "gav_singleton.h"

#include <godot_cpp/classes/engine.hpp>

#include "av_wrapper/av_player.h"
#include "gav_settings.h"
#include "shaders.h"
GAVSingleton::GAVSingleton() {
	if (godot::Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	if (gav_settings::verbose_logging()) {
		log.set_level(GAVLog::VERBOSE);
	}
	log.info("construct");
}
GAVSingleton::~GAVSingleton() {
	if (godot::Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	log.info("cleanup");
	cleanup_shaders();
	AvPlayer::codecs.cleanup();
}
