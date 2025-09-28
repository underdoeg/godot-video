//
// Created by phwhitfield on 27.09.25.
//

#include "gav_texture.h"

#include "shaders.h"

#include "godot_cpp/classes/rd_shader_file.hpp"
#include "godot_cpp/classes/rd_uniform.hpp"
#include "godot_cpp/classes/rendering_server.hpp"

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>

extern "C" {
#include <libavutil/pixdesc.h>
}

using namespace godot;

GAVTexture::GAVTexture(godot::RenderingDevice *rd) :
		main_rd(rd), conversion_rd(rd) {
}
GAVTexture::~GAVTexture() {
}

bool GAVTexture::setup(const AvVideoInfo &_info) {
	// create the output texture (plane textures will be created on demand)
	if (info.width == _info.width && info.height == _info.height) {
		log.verbose("setup: size is already set to ", info.width, "x", info.height);
		return true;
	}
	bool test_copy = false;

	log.verbose("begin create");
	info = _info;

	if (info.width % 4 != 0 || info.height % 4 != 0) {
		// This is relevant because of the compute shader sizes
		log.warn("video width and height should be divisible by 4. it might still work, but miss a few pixels");
	}

	// the output texture
	Ref<RDTextureFormat> format;
	format.instantiate();
	format->set_width(info.width);
	format->set_height(info.height);
	format->set_mipmaps(1);
	format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
	format->set_usage_bits(
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
			// RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
			RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
	// format->set_array_layers(2);
	// if (test_copy) {
	// 	format->set_format(RenderingDevice::DATA_FORMAT_R8_UNORM);
	// } else {
	format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	// }
	// chroma planes are half the size of the luma plane
	// width = i == 0 || i == 3 ? frame_size.width : Math::ceil(frame_size.width / 2.0f);
	// new_format.height = i == 0 || i == 3 ? frame_size.height : Math::ceil(frame_size.height / 2.0f);

	auto view = memnew(RDTextureView);

	// if (!texture_rid.is_valid()) {
	texture_rid = main_rd->texture_create(format, view);

	if (!texture_rid.is_valid()) {
		log.error("texture_create failed");
		return false;
	};

	set_transparent();

	// this would be necessary if we did conversion on a different rendering device
	if (main_rd != conversion_rd) {
		auto vk_texture = main_rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, texture_rid, 0);
		texture_rid_main = main_rd->texture_create_from_extension(RenderingDevice::TEXTURE_TYPE_2D,
				format->get_format(), format->get_samples(), format->get_usage_bits(),
				vk_texture, format->get_width(), format->get_height(), format->get_depth(), format->get_array_layers());
	} else {
		texture_rid_main = texture_rid;
	}
	log.info("created with size: ", info.width, "x", info.height);
	return true;
}

Ref<Texture2D> GAVTexture::get_texture() {
	if (!texture_rid_main) {
		return {};
	}
	if (texture.is_null() || texture->get_texture_rd_rid() != texture_rid_main) {
		texture.instantiate();
		texture->set_texture_rd_rid(texture_rid_main);
	}
	return texture;
}
void GAVTexture::set_transparent() const {
	PackedByteArray pixels;
	// for (int i = 0; i < num_planes; i++) {
	// 	auto plane = plane_infos[i];
	// 	pixels.resize(plane.byte_size);
	// 	rd->texture_update(planes[i], 0, pixels);
	// }
	pixels.resize(info.width * info.height * 4);

	auto ptr = pixels.ptrw();
	// for (int i = 0; i < info.width * info.height * 4; i+=4) {
	// 	// pixels.ptrw()[i] = rand() % 256;
	// 	ptr[i] = 0;
	// 	ptr[i + 1] = 0;
	// 	ptr[i + 2] = 0;
	// 	ptr[i + 3] = 127;
	// }
	conversion_rd->texture_update(texture_rid, 0, pixels);
}

