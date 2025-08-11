//
// Created by phwhitfield on 05.08.25.
//

#include "gav_stream.h"

using namespace godot;

void GAVStream::_bind_methods() {
}
godot::Ref<VideoStreamPlayback> GAVStream::_instantiate_playback() {
	// UtilityFunctions::print("GAVStream::instantiate_playback()");
	auto playback = memnew(GAVPlayback);
	playback->load(get_file());
	return playback;
}