//
// Created by phwhitfield on 06.08.25.
//

#pragma once

#include "packet_decoder.h"

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
	int width = 0;
	int height = 0;
	godot::RID texture_rid{};
	godot::RID texture_rid_main{};
	godot::Ref<godot::Texture2DRD> texture;
	AVCodecContext *codec_ctx = nullptr;
	;
	godot::RenderingDevice *rd = nullptr;
	int num_planes = 0;
	std::array<PlaneInfo, 4> plane_infos{};
	std::array<godot::RID, 4> planes{};
	godot::RID conversion_shader{};
	godot::RID conversion_shader_uniform_set{};
	godot::RID conversion_pipeline{};
	// VkCommandBuffer* cmd_buf;

	AVPixelFormat pipeline_format = AV_PIX_FMT_NONE;

	bool setup_pipeline(AVPixelFormat pixel_format);

public:
	GAVTexture();
	~GAVTexture();
	bool test_copy = false;

	godot::Ref<godot::Texture2DRD> setup(AVCodecContext *ctx, godot::RenderingDevice *rd);
	[[nodiscard]] godot::Ref<godot::Texture2DRD> get_texture() const {
		return texture;
	}
	void update(AVFramePtr frame);
};
