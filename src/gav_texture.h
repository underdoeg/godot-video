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
public:
	using Buffers = std::array<godot::PackedByteArray, 4>;
	using BuffersPtr = std::shared_ptr<Buffers>;

private:
	enum Pipeline {
		VULKAN,
		SW,
		UNKNOWN,
	};

	// Pipeline pipeline_type = UNKNOWN;

	struct PlaneInfo {
		int width, height, depth, line_size;
		size_t byte_size;
	};

	int width = 0;
	int height = 0;
	godot::RID texture_rid{};
	godot::RID texture_rid_main{};

	godot::RenderingDevice *rd = nullptr;
	int num_planes = 0;
	std::array<PlaneInfo, 4> plane_infos{};
	std::array<godot::RID, 4> planes{};
	Buffers plane_buffers{};
	AVFramePtr conversion_frame;
	godot::RID conversion_shader{};
	godot::RID conversion_shader_uniform_set{};
	godot::RID conversion_pipeline{};
	// VkCommandBuffer* cmd_buf;

	AVPixelFormat pipeline_format = AV_PIX_FMT_NONE;

	bool setup_pipeline(AVPixelFormat pixel_format);
	void run_conversion_shader() const;

public:
	GAVTexture();
	~GAVTexture();
	bool test_copy = false;
	AVCodecContext *codec_ctx = nullptr;

	int get_width() const { return width; }
	int get_height() const { return height; }

	// setup returns an RID to wrap
	godot::RID setup(int w, int h, godot::RenderingDevice *_rd);
	void set_black();
	// [[nodiscard]] godot::Ref<godot::Texture2DRD> get_texture() const {
	// 	return texture;
	// }

	void frame_to_buffers(const AVFramePtr &frame, Buffers &buffers) const;

	void update_from_buffers(const Buffers &buffers, AVPixelFormat format);
	void update_from_vulkan(const AVFramePtr &frame);
	void update_from_sw(const AVFramePtr &frame);
	void update_from_hw(const AVFramePtr &shared);
};
