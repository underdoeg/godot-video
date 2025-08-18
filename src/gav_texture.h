//
// Created by phwhitfield on 06.08.25.
//

#pragma once

#include <array>

#include "godot_cpp/classes/rendering_device.hpp"
#include "godot_cpp/classes/texture2drd.hpp"

extern "C" {
#include <libavcodec/avcodec.h>
}

class GAVTexture {
	struct PlaneInfo {
		int width, height, depth;
	};

	godot::Ref<godot::Texture2DRD> texture;
	AVCodecContext *codec_ctx;
	godot::RenderingDevice *rd;
	int num_planes = 0;
	std::array<PlaneInfo, 4> plane_infos;
	std::array<godot::RID, 4> planes;
	godot::RID conversion_shader;

public:
	GAVTexture();
	~GAVTexture();
	godot::Ref<godot::Texture2DRD> setup(AVCodecContext *ctx, godot::RenderingDevice *rd);
	[[nodiscard]] godot::Ref<godot::Texture2DRD> get_texture() const {
		return texture;
	}
	void update(AVFrame *frame) const;
};
