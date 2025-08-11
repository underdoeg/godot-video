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

public:
	godot::Ref<godot::VideoStreamPlayback>  _instantiate_playback() override;
};
