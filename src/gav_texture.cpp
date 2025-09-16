//
// Created by phwhitfield on 06.08.25.
//

#include "godot_cpp/classes/rd_shader_file.hpp"
#include "godot_cpp/classes/rd_uniform.hpp"
#include "godot_cpp/classes/rendering_server.hpp"

#include "shaders.h"
#include "vk_ctx.h"

extern "C" {
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/imgutils.h>
#include <libavutil/pixdesc.h>
}

#include <godot_cpp/classes/rd_texture_format.hpp>
#include <godot_cpp/classes/rd_texture_view.hpp>

#include "gav_texture.h"

using namespace godot;

struct VkFunctions {
	PFN_vkBeginCommandBuffer begin_command_buffer;
	PFN_vkEndCommandBuffer end_command_buffer;
	PFN_vkCmdCopyImage cmd_copy_image;
	PFN_vkCreateCommandPool create_command_pool;
	PFN_vkAllocateCommandBuffers allocate_command_buffers;
	PFN_vkQueueSubmit queue_submit;
	PFN_vkCmdCopyImageToBuffer copy_image_to_buffer;
};

static VkFunctions *vkf = nullptr;

GAVTexture::GAVTexture() {
}
GAVTexture::~GAVTexture() {
	for (auto &b : planes) {
		if (b.is_valid()) {
			// UtilityFunctions::print("release plane");
			rd->free_rid(b);
		}
	}

	if (texture_rid != texture_rid_main && texture_rid_main.is_valid()) {
		// UtilityFunctions::print("release texture_rid_main");
		RenderingServer::get_singleton()->get_rendering_device()->free_rid(texture_rid_main);
	}

	if (conversion_pipeline.is_valid()) {
		// UtilityFunctions::print("release conversion_pipeline");
		rd->free_rid(conversion_pipeline);
	}

	if (texture_rid.is_valid()) {
		// UtilityFunctions::print("release texture_rid");
		rd->free_rid(texture_rid);
	}
}

RID GAVTexture::setup(int w, int h, RenderingDevice *_rd) {
	// if (texture.is_valid() && texture->get_texture_rd_rid().is_valid()) {
	// 	UtilityFunctions::print("TODO cleanup old texture");
	// }

	rd = _rd;

	// rd->compute_pipeline_create()

	// auto *frames = reinterpret_cast<AVHWFramesContext *>(codec_ctx->hw_frames_ctx->data);

	// UtilityFunctions::print("reported pixel format on setup is ", av_get_pix_fmt_name(pixel_format));
	pipeline_format = AV_PIX_FMT_NONE;

	// if (texture.is_valid()) {
	// 	if (width == codec_ctx->width && height == codec_ctx->height) {
	// 		return texture;
	// 	}

	// if (texture_rid.is_valid()) {
	// 	rd->free_rid(texture_rid);
	// }
	// if (texture_rid_main.is_valid()) {
	// RenderingServer::get_singleton()->get_rendering_device()->free_rid(texture_rid_main);
	// }
	// }

	width = w;
	height = h;

	if (verbose_logging)
		UtilityFunctions::print("create texture of size ", width, "x", height);
	//
	if (width % 8 != 0 || height % 8 != 0) {
		// This is relevant because of the compute shader sizes
		UtilityFunctions::printerr("video width and height should be divisible by 8. it might still work, but miss a few pixels");
	}

	// the output texture
	Ref<RDTextureFormat> format;
	format.instantiate();
	format->set_width(width);
	format->set_height(height);
	format->set_mipmaps(1);
	format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
	format->set_usage_bits(
			RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
			RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
			// RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
			RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
	// format->set_array_layers(2);
	if (test_copy) {
		format->set_format(RenderingDevice::DATA_FORMAT_R8_UNORM);
	} else {
		format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);
	}
	// chroma planes are half the size of the luma plane
	// width = i == 0 || i == 3 ? frame_size.width : Math::ceil(frame_size.width / 2.0f);
	// new_format.height = i == 0 || i == 3 ? frame_size.height : Math::ceil(frame_size.height / 2.0f);

	auto view = memnew(RDTextureView);

	// if (!texture_rid.is_valid()) {
	texture_rid = rd->texture_create(format, view);

	PackedByteArray black;
	black.resize(width * height * 4);
	rd->texture_update(texture_rid, 0, black);

	// }
	if (!texture_rid.is_valid()) {
		UtilityFunctions::printerr("Could not create texture");
		return {};
	}

	const auto rd_main = RenderingServer::get_singleton()->get_rendering_device();
	if (rd_main == rd) {
		// UtilityFunctions::print("AV Texture is using the main rendering device");
		// texture->set_texture_rd_rid(texture_rid);
		return texture_rid;
	}
	// UtilityFunctions::print("AV Texture is not using the main rendering device");
	// expose local texture to main rd device
	auto vk_texture = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, texture_rid, 0);
	texture_rid_main = rd_main->texture_create_from_extension(RenderingDevice::TEXTURE_TYPE_2D,
			format->get_format(), format->get_samples(), format->get_usage_bits(),
			vk_texture, format->get_width(), format->get_height(), format->get_depth(), format->get_array_layers());
	//argument removed for compatibility with godot 4.4. , format->get_mipmaps());
	// texture->set_texture_rd_rid(texture_rid_main);
	return texture_rid_main;

	// the pixel format  reported might change on the first frame, so we create the shader in update_from_vulkan
	// auto pixel_format = frames->sw_format;
}

