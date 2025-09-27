#pragma once
#include "av_wrapper/av_player.h"
#include "gav_log.h"
#include "godot_cpp/classes/rendering_device.hpp"
#include "godot_cpp/classes/texture2d.hpp"
#include "godot_cpp/classes/texture2drd.hpp"
#include "godot_cpp/variant/rid.hpp"

class GAVTexture {
	godot::RenderingDevice *main_rd;
	godot::RenderingDevice *conversion_rd;

	godot::RID conversion_pipeline{};
	godot::RID conversion_shader{};
	godot::RID conversion_shader_uniform_set{};

	AVPixelFormat pipeline_format = AV_PIX_FMT_NONE;

	AvPlaneInfos plane_infos{};
	std::vector<godot::RID> plane_textures;
	AvVideoInfo info{};

	godot::RID texture_rid, texture_rid_main;

	std::vector<godot::PackedByteArray> buffers;

	godot::Ref<godot::Texture2DRD> texture;

	bool setup_pipeline(AVPixelFormat pixel_format, AVColorSpace color_space = AVCOL_SPC_UNSPECIFIED);
	void run_conversion_shader() const;

public:
	GAVLog log = GAVLog("GAVTexture");

	explicit GAVTexture(godot::RenderingDevice *rd);
	~GAVTexture();
	bool setup(const AvVideoInfo &info);
	godot::Ref<godot::Texture2D> get_texture();

	void set_transparent() const;

	void update(const AvVideoFrame &frame);
};