bool GAVTexture::setup_pipeline(AVPixelFormat pixel_format, AVColorSpace color_space) {
	if (pixel_format == AV_PIX_FMT_NONE) {
		log.error("invalid pixel format requested");
		return false;
	}
	if (pipeline_format == pixel_format) {
		return true;
	}

	if (conversion_pipeline.is_valid()) {
		conversion_rd->free_rid(conversion_pipeline);
	}

	for (const auto &t : plane_textures) {
		conversion_rd->free_rid(t);
	}
	plane_textures.clear();

	plane_infos = av_get_plane_infos(pixel_format, info.width, info.height);

	log.info("setup pipeline for pixel format: ", av_get_pix_fmt_name(pixel_format));

	// TODO handle color space
	auto col_space = color_space;

	// taken from https://github.com/Themaister/Granite/blob/master/video/ffmpeg_decode.cpp#L777
	if (col_space == AVCOL_SPC_UNSPECIFIED) {
		// The common case is when we have an unspecified color space.
		// We have to deduce the color space based on resolution since NTSC, PAL, HD and UHD all
		// have different conversions.
		if (info.height < 625)
			col_space = AVCOL_SPC_SMPTE170M; // 525 line NTSC
		else if (info.height < 720)
			col_space = AVCOL_SPC_BT470BG; // 625 line PAL
		else if (info.height < 2160)
			col_space = AVCOL_SPC_BT709; // BT709 HD
		else
			col_space = AVCOL_SPC_BT2020_NCL; // UHD
	}

	log.verbose("color space: ", av_color_space_name(col_space));

	for (const auto &plane : plane_infos) {
		auto tex_format = RenderingDevice::DATA_FORMAT_R8_UNORM;
		if (
				pixel_format == AV_PIX_FMT_P016LE ||
				pixel_format == AV_PIX_FMT_P010LE) {
			tex_format = RenderingDevice::DATA_FORMAT_R16_UNORM;
		}

		Ref<RDTextureFormat> format;
		format.instantiate();
		format->set_width(plane.width);
		format->set_height(plane.height);
		format->set_mipmaps(1);
		format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
		format->set_usage_bits(
				RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
				// RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT |
				// RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
				// RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
				RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
				RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
		format->set_format(tex_format);

		auto view = memnew(RDTextureView);

		auto rid = conversion_rd->texture_create(format, view);
		// planes[i] = rd->texture_buffer_create(byte_size, RenderingDevice::DATA_FORMAT_R8_UINT);
		if (!rid.is_valid()) {
			log.error("texture_create failed");
			return false;
		}
		plane_textures.push_back(rid);

		log.verbose("created plane - size: ", plane.width, "x", plane.height, ", byte_size: ", plane.byte_size, ",  line_size: ", plane.line_size);
	}

	// // create the compute shader
	Vector2i size = { info.width, info.height };
	if (pixel_format == AV_PIX_FMT_NV12) {
		conversion_shader = nv12(conversion_rd, size);
	} else if (pixel_format == AV_PIX_FMT_YUV420P) {
		conversion_shader = yuv420(conversion_rd, size);
	} else if (pixel_format == AV_PIX_FMT_P010LE) {
		conversion_shader = p010le(conversion_rd, size);
	} else if (pixel_format == AV_PIX_FMT_P016LE) {
		conversion_shader = p016le(conversion_rd, size);
	} else if (pixel_format == AV_PIX_FMT_YUV420P10LE) {
		conversion_shader = yuv420p10le(conversion_rd, size);
	} else {
		UtilityFunctions::printerr("unsupported pixel format: ", av_get_pix_fmt_name(pixel_format));
	}

	if (conversion_shader.is_valid()) {
		TypedArray<RDUniform> uniforms;

		auto u_result = memnew(RDUniform);
		u_result->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
		u_result->set_binding(0);
		u_result->add_id(texture_rid);
		uniforms.push_back(u_result);

		for (int i = 0; i < plane_textures.size(); i++) {
			auto u = memnew(RDUniform);
			u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
			u->set_binding(i + 1);
			u->add_id(plane_textures[i]);
			uniforms.push_back(u);
		}
		conversion_shader_uniform_set = conversion_rd->uniform_set_create(uniforms, conversion_shader, 0);
		if (!conversion_shader_uniform_set.is_valid()) {
			UtilityFunctions::printerr("failed to create shader uniform set");
			return false;
		}
		conversion_pipeline = conversion_rd->compute_pipeline_create(conversion_shader);
	} else {
		UtilityFunctions::printerr("Could not bind textures to conversion shader, shader is invalid");
		return false;
	}
	// set_transparent();
	pipeline_format = pixel_format;
	// pipeline_ready = true;
	return true;
}

void GAVTexture::run_conversion_shader() const {
	MEASURE;
	if (!conversion_shader.is_valid()) {
		UtilityFunctions::printerr("Conversion shader is invalid");
		return;
	}
	if (!conversion_shader_uniform_set.is_valid()) {
		UtilityFunctions::printerr("Conversion shader uniform set is invalid");
		return;
	}
	if (!conversion_pipeline.is_valid()) {
		UtilityFunctions::printerr("Conversion pipeline is invalid");
		return;
	}

	// if (!test_copy) {
	// calculate invocation size
	auto compute = conversion_rd->compute_list_begin();
	conversion_rd->compute_list_bind_compute_pipeline(compute, conversion_pipeline);
	conversion_rd->compute_list_bind_uniform_set(compute, conversion_shader_uniform_set, 0);
	conversion_rd->compute_list_dispatch(compute, std::floor(info.width / 4), std::floor(info.height / 4), 1);
	conversion_rd->compute_list_end();
	if (conversion_rd != RenderingServer::get_singleton()->get_rendering_device()) {
		conversion_rd->submit();
		conversion_rd->sync();
	}
}

void GAVTexture::update(const AvVideoFrame &frame) {
	MEASURE_N("Texture");
	if (frame.type == VK_BUFFER) {
		log.warn("VK_BUFFER frame types are not yet implemented.");
		return;
	}
	if (frame.type == HW_BUFFER) {
		log.warn("HW_BUFFER frame types should get converted by the av_player.");
		return;
	}
	auto pixel_format = static_cast<AVPixelFormat>(frame.frame->format);
	if (!setup_pipeline(pixel_format, frame.color_space)) {
		log.error("failed to setup conversion pipeline");
		return;
	}

	if (buffers.size() != plane_infos.size())
		buffers.resize(plane_infos.size());

	for (int i = 0; i < plane_infos.size(); i++) {
		const auto [width, height, depth, line_size, byte_size] = plane_infos[i];

		if (buffers[i].size() < byte_size)
			buffers[i].resize(byte_size);

		const auto src = frame.frame->data[i];

		// we expect tight lines without extra, but line_size of the frame can have additional padding. check for it
		const auto frame_line_size = frame.frame->linesize[i];
		// UtilityFunctions::print(info.line_size, " -- ", frame->linesize[i]);
		if (frame_line_size == line_size) {
			MEASURE_N("memcopy tight");
			memcpy(buffers[i].ptrw(), src, byte_size);
		} else {
			MEASURE_N("memcopy lines");
			// copy line for line
			for (size_t y = 0; y < height; y++) {
				memcpy(buffers[i].ptrw() + line_size * y, src + frame_line_size * y, line_size);
			}
		}

		if (plane_textures.size() > i) {
			MEASURE_N("upload");
			conversion_rd->texture_update(plane_textures.at(i), 0, buffers[i]);
		}
	}

	run_conversion_shader();
}
