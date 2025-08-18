//
// Created by phwhitfield on 06.08.25.
//

#include "godot_cpp/classes/rd_shader_file.hpp"
#include "godot_cpp/classes/rendering_server.hpp"
#include "shaders.h"
#include "vk_ctx.h"

#include <libavutil/hwcontext_vulkan.h>
#include <vulkan/vulkan_core.h>
extern "C" {
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
	texture.instantiate();
}
GAVTexture::~GAVTexture() {
	for (auto &b : planes) {
		if (b.is_valid()) {
			rd->free_rid(b);
			b = RID();
		}
	}
}

Ref<Texture2DRD> GAVTexture::setup(AVCodecContext *_ctx, RenderingDevice *_rd) {
	if (texture.is_valid() && texture->get_texture_rd_rid().is_valid()) {
		UtilityFunctions::print("TODO cleanup old texture");
	}
	codec_ctx = _ctx;
	rd = _rd;

	// rd->compute_pipeline_create()

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

	auto *frames = reinterpret_cast<AVHWFramesContext *>(codec_ctx->hw_frames_ctx->data);
	auto width = codec_ctx->width;
	auto height = codec_ctx->height;
	UtilityFunctions::print("create texture of size ", width, "x", height);

	auto pixel_format = frames->sw_format;

	UtilityFunctions::print("frame format is: ", av_get_pix_fmt_name(pixel_format));
	std::array<int, 4> line_sizes{ 0, 0, 0, 0 };
	std::array<size_t, 4> plane_sizes{ 0, 0, 0, 0 };
	// for (auto &plane : plane_sizes) {
	// 	plane = 0;
	// };
	av_image_fill_linesizes(line_sizes.data(), pixel_format, codec_ctx->width);
	av_image_fill_plane_sizes(plane_sizes.data(), pixel_format, codec_ctx->height, reinterpret_cast<const ptrdiff_t *>(line_sizes.data()));

	//

	// create texture buffers, maybe we could use the ones provided by ffmpeg directly
	// but godot does probably not allow mixing with native vulkan types and we don't really want to write the compute shader in vulkan at this point
	String plane_sizes_str = "Plane sizes: ";
	for (int i = 0; i < planes.size(); i++) {
		auto byte_size = plane_sizes[i];
		auto line_size = line_sizes[i];
		if (byte_size > 0) {
			plane_sizes_str += " | bytes: " + String::num_int64(byte_size) + " - line: " + String::num_int64(line_size);

			// create texture planes
			int w = line_size;
			int h = byte_size / line_size;
			plane_infos[i] = {
				w,
				h,
				1
			};
			// UtilityFunctions::print("Create plane buffer ", i, "  ", w, "x", h, " = ", plane_sizes[i], " bytes");
			// // planes[i] = rd->texture_buffer_create(byte_size, RenderingDevice::DATA_FORMAT_R8_UINT);
			// planes[i] = rd->storage_buffer_create(byte_size, PackedByteArray());
			// if (!planes[i].is_valid()) {
			// 	UtilityFunctions::printerr("Failed to create plane buffer ", i);
			// }

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
					// RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
					RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
			format->set_format(RenderingDevice::DATA_FORMAT_R8_UNORM);

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

	UtilityFunctions::print("number of planes: ", num_planes, "  ", plane_sizes_str);

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
			// RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
			RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
	// format->set_array_layers(2);
	format->set_format(RenderingDevice::DATA_FORMAT_R8G8B8A8_UNORM);

	// chroma planes are half the size of the luma plane
	// width = i == 0 || i == 3 ? frame_size.width : Math::ceil(frame_size.width / 2.0f);
	// new_format.height = i == 0 || i == 3 ? frame_size.height : Math::ceil(frame_size.height / 2.0f);

	auto view = memnew(RDTextureView);

	const auto tex_rd = rd->texture_create(format, view);
	if (!tex_rd) {
		UtilityFunctions::printerr("Could not create texture");
		return texture;
	}

	texture->set_texture_rd_rid(tex_rd);

	// // create the compute shader
	// auto shader = yuv420(rd, { width, height });
	// conversion_shader = rd->compute_pipeline_create(shader);
	// if (!conversion_shader) {
	// 	UtilityFunctions::printerr("Could not create conversion shader");
	// }

	return texture;
}
void GAVTexture::update(AVFrame *frame) const {
	// UtilityFunctions::print("update texture");

	auto *hw_dev = reinterpret_cast<AVHWDeviceContext *>(codec_ctx->hw_device_ctx->data);

	// auto pixel_format = static_cast<AVPixelFormat>(frame->);
	// UtilityFunctions::print("update frame format is: ", av_get_pix_fmt_name(pixel_format));
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

	auto *vk_frames_ctx = static_cast<AVVulkanFramesContext *>(frames->hwctx);

	auto vk_queue = reinterpret_cast<VkQueue>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, {}, 0));

	// that can be allocated out of it.
	VkCommandPoolCreateInfo cmd_pool_info = {};
	cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmd_pool_info.pNext = nullptr;
	cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmd_pool_info.queueFamilyIndex = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_QUEUE_FAMILY, {}, 0);

	VkCommandPool cmd_pool;
	if (vkf->create_command_pool(vk->act_dev, &cmd_pool_info, nullptr, &cmd_pool) != VK_SUCCESS) {
		UtilityFunctions::printerr("Couldn't create command pool");
		return;
	}

	// Populate a command buffer info struct with a reference to the command pool from which the memory for the command buffer is taken.
	// Notice the "level" parameter which ensures these will be primary command planes.
	VkCommandBufferAllocateInfo cmd_buf_info = {};
	cmd_buf_info.pNext = nullptr;
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = cmd_pool;
	cmd_buf_info.commandBufferCount = 1;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VkCommandBuffer cmd_buf;

	if (vkf->allocate_command_buffers(vk->act_dev, &cmd_buf_info, &cmd_buf) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not allocate command planes!");
		return;
	}

	auto *vk_frame = reinterpret_cast<AVVkFrame *>(frame->data[0]);

	vk_frames_ctx->lock_frame(frames, vk_frame);

	auto cleanup = [&] {
		UtilityFunctions::print("cleanup");
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
		cleanup();
		return;
	}



	for (int i = 0; i < num_planes; i++) {
		// // TODO: do we need to wait for semaphores?
		// if (vk_frame->sem[i] == VK_NULL_HANDLE || !vk_frame->sem_value[i]) {
		// 	// 	// vkQueueSubmit(wait = sem[img_index], value = sem_value[img_index])
		// 	// 	// use the sem on submit below
		// 	// 	// UtilityFunctions::print("should acquire frame");
		// 	UtilityFunctions::printerr("Could not acquire Vulkan Semaphore!");
		// }

		// planes are always in the same image? as layers?
		const auto ffmpeg_img = vk_frame->img[0];
		const auto info = plane_infos[i];
		UtilityFunctions::print("Copy plane ", i, " - ", info.width, "x", info.height);

		// const auto godot_img = reinterpret_cast<VkBuffer>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_BUFFER, planes[i], 0));
		if (!planes[i].is_valid()) {
			UtilityFunctions::printerr("godot image ", i, " is not valid");
			cleanup();
			return;
		}

		if (!ffmpeg_img) {
			UtilityFunctions::printerr("ffmpeg image ", i, " is not valid");
			cleanup();
			return;
		}

		// use buffer
		// auto godot_img = reinterpret_cast<VkBuffer>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_BUFFER, planes[i], 0));
		// if (!godot_img) {
		// 	UtilityFunctions::printerr("godot image is not valid");
		// 	cleanup();
		// 	return;
		// }

		// VkBufferImageCopy region;
		// region.bufferOffset = 0;
		// region.bufferRowLength = info.width;
		// region.bufferImageHeight = info.height;
		// region.imageOffset.x = region.imageOffset.y = region.imageOffset.z = i;
		// region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		// region.imageSubresource.mipLevel = 0;
		// region.imageSubresource.baseArrayLayer = 0;
		// region.imageSubresource.layerCount = 1;
		// region.imageExtent.width = info.width;
		// region.imageExtent.height = info.height;
		// region.imageExtent.depth = 1;
		//
		// vkf->copy_image_to_buffer(cmd_buf, ffmpeg_img, vk_frame->layout[i], godot_img, 1, &region);

		// use image
		auto godot_img = reinterpret_cast<VkImage>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, planes[i], 0));
		if (!godot_img) {
			UtilityFunctions::printerr("godot image is not valid");
			cleanup();
			return;
		}

		auto flag = VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT;
		if (i == 1) {
			flag = VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT;
		}else if (i == 2) {
			flag = VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT;
		}

		VkImageCopy region;
		region.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT & flag;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = 0;
		region.srcSubresource.layerCount = 1;
		region.srcOffset.x = 0;
		region.srcOffset.y = 0;
		region.srcOffset.z = 0;
		region.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		region.dstSubresource.mipLevel = 0;
		region.dstSubresource.baseArrayLayer = 0;
		region.dstSubresource.layerCount = 1;
		region.dstOffset.x = 0;
		region.dstOffset.y = 0;
		region.dstOffset.z = 0;
		region.extent.width = info.width;
		region.extent.height = info.height;
		region.extent.depth = 1;
		vkf->cmd_copy_image(cmd_buf, ffmpeg_img, vk_frame->layout[0], godot_img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	}

	if (vkf->end_command_buffer(cmd_buf) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not end command buffer");
		cleanup();
		return;
	}

	UtilityFunctions::print("end cmd buffer");

	// for (int i = 0; i < num_planes; i++) {
	// 	if (vk_frame->sem[i] == VK_NULL_HANDLE || !vk_frame->sem_value[i]) {
	// 			// vkQueueSubmit(wait = sem[img_index], value = sem_value[img_index])
	// 		// 	// use the sem on submit below
	// 		// 	// UtilityFunctions::print("should acquire frame");
	// 		UtilityFunctions::printerr("Could not acquire Vulkan Semaphore!");
	// 	}
	// }

	VkTimelineSemaphoreSubmitInfo timeline_info;
	timeline_info.sType = VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO;
	timeline_info.pNext = nullptr;
	timeline_info.waitSemaphoreValueCount = 1;
	timeline_info.pWaitSemaphoreValues = vk_frame->sem_value;
	// timelineInfo1.pSignalSemaphoreValues = vk_frame->sem_value;

	std::array<VkPipelineStageFlags, 4> stageFlags = {VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT};

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
		cleanup();
		return;
	}

	UtilityFunctions::print("done");

	// TODO: free barrier
	// if (vk_frame->sem[img_index] != VK_NULL_HANDLE) {
	// 	// UtilityFunctions::print("should release frame");
	// 	// vk_frame->sem_value[img_index] += 1;
	// 	// vkQueueSubmit(signal = sem[img_index], value = sem_value[img_index]);
	// }

	// Release the image back to FFmpeg.
	cleanup();

	// run the conversion shader

	// vk_frames_ctx->unlock_frame(frames, vk_frame);
	// UtilityFunctions::print("update texture done");

	// // Acquire the image from FFmpeg.

	//
	// VkCommandBufferBeginInfo cmd_buf_begin;
	// cmd_buf_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	// cmd_buf_begin.pNext = nullptr;
	// cmd_buf_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	// cmd_buf_begin.pInheritanceInfo = nullptr;
	// vkBeginCommandBuffer(cmd_buf, &cmd_buf_begin);
	//
	// const auto rid = RenderingServer::get_singleton()->texture_get_rd_texture(texture->get_rid());
	// const auto vk_image = reinterpret_cast<VkImage>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, rid, 0));
	//
	// // vkCmdCopyBufferToImage(cmd_buf, vk_frame->)
	// // Do a image copy to part of the dst image - checks should stay small
	// VkImageCopy cregion;
	// cregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	// cregion.srcSubresource.mipLevel = 0;
	// cregion.srcSubresource.baseArrayLayer = 0;
	// cregion.srcSubresource.layerCount = 1;
	// cregion.srcOffset.x = 0;
	// cregion.srcOffset.y = 0;
	// cregion.srcOffset.z = 0;
	// cregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	// cregion.dstSubresource.mipLevel = 0;
	// cregion.dstSubresource.baseArrayLayer = 0;
	// cregion.dstSubresource.layerCount = 1;
	// cregion.dstOffset.x = 0;
	// cregion.dstOffset.y = 0;
	// cregion.dstOffset.z = 0;
	// cregion.extent.width = frame->width;
	// cregion.extent.height = frame->height;
	// cregion.extent.depth = 1;
	//
	// vkf->cmd_copy_image(cmd_buf, vk_frame->img[img_index], vk_frame->layout[img_index], vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cregion);
	//
	// if (vkEndCommandBuffer(cmd_buf) != VK_SUCCESS) {
	// 	UtilityFunctions::printerr("Couldn't copy image!");
	// }
	//

	//
	// VkSemaphoreWaitInfo waitInfo;
	// waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO;
	// waitInfo.pNext = NULL;
	// waitInfo.flags = 0;
	// waitInfo.semaphoreCount = 1;
	// waitInfo.pSemaphores = &vk_frame->sem[img_index];
	// waitInfo.pValues = &vk_frame->sem_value[img_index];
	// // vkWaitSemaphores(vk->act_dev, &waitInfo, UINT64_MAX);
	//
	// VkSubmitInfo vsi;
	// vsi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	// // vsi.pNext = &timelineInfo1;
	// vsi.pNext = nullptr;
	// vsi.commandBufferCount = 1;
	// vsi.pCommandBuffers = &cmd_buf;
	//
	// vsi.waitSemaphoreCount = 0;
	// vsi.pWaitSemaphores = nullptr; //vk_frame->sem;
	// vsi.signalSemaphoreCount = 0;
	// vsi.pSignalSemaphores = nullptr; //vk_frame->sem; //->sem_value;
	// vsi.pWaitDstStageMask = nullptr;
	//
	// if (auto result = vkf->queue_submit(vk_queue, 1, &vsi, VK_NULL_HANDLE); result != VK_SUCCESS) {
	// 	UtilityFunctions::printerr("Could not submit queue!", result);
	// } else {
	// 	// UtilityFunctions::print("Submitted frame!");
	// }

	// Unblock thread3's device work.
	// vk_frame->sem_value[img_index] += 1;
	// VkSemaphoreSignalInfo signalInfo;
	// signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO;
	// signalInfo.pNext = nullptr;
	// signalInfo.semaphore = vk_frame->sem[img_index];
	// signalInfo.value = vk_frame->sem_value[img_index];

	// vkSignalSemaphore(vk->act_dev, &signalInfo);

	// VkImageMemoryBarrier prePresentBarrier = {};
	// prePresentBarrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	// prePresentBarrier.pNext = NULL;
	// prePresentBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	// prePresentBarrier.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;
	// prePresentBarrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	// prePresentBarrier.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
	// prePresentBarrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	// prePresentBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	// prePresentBarrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	// prePresentBarrier.subresourceRange.baseMipLevel = 0;
	// prePresentBarrier.subresourceRange.levelCount = 1;
	// prePresentBarrier.subresourceRange.baseArrayLayer = 0;
	// prePresentBarrier.subresourceRange.layerCount = 1;
	// prePresentBarrier.image = info.planes[info.current_buffer].image;
	// vkCmdPipelineBarrier(info.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
	// 					 &prePresentBarrier);
	//https://docs.vulkan.org/spec/latest/chapters/copies.html#copies-images
}
