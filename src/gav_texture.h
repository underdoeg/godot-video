//
// Created by phwhitfield on 06.08.25.
//

#pragma once
#include "godot_cpp/classes/rendering_device.hpp"
#include "godot_cpp/classes/texture2drd.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

class GAVTexture {
	godot::Ref<godot::Texture2DRD> texture;
	AVCodecContext *codec_ctx;
	godot::RenderingDevice *rd;

public:
	GAVTexture();
	godot::Ref<godot::Texture2DRD> setup(AVCodecContext *ctx, godot::RenderingDevice *rd);
	[[nodiscard]] godot::Ref<godot::Texture2DRD> get_texture() const {
		return texture;
	}
	void update(const AVFrame *av_frame) const;
};