bool GAVTexture::setup_pipeline(AVPixelFormat pixel_format) {
	if (pipeline_format == pixel_format) {
		return true;
	}

	if (conversion_pipeline.is_valid()) {
		rd->free_rid(conversion_pipeline);
	}
	for (auto &b : planes) {
		if (b.is_valid()) {
			rd->free_rid(b);
		}
	}

	// auto *hw_dev = reinterpret_cast<AVHWDeviceContext *>(codec_ctx->hw_device_ctx->data);
	// auto *vk = static_cast<AVVulkanDeviceContext *>(hw_dev->hwctx);
	// auto *frames = reinterpret_cast<AVHWFramesContext *>(codec_ctx->hw_frames_ctx->data);

	if (verbose_logging)
		UtilityFunctions::print("setup pipeline for pixel format: ", av_get_pix_fmt_name(pixel_format));
	std::array<int, 4> line_sizes{ 0, 0, 0, 0 };
	std::array<ptrdiff_t, 4> line_sizes_ptr;
	std::array<size_t, 4> plane_sizes{ 0, 0, 0, 0 };

	if (av_image_fill_linesizes(line_sizes.data(), pixel_format, width) < 0) {
		UtilityFunctions::printerr("Could not fill line sizes");
		return false;
	}
	for (int i = 0; i < 4; i++) {
		line_sizes_ptr[i] = line_sizes[i];
		// line_sizes[i] = av_image_get_linesize(pixel_format, width, i);
	}
	if (av_image_fill_plane_sizes(plane_sizes.data(), pixel_format, height, line_sizes_ptr.data()) < 0) {
		UtilityFunctions::printerr("Could not fill plane sizes");
		return false;
	}

	// TODO handle color space
	auto col_space = codec_ctx ? codec_ctx->colorspace : AVCOL_SPC_UNSPECIFIED;

	// taken from https://github.com/Themaister/Granite/blob/master/video/ffmpeg_decode.cpp#L777
	if (col_space == AVCOL_SPC_UNSPECIFIED) {
		// The common case is when we have an unspecified color space.
		// We have to deduce the color space based on resolution since NTSC, PAL, HD and UHD all
		// have different conversions.
		if (height < 625)
			col_space = AVCOL_SPC_SMPTE170M; // 525 line NTSC
		else if (height < 720)
			col_space = AVCOL_SPC_BT470BG; // 625 line PAL
		else if (height < 2160)
			col_space = AVCOL_SPC_BT709; // BT709 HD
		else
			col_space = AVCOL_SPC_BT2020_NCL; // UHD
	}
	//

	// create texture buffers, maybe we could use the ones provided by ffmpeg directly
	// but godot does probably not allow mixing with native vulkan types and we don't really want to write the compute shader in vulkan at this point
	String plane_sizes_str = "Plane sizes: \n";
	for (int i = 0; i < planes.size(); i++) {
		auto byte_size = plane_sizes[i];
		auto line_size = line_sizes[i];
		if (byte_size > 0) {
			plane_sizes_str += "bytes: " + String::num_int64(byte_size) + " - line: " + String::num_int64(line_size) + " - height: " + String::num_int64(byte_size / line_size) + "\n";

			// create texture planes
			int w = line_size;
			auto tex_format = RenderingDevice::DATA_FORMAT_R8_UNORM;

			if (pixel_format == AV_PIX_FMT_P016LE
					// || (pixel_format == AV_PIX_FMT_YUV420P10LE)
			) {
				w /= 2;
				tex_format = RenderingDevice::DATA_FORMAT_R16_UNORM;
			}
			auto h = byte_size / line_size;
			// if (h == 2)
			// 	h = 1080;
			plane_infos[i] = { w, static_cast<int>(h), 1, line_size, byte_size };

			Ref<RDTextureFormat> format;
			format.instantiate();
			format->set_width(w);
			format->set_height(h);
			format->set_mipmaps(1);
			format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
			format->set_usage_bits(
					RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
					// RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
					// RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
					RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
					RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
			format->set_format(tex_format);

			auto view = memnew(RDTextureView);

			planes[i] = rd->texture_create(format, view);
			// planes[i] = rd->texture_buffer_create(byte_size, RenderingDevice::DATA_FORMAT_R8_UINT);
			if (!planes[i]) {
				UtilityFunctions::print("failed to create texture");
			}

			num_planes = i + 1;
		}
	}

	// num_planes = 2;

	if (verbose_logging) {
		UtilityFunctions::print("number of planes: ", num_planes, "  ", plane_sizes_str);
	}
	// // create the compute shader
	if (pixel_format == AV_PIX_FMT_NV12) {
		conversion_shader = nv12(rd, { width, height });
	} else if (pixel_format == AV_PIX_FMT_YUV420P) {
		conversion_shader = yuv420(rd, { width, height });
	} else if (pixel_format == AV_PIX_FMT_P010LE) {
		conversion_shader = p010le(rd, { width, height });
	} else if (pixel_format == AV_PIX_FMT_P016LE) {
		conversion_shader = p016le(rd, { width, height });
	} else if (pixel_format == AV_PIX_FMT_YUV420P10LE) {
		conversion_shader = yuv420p10le(rd, { width, height });
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

		for (int i = 0; i < num_planes; i++) {
			auto u = memnew(RDUniform);
			u->set_uniform_type(RenderingDevice::UNIFORM_TYPE_IMAGE);
			u->set_binding(i + 1);
			u->add_id(planes[i]);
			uniforms.push_back(u);
		}
		conversion_shader_uniform_set = rd->uniform_set_create(uniforms, conversion_shader, 0);
		if (!conversion_shader_uniform_set.is_valid()) {
			UtilityFunctions::printerr("failed to create shader uniform set");
			return false;
		}
		conversion_pipeline = rd->compute_pipeline_create(conversion_shader);
	} else {
		UtilityFunctions::printerr("Could not bind textures to conversion shader, shader is invalid");
		return false;
	}
	pipeline_format = pixel_format;
	return true;
}
void GAVTexture::run_conversion_shader() {
	// run the conversion shader
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
	if (!test_copy) {
		// calculate invocation size
		auto compute = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(compute, conversion_pipeline);
		rd->compute_list_bind_uniform_set(compute, conversion_shader_uniform_set, 0);
		rd->compute_list_dispatch(compute, std::floor(width / 8), std::floor(height / 8), 1);
		rd->compute_list_end();
		if (rd != RenderingServer::get_singleton()->get_rendering_device()) {
			rd->submit();
			rd->sync();
		}
	}
}

void GAVTexture::update_from_vulkan(const AVFramePtr &frame) {
	// UtilityFunctions::print("update_from_vulkan texture");
	if (!vkf) {
		auto v = reinterpret_cast<VkInstance>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TOPMOST_OBJECT, {}, 0));
		vkf = new VkFunctions{
			reinterpret_cast<PFN_vkBeginCommandBuffer>(vk_proc_addr(v, "vkBeginCommandBuffer")),
			reinterpret_cast<PFN_vkEndCommandBuffer>(vk_proc_addr(v, "vkEndCommandBuffer")),
			reinterpret_cast<PFN_vkCmdCopyImage>(vk_proc_addr(v, "vkCmdCopyImage")),
			reinterpret_cast<PFN_vkCreateCommandPool>(vk_proc_addr(v, "vkCreateCommandPool")),
			reinterpret_cast<PFN_vkAllocateCommandBuffers>(vk_proc_addr(v, "vkAllocateCommandBuffers")),
			reinterpret_cast<PFN_vkQueueSubmit>(vk_proc_addr(v, "vkQueueSubmit")),
			reinterpret_cast<PFN_vkCmdCopyImageToBuffer>(vk_proc_addr(v, "vkCmdCopyImageToBuffer")),
		};
	}

	auto *hw_dev = reinterpret_cast<AVHWDeviceContext *>(codec_ctx->hw_device_ctx->data);
	// auto pixel_format = static_cast<AVPixelFormat>(frame->format);
	// UtilityFunctions::print("update_from_vulkan frame format is: ", av_get_pix_fmt_name(pixel_format));
	// return;

	// const VkFormat *fmts = nullptr;
	// VkImageAspectFlags aspects;
	// VkImageUsageFlags usage;
	// int nb_images;

	// int ret = av_vkfmt_from_pixfmt2(hwdev, active_upload_pix_fmt,
	// 		VK_IMAGE_USAGE_SAMPLED_BIT, &fmts,
	// 		&nb_images, &aspects, &usage);

	auto *vk = static_cast<AVVulkanDeviceContext *>(hw_dev->hwctx);

	auto *frames = reinterpret_cast<AVHWFramesContext *>(codec_ctx->hw_frames_ctx->data);

	if (!setup_pipeline(frames->sw_format)) {
		UtilityFunctions::printerr("failed to setup render pipeline");
		return;
	}
	auto *vk_frames_ctx = static_cast<AVVulkanFramesContext *>(frames->hwctx);

	auto vk_queue = reinterpret_cast<VkQueue>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, {}, 0));

	// that can be allocated out of it.

	VkCommandPoolCreateInfo cmd_pool_info = {};
	cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmd_pool_info.pNext = nullptr;
	cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmd_pool_info.queueFamilyIndex = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_QUEUE_FAMILY, {}, 0);

	VkCommandPool cmd_copy_frames;
	if (vkf->create_command_pool(vk->act_dev, &cmd_pool_info, nullptr, &cmd_copy_frames) != VK_SUCCESS) {
		UtilityFunctions::printerr("Couldn't create command pool");
		return;
	}

	// Populate a command buffer info struct with a reference to the command pool from which the memory for the command buffer is taken.
	// Notice the "level" parameter which ensures these will be primary command planes.
	VkCommandBufferAllocateInfo cmd_buf_info = {};
	cmd_buf_info.pNext = nullptr;
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = cmd_copy_frames;
	cmd_buf_info.commandBufferCount = 1;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VkCommandBuffer cmd_buf;

	if (vkf->allocate_command_buffers(vk->act_dev, &cmd_buf_info, &cmd_buf) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not allocate command planes!");
		return;
	}

	auto *vk_frame = reinterpret_cast<AVVkFrame *>(frame->data[0]);

	vk_frames_ctx->lock_frame(frames, vk_frame);

	auto release_frames = [&] {
		vk_frames_ctx->unlock_frame(frames, vk_frame);
	};

	// copy all image planes
	VkCommandBufferBeginInfo cmd_buf_begin;
	cmd_buf_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmd_buf_begin.pNext = nullptr;
	cmd_buf_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	cmd_buf_begin.pInheritanceInfo = nullptr;

	if (vkf->begin_command_buffer(cmd_buf, &cmd_buf_begin) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not begin command buffer");
		release_frames();
		return;
	}

	if (test_copy) {
		const auto ffmpeg_img = vk_frame->img[0];
		const auto info = plane_infos[0];
		// use image
		const auto godot_img = reinterpret_cast<VkImage>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, texture_rid, 0));
		VkImageCopy region;

		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_PLANE_0_BIT;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffset.x = region.srcOffset.y = region.srcOffset.z = 0;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.mipLevel = 0;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
		region.dstOffset.x = region.dstOffset.y = region.dstOffset.z = 0;
		region.extent.width = info.width;
		region.extent.height = info.height;
		region.extent.depth = 1;
		vkf->cmd_copy_image(cmd_buf, ffmpeg_img, vk_frame->layout[0], godot_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		// UtilityFunctions::print("TEST COPY");
	} else {
		for (int i = 0; i < num_planes; i++) {
			// planes are always in the same image? as layers?
			const auto ffmpeg_img = vk_frame->img[0];
			const auto info = plane_infos[i];
			// UtilityFunctions::print("Copy plane ", i, " - ", info.width, "x", info.height);

			if (!planes[i].is_valid()) {
				UtilityFunctions::printerr("godot image ", i, " is not valid");
				release_frames();
				return;
			}

			if (!ffmpeg_img) {
				UtilityFunctions::printerr("ffmpeg image ", i, " is not valid");
				release_frames();
				return;
			}

			auto layer_flag = VK_IMAGE_ASPECT_PLANE_0_BIT;
			if (i == 1) {
				layer_flag = VK_IMAGE_ASPECT_PLANE_1_BIT;
			} else if (i == 2) {
				layer_flag = VK_IMAGE_ASPECT_PLANE_2_BIT;
			}

			// use image
			const auto godot_img = reinterpret_cast<VkImage>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, planes[i], 0));
			VkImageCopy region;
			region.srcSubresource.aspectMask = layer_flag;
			region.srcSubresource.mipLevel = 0;
			region.srcSubresource.baseArrayLayer = 0;
			region.srcSubresource.layerCount = 1;
			region.srcOffset.x = region.srcOffset.y = region.srcOffset.z = 0;
			region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			region.dstSubresource.mipLevel = 0;
			region.dstSubresource.baseArrayLayer = 0;
			region.dstSubresource.layerCount = 1;
			region.dstOffset.x = region.dstOffset.y = region.dstOffset.z = 0;
			region.extent.width = info.width;
			region.extent.height = info.height;
			region.extent.depth = 1;
			vkf->cmd_copy_image(cmd_buf, ffmpeg_img, vk_frame->layout[0], godot_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
		}
	}

	if (vkf->end_command_buffer(cmd_buf) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not end command buffer");
		release_frames();
		return;
	}

	VkTimelineSemaphoreSubmitInfo timeline_info;
	timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
	timeline_info.pNext = nullptr;
	timeline_info.waitSemaphoreValueCount = 1;
	timeline_info.pWaitSemaphoreValues = vk_frame->sem_value;

	std::array<VkPipelineStageFlags, 4> stageFlags = { VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT };

	// execute the copy commands
	VkSubmitInfo vsi;
	vsi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	vsi.waitSemaphoreCount = 1;
	vsi.pWaitSemaphores = vk_frame->sem;
	vsi.signalSemaphoreCount = 1;
	vsi.signalSemaphoreCount = 0;
	vsi.pWaitDstStageMask = stageFlags.data();
	vsi.pNext = &timeline_info;
	vsi.commandBufferCount = 1;
	vsi.pCommandBuffers = &cmd_buf;
	if (vkf->queue_submit(vk_queue, 1, &vsi, VK_NULL_HANDLE) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not queue command buffer");
		release_frames();
		return;
	}

	release_frames();

	run_conversion_shader();
}
void GAVTexture::update_from_sw(const AVFramePtr &frame) {
	if (!setup_pipeline(static_cast<enum AVPixelFormat>(frame->format))) { //frames->sw_format)) {
		UtilityFunctions::printerr("failed to setup render pipeline");
		return;
	}

	for (int i = 0; i < num_planes; i++) {
		auto plane = planes[i];
		auto info = plane_infos[i];

		// const auto byte_size = frame->linesize[0] * info.height;
		// UtilityFunctions::print(frame->linesize[0], " ", info.line_size);
		if (plane_buffers[i].size() < info.byte_size) {
			plane_buffers[i].resize(info.byte_size);
		}

		auto src = frame->data[i];

		// we expect tight lines without extra, but line_size of the frame can have additional padding. check for it
		const auto line_size = frame->linesize[i];
		// UtilityFunctions::print(info.line_size, " -- ", frame->linesize[i]);
		if (line_size == info.line_size) {
			memcpy(plane_buffers[i].ptrw(), src, info.byte_size);
		} else {
			if (plane_buffers[i].size() < info.byte_size) {
				plane_buffers[i].resize(info.byte_size);
			}
			// copy line for line
			for (size_t y = 0; y < info.height; y++) {
				memcpy(plane_buffers[i].ptrw() + info.line_size * y, src + line_size * y, info.line_size);
			}
		}
		rd->texture_update(plane, 0, plane_buffers[i]);
	}
	run_conversion_shader();
}

