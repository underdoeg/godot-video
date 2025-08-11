#pragma once

#include "godot_cpp/classes/engine.hpp"
#include "godot_cpp/classes/image_texture.hpp"
#include "godot_cpp/classes/node.hpp"
#include "godot_cpp/classes/texture2drd.hpp"
#include "godot_cpp/classes/wrapped.hpp"
#include "godot_cpp/variant/variant.hpp"

///////////////////////// FOR TESTING NOT IN USE ///////////////////////////////////

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#include <thread>

namespace godot {
class AvVideo : public Node {
	GDCLASS(AvVideo, Node)

	std::thread thread;
	AVFormatContext *fmt_ctx = nullptr;
	AVCodecContext *dec_ctx = nullptr;
	int video_stream_index = -1;

	// AVCodecContext *video_codec_ctx = nullptr;

protected:
	static void _bind_methods();

public:
	AvVideo() = default;
	~AvVideo() override = default;

	void play();
	Ref<Texture2D> get_texture();

	void _process(double p_delta) override;
};

} //namespace godot