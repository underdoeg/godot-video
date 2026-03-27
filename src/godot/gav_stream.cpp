//
// Created by phwhitfield on 05.08.25.
//

#include "gav_stream.h"

using namespace godot;

void GAVStream::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));
	ADD_SIGNAL(MethodInfo("first_frame"));

	ClassDB::bind_method(D_METHOD("set_timecode_enabled", "enabled"), &GAVStream::set_timecode_enabled);
	ClassDB::bind_method(D_METHOD("get_timecode_enabled"), &GAVStream::get_timecode_enabled);
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "timecode_enabled"), "set_timecode_enabled", "get_timecode_enabled");

	ClassDB::bind_method(D_METHOD("set_timecode_user_data", "data"), &GAVStream::set_timecode_user_data);
	ClassDB::bind_method(D_METHOD("get_timecode_user_data"), &GAVStream::get_timecode_user_data);
	ADD_PROPERTY(PropertyInfo(Variant::INT, "timecode_user_data"), "set_timecode_user_data", "get_timecode_user_data");
}

godot::Ref<VideoStreamPlayback> GAVStream::_instantiate_playback() {
	UtilityFunctions::print("GAVStream::instantiate_playback()");
	playback = memnew(GAVPlayback);
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

	playback->timecode_enabled = timecode_enabled;
	playback->timecode_user_data = timecode_user_data;

	return playback;
}
void GAVStream::set_timecode_enabled(bool enable) {
	timecode_enabled = enable;
	UtilityFunctions::print("GAVStream::set_timecode_enabled()");
	if (playback) {
		playback->timecode_enabled = timecode_enabled;
	}
}
bool GAVStream::get_timecode_enabled() const {
	return timecode_enabled;
}
void GAVStream::set_timecode_user_data(int p_data) {
	timecode_user_data = p_data;
	if (playback) {
		playback->timecode_user_data = timecode_user_data;
	}
}
int GAVStream::get_timecode_user_data() const {
	return timecode_user_data;
}