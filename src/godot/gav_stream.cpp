//
// Created by phwhitfield on 05.08.25.
//

#include "gav_stream.h"

using namespace godot;

void GAVStream::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));
	ADD_SIGNAL(MethodInfo("first_frame"));
}
godot::Ref<VideoStreamPlayback> GAVStream::_instantiate_playback() {
	// UtilityFunctions::print("GAVStream::instantiate_playback()");
	auto playback = memnew(GAVPlayback);
	playback->load(get_file());
	playback->callbacks = {
		[&] {
			emit_signal("finished");
		},
		[&] {

		},
		[&] {
			emit_signal("first_frame");
		}
	};
	return playback;
}