void GAVTexture::update_from_hw(const AVFramePtr &hw_frame) {
	// auto *frames = reinterpret_cast<AVHWFramesContext *>(codec_ctx->hw_frames_ctx->data);

	if (!conversion_frame) {
		conversion_frame = av_frame_ptr();
	}

	// AVPixelFormat* formats = nullptr;
	// av_hwframe_transfer_get_formats(hw_frame->hw_frames_ctx, AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0);
	// for (AVPixelFormat *p = formats;
	// 			*p != AV_PIX_FMT_NONE; p++) {
	// 	UtilityFunctions::print(av_get_pix_fmt_name(*p));
	// }

	// UtilityFunctions::print("Updating texture");
	// UtilityFunctions::print("width = ", hw_frame->width);
	// UtilityFunctions::print("height = ", hw_frame->height);
	// UtilityFunctions::print("format = ", av_get_pix_fmt_name((AVPixelFormat)hw_frame->format));

	if (!hw_frame->hw_frames_ctx) {
		update_from_sw(hw_frame);
		return;
	}

	if (!ff_ok(av_hwframe_transfer_data(conversion_frame.get(), hw_frame.get(), 0))) {
		UtilityFunctions::printerr("Could not transfer_data from hw to sw");
		return;
	}

	update_from_sw(conversion_frame);
}
