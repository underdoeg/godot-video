//
// Created by phwhitfield on 22.08.25.
//

#include "packet_decoder.h"

#include <algorithm>

using namespace godot;

// std::shared_ptr<AVFrame> make_shared(AVFrame* )

PacketDecoder::PacketDecoder(AVCodecContext *ctx, FrameHandler handler, int _max_frames) :
		codec_context(ctx),
		frame_handler(std::move(handler)),
		max_frames(_max_frames) {
	name = avcodec_get_name(ctx->codec_id);
	UtilityFunctions::print("Created PacketDecoder for ", name);
}

bool PacketDecoder::is_ready() const {
	return state == State::READY && frames.size() < max_frames;
}
void PacketDecoder::handle(AVPacket *pkt) {
	// while (true) {
	const auto res = avcodec_send_packet(codec_context, pkt);
	if (res == 0 || res == AVERROR(EAGAIN)) {
		// packet received succesfully or context is still waiting on pulling the decoded frames
		state = State::DECODE;
		// break;
	} else if (!ff_ok(res)) {
		godot::UtilityFunctions::printerr("Frame Handler received Error");
		state = State::ERROR;
	}
	// break;
	// }
	// godot::UtilityFunctions::print("handle done");
	av_packet_unref(pkt);
}

void PacketDecoder::process() {
	// offer any frames in the queue to the frame handler
	if (!frames.empty()) {
		const auto [from, to] = std::ranges::remove_if(frames, frame_handler);
		frames.erase(from, to);
	}

	if (state != State::DECODE) {
		return;
	}

	auto frame = av_frame_ptr();
	const auto res = avcodec_receive_frame(codec_context, frame.get());
	if (res == AVERROR(EAGAIN)) {
		state = State::READY;
		return;
	}
	if (!ff_ok(res)) {
		state = State::ERROR;
		return;
	}
	// frame is good, offer it straight away
	if (!frame_handler(frame)) {
		frames.push_back(frame);
	}
}