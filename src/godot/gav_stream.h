//
// Created by phwhitfield on 05.08.25.
//
#pragma once

#include "gav_playback.h"

#include <godot_cpp/classes/resource.hpp>
#include <godot_cpp/classes/video_stream.hpp>

class GAVStream : public godot::VideoStream {
	GDCLASS(GAVStream, VideoStream)
	static void _bind_methods();
	bool timecode_enabled = false;
	int timecode_user_data = 0;
	GAVPlayback* playback = nullptr;
public:
	godot::Ref<godot::VideoStreamPlayback>  _instantiate_playback() override;

	void set_timecode_enabled(bool enable);
	bool get_timecode_enabled() const;
	void set_timecode_user_data(int p_data);
	int get_timecode_user_data() const;
};
