//
// Created by phwhitfield on 22.08.25.
//

#pragma once
#include "helpers.h"

#include <deque>
#include <functional>
#include <memory>
#include <utility>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
}

using FrameHandler = std::function<bool(const AVFramePtr& frame)>;

class PacketDecoder {
	enum State {
		READY,
		DECODE,
		ERROR
	};
	State state = State::READY;
	AVCodecContext *codec_context = nullptr;

	std::deque<std::shared_ptr<AVFrame>> frames = {};

	// callback function takes the avFrame and has to unref it.
	// return false if you dont want to use the frame
	FrameHandler frame_handler;
	godot::String name;

	int max_frames = 1;

public:
	explicit PacketDecoder(AVCodecContext *ctx, FrameHandler handler, int max_frames = 10);

	[[nodiscard]] bool is_ready() const;
	[[nodiscard]] bool queue_empty() const {
		return frames.empty();
	}

	void handle(AVPacket *pkt);
	void process();
	auto num_frames() const {
		return frames.size();
	}
	void clear() {
		frames.clear();
	}
};
