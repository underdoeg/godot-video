#include "av_video.h"

#include <algorithm>
#include <thread>

extern "C" {
#include <libavcodec/avcodec.h>

#include <libswscale/swscale.h>

#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>

#include <libavformat/avformat.h>

#include <libavfilter/avfilter.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext.h>
#include <libavutil/hwcontext_vulkan.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
// #include <libavfilter/vulkan_filter.h>
}

using namespace godot;

int counter = 0;

static Ref<Texture2DRD> texture;

static AVFilterContext *buffersink_ctx;
static AVFilterContext *buffersrc_ctx;
static AVFilterGraph *filter_graph;
static bool has_filter_graph = false;

void process_hardware_frame(AVCodecContext *ctx, AVFrame *av_frame) {
	// UtilityFunctions::print("frame");
	// AVFrame *cpu_frame = av_frame_alloc();
	// if (!cpu_frame) {
	// 	std::cerr << "Could not allocate CPU frame\n";
	// 	return;
	// }

	auto *frames = reinterpret_cast<AVHWFramesContext *>(ctx->hw_frames_ctx->data);

	av_frame->pts = av_frame->best_effort_timestamp;

	auto av_frame_converted = av_frame_alloc();

	if (has_filter_graph) {
		/* push the decoded frame into the filtergraph */
		if (av_buffersrc_add_frame_flags(buffersrc_ctx, av_frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
			UtilityFunctions::printerr("Error filtering frame");
			return;
		}

		/* pull filtered frames from the filtergraph */
		while (true) {
			int ret = av_buffersink_get_frame(buffersink_ctx, av_frame_converted);
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				break;
			if (ret < 0) {
				UtilityFunctions::printerr("Error filtering frame");
			}
			// display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
			// av_frame_unref(filt_frame);
		}
	} else {
		av_frame_converted = av_frame;
	}
	// av_frame_unref(frame);

	if (!texture->get_texture_rd_rid()) {
		UtilityFunctions::print("create texture");
		UtilityFunctions::print("frame format is: ", av_get_pix_fmt_name(frames->sw_format));
		const auto fmt = frames->sw_format; //AV_PIX_FMT_NV12; //AV_PIX_FMT_YUV420P;
		int line_sizes[4];
		size_t plane_sizes[4];
		// const auto descr = av_pix_fmt_desc_get(fmt);
		// UtilityFunctions::print(descr->nb_components);
		// for (int i = 0; i < 4; i++) {
		// UtilityFunctions::print(descr->comp[i].plane);
		// }
		av_image_fill_linesizes(line_sizes, fmt, av_frame->width);
		av_image_fill_plane_sizes(plane_sizes, fmt, av_frame->height, reinterpret_cast<const ptrdiff_t *>(line_sizes));
		// check planes
		String plane_sizes_str = "Plane sizes: ";
		for (int i = 0; i < 4; i++) {
			plane_sizes_str += String::num_int64(plane_sizes[i]) + ", ";
		}
		UtilityFunctions::print(plane_sizes_str);
		// create texture
		auto rd = RenderingServer::get_singleton()->get_rendering_device();

		Ref<RDTextureFormat> format;
		format.instantiate();
		format->set_width(av_frame->width);
		format->set_height(av_frame->height);
		format->set_mipmaps(1);
		format->set_texture_type(RenderingDevice::TEXTURE_TYPE_2D);
		format->set_usage_bits(
				RenderingDevice::TEXTURE_USAGE_SAMPLING_BIT |
				RenderingDevice::TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
				// RenderingDevice::TEXTURE_USAGE_CAN_COPY_FROM_BIT |
				// RenderingDevice::TEXTURE_USAGE_STORAGE_BIT |
				RenderingDevice::TEXTURE_USAGE_CAN_UPDATE_BIT);
		// format->set_array_layers(2);
		format->set_format(RenderingDevice::DATA_FORMAT_R8_UNORM);

		// chroma planes are half the size of the luma plane
		// width = i == 0 || i == 3 ? frame_size.width : Math::ceil(frame_size.width / 2.0f);
		// new_format.height = i == 0 || i == 3 ? frame_size.height : Math::ceil(frame_size.height / 2.0f);

		Ref<RDTextureView> view;
		view.instantiate();

		const auto tex_rd = rd->texture_create(format, view);
		if (!tex_rd) {
			UtilityFunctions::printerr("Could not create texture");
			return;
		}

		texture->set_texture_rd_rid(tex_rd);
	}

	// create a texture

	auto *hw_dev = reinterpret_cast<AVHWDeviceContext *>(ctx->hw_device_ctx->data);
	// const VkFormat *fmts = nullptr;
	// VkImageAspectFlags aspects;
	// VkImageUsageFlags usage;
	// int nb_images;

	// int ret = av_vkfmt_from_pixfmt2(hwdev, active_upload_pix_fmt,
	// 		VK_IMAGE_USAGE_SAMPLED_BIT, &fmts,
	// 		&nb_images, &aspects, &usage);

	auto *vk = static_cast<AVVulkanDeviceContext *>(hw_dev->hwctx);

	auto *vk_frames_ctx = static_cast<AVVulkanFramesContext *>(frames->hwctx);

	auto rd = RenderingServer::get_singleton()->get_rendering_device();

	auto vk_queue = reinterpret_cast<VkQueue>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, {}, 0));

	// that can be allocated out of it.
	VkCommandPoolCreateInfo cmd_pool_info = {};
	cmd_pool_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	cmd_pool_info.pNext = nullptr;
	cmd_pool_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	cmd_pool_info.queueFamilyIndex = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_QUEUE_FAMILY, {}, 0);

	VkCommandPool cmd_pool;
	if (vkCreateCommandPool(vk->act_dev, &cmd_pool_info, nullptr, &cmd_pool) != VK_SUCCESS) {
		UtilityFunctions::printerr("Couldn't create command pool");
		return;
	}

	// Create the actual command pool.
	// debugAssertFunctionResult(vk::CreateCommandPool(appManager.device, &commandPoolInfo, nullptr, &appManager.commandPool), "Command Pool Creation");

	// Resize the vector to have a number of elements equal to the number of swapchain images.
	// appManager.cmdBuffers.resize(appManager.swapChainImages.size());

	// Populate a command buffer info struct with a reference to the command pool from which the memory for the command buffer is taken.
	// Notice the "level" parameter which ensures these will be primary command buffers.
	VkCommandBufferAllocateInfo cmd_buf_info = {};
	cmd_buf_info.pNext = nullptr;
	cmd_buf_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	cmd_buf_info.commandPool = cmd_pool;
	cmd_buf_info.commandBufferCount = 1;
	cmd_buf_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;

	VkCommandBuffer cmd_buf;

	if (vkAllocateCommandBuffers(vk->act_dev, &cmd_buf_info, &cmd_buf) != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not allocate command buffers!");
		return;
	};

	auto *vk_frame = reinterpret_cast<AVVkFrame *>(av_frame_converted->data[0]);

	const auto img_index = 0;

	vk_frames_ctx->lock_frame(frames, vk_frame);

	// Acquire the image from FFmpeg.
	if (vk_frame->sem[img_index] == VK_NULL_HANDLE || !vk_frame->sem_value[img_index]) {
		// vkQueueSubmit(wait = sem[img_index], value = sem_value[img_index])
		// use the sem on submit below
		// UtilityFunctions::print("should acquire frame");
		UtilityFunctions::printerr("Could not acquire Vulkan Semaphore!");
	}

	// cmd->image_barrier(
	// 	*wrapped_image,
	// 	vk_frame->layout[img_index],
	// 	VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
	// 	VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT /* sem wait stage */, 0,
	// 	VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
	// 	VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
	VkCommandBufferBeginInfo cmd_buf_begin;
	cmd_buf_begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
	cmd_buf_begin.pNext = nullptr;
	cmd_buf_begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
	cmd_buf_begin.pInheritanceInfo = nullptr;
	vkBeginCommandBuffer(cmd_buf, &cmd_buf_begin);

	const auto rid = RenderingServer::get_singleton()->texture_get_rd_texture(texture->get_rid());
	const auto vk_image = reinterpret_cast<VkImage>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TEXTURE, rid, 0));

	// vkCmdCopyBufferToImage(cmd_buf, vk_frame->)
	// Do a image copy to part of the dst image - checks should stay small
	VkImageCopy cregion;
	cregion.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	cregion.srcSubresource.mipLevel = 0;
	cregion.srcSubresource.baseArrayLayer = 0;
	cregion.srcSubresource.layerCount = 1;
	cregion.srcOffset.x = 0;
	cregion.srcOffset.y = 0;
	cregion.srcOffset.z = 0;
	cregion.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	cregion.dstSubresource.mipLevel = 0;
	cregion.dstSubresource.baseArrayLayer = 0;
	cregion.dstSubresource.layerCount = 1;
	cregion.dstOffset.x = 0;
	cregion.dstOffset.y = 0;
	cregion.dstOffset.z = 0;
	cregion.extent.width = av_frame_converted->width;
	cregion.extent.height = av_frame_converted->height;
	cregion.extent.depth = 1;
	vkCmdCopyImage(cmd_buf, vk_frame->img[img_index], vk_frame->layout[img_index], vk_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cregion);

	if (vkEndCommandBuffer(cmd_buf) != VK_SUCCESS) {
		UtilityFunctions::printerr("Couldn't copy image!");
	}

	VkSubmitInfo vsi;
	vsi.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	vsi.pNext = nullptr;
	vsi.commandBufferCount = 1;
	vsi.pCommandBuffers = &cmd_buf;
	vsi.waitSemaphoreCount = 1;
	vsi.pWaitSemaphores = vk_frame->sem;
	vsi.signalSemaphoreCount = 0;
	vsi.pSignalSemaphores = nullptr;
	vsi.pWaitDstStageMask = nullptr;

	//result = vkQueueSubmit( Queue, 1, IN &vsi, VK_NULL_HANDLE );
	if (auto result = vkQueueSubmit(vk_queue, 1, &vsi, nullptr); result != VK_SUCCESS) {
		UtilityFunctions::printerr("Could not submit queue!", result);
	} else {
		// UtilityFunctions::print("Submitted frame!");
	}

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
	// prePresentBarrier.image = info.buffers[info.current_buffer].image;
	// vkCmdPipelineBarrier(info.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, NULL, 0, NULL, 1,
	// 					 &prePresentBarrier);
	//https://docs.vulkan.org/spec/latest/chapters/copies.html#copies-images

	// Release the image back to FFmpeg.
	if (vk_frame->sem[img_index] != VK_NULL_HANDLE) {
		// UtilityFunctions::print("should release frame");
		// vk_frame->sem_value[img_index] += 1;
		// vkQueueSubmit(signal = sem[img_index], value = sem_value[img_index]);
	}
	vk_frames_ctx->unlock_frame(frames, vk_frame);
	// av_frame_free(&av_frame_converted);

	// static int frame_num = 0;
	// frame_num++;

	// auto *hwdev = reinterpret_cast<AVHWDeviceContext *>(hw.device->data);
	// const VkFormat *fmts = nullptr;
	// VkImageAspectFlags aspects;
	// VkImageUsageFlags usage;
	// int nb_images;

	// int ret = av_vkfmt_from_pixfmt2(hwdev, active_upload_pix_fmt,
	// 								VK_IMAGE_USAGE_SAMPLED_BIT, &fmts,
	// 								&nb_images, &aspects, &usage);

	// // Transfer data from GPU (hw_frame) to CPU (cpu_frame)
	// if (av_hwframe_transfer_data(cpu_frame, hw_frame, 0) < 0) {
	// 	std::cerr << "Error transferring the frame data to CPU\n";
	// 	av_frame_free(&cpu_frame);
	// 	return;
	// }

	// https://themaister.net/blog/2023/01/05/vulkan-video-shenanigans-ffmpeg-radv-integration-experiments/

	// UtilityFunctions::print(cpu_frame->format == AV_PIX_FMT_NV12);

	// for (int i = 0; i < cpu_frame->height; ++i) {
	// 	image.write(reinterpret_cast<const char*>(cpu_frame->data[img_index] + (i * cpu_frame->linesize[img_index])), cpu_frame->width);
	// }

	// Image::create_from_data(cpu_frame->width, cpu_frame->height, cpu_frame->format,)
	// if (frame_num == 5) {
	// 	UtilityFunctions::print(rd->texture_get_data(rid, 1).size());
	// 	// Image::create_from_data(3840, 2160, false, Image::FORMAT_R8, rd->texture_get_data(rid, 1))->save_jpg("layer1.jpg");
	// 	// texture->get_image()->save_jpg("test" + String::num_int64(frame_num) + ".jpg");
	// }
	// UtilityFunctions::print(cpu_frame->width, " -- ", cpu_frame->height);
}

Ref<Texture2D> AvVideo::get_texture() {
	return texture;
}

void AvVideo::_bind_methods() {
	ClassDB::bind_method(D_METHOD("play"), &AvVideo::play);
	ClassDB::bind_method(D_METHOD("get_texture"), &AvVideo::get_texture);
}

// taken from https://github.com/Willieodwyer/ffmpeg-vulkan

// static int set_hwframe_ctx(AVCodecContext *ctx, AVBufferRef *hw_device_ctx, AVPixelFormat pix_fmt, int width,
// 		int height) {
// 	AVBufferRef *hw_frames_ref = av_hwframe_ctx_alloc(hw_device_ctx);
// 	if (!hw_frames_ref) {
// 		fprintf(stderr, "Failed to create hardware frame context.\n");
// 		return -1;
// 	}
//
// 	UtilityFunctions::print("Init hwframe context");
//
// 	AVHWFramesContext *frames_ctx = (AVHWFramesContext *)(hw_frames_ref->data);
// 	frames_ctx->format = pix_fmt; // Hardware pixel format
// 	frames_ctx->sw_format = AV_PIX_FMT_YUV420P; // Software pixel format for transfer
// 	frames_ctx->width = width;
// 	frames_ctx->height = height;
// 	frames_ctx->initial_pool_size = 20;
//
// 	if (av_hwframe_ctx_init(hw_frames_ref) < 0) {
// 		fprintf(stderr, "Failed to initialize hardware frame context.\n");
// 		av_buffer_unref(&hw_frames_ref);
// 		return -1;
// 	}
//
// 	ctx->hw_frames_ctx = av_buffer_ref(hw_frames_ref);
// 	if (!ctx->hw_frames_ctx) {
// 		av_buffer_unref(&hw_frames_ref);
// 		return AVERROR(ENOMEM);
// 	}
//
// 	av_buffer_unref(&hw_frames_ref);
// 	return 0;
// }

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL get_it(
		VkInstance instance,
		const char *pName) {
	// auto ret = vkGetInstanceProcAddr(instance, pName);
	// UtilityFunctions::print("get_it: ", pName, " - ", ret != nullptr);
	// return ret;
	return vkGetInstanceProcAddr(instance, pName);
}

static AVCodecContext *OpenVideoStream(AVFormatContext *fmt_ctx, int stream_idx, AVHWDeviceType device_type) {
	AVCodecParameters *codecpar = fmt_ctx->streams[stream_idx]->codecpar;
	const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
	if (!decoder) {
		std::cerr << "Decoder not found\n";
		return nullptr;
	}

	// Print codec information
	std::cout << "Video codec: " << avcodec_get_name(codecpar->codec_id) << std::endl;
	std::cout << "Width: " << codecpar->width << " Height: " << codecpar->height << std::endl;
	std::cout << "Bitrate: " << codecpar->bit_rate << std::endl;

	AVCodecContext *codec_ctx;
	codec_ctx = avcodec_alloc_context3(nullptr);
	if (!codec_ctx) {
		printf("avcodec_alloc_context3 failed");
		return nullptr;
	}

	int result = avcodec_parameters_to_context(codec_ctx, fmt_ctx->streams[stream_idx]->codecpar);
	if (result < 0) {
		char error_str[1024];
		av_make_error_string(error_str, sizeof(error_str), result);
		printf("avcodec_parameters_to_context failed: %s\n", error_str);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}
	codec_ctx->pkt_timebase = fmt_ctx->streams[stream_idx]->time_base;

	// if (device_type != AV_HWDEVICE_TYPE_NONE) {
	/* Look for supported hardware accelerated configurations */
	int i = 0;
	const AVCodecHWConfig *accel_config = nullptr;
	{
		const AVCodecHWConfig *config = nullptr;
		while ((config = avcodec_get_hw_config(decoder, i++)) != nullptr) {
			printf("Found %s hardware acceleration with pixel format %s\n", av_hwdevice_get_type_name(config->device_type),
					av_get_pix_fmt_name(config->pix_fmt));

			if (config->device_type != device_type || !(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
				continue;
			}
			accel_config = config;
		}
	}

	if (!accel_config) {
		std::cerr << "Unable to locate hw acceleration type: " << av_hwdevice_get_type_name(device_type) << std::endl;
		return nullptr;
	}

	// result = av_hwdevice_ctx_create(&codec_ctx->hw_device_ctx, accel_config->device_type, NULL, NULL, 0);
	// if (result < 0) {
	// 	char error_str[1024];
	// 	av_make_error_string(error_str, sizeof(error_str), result);
	// 	printf("Couldn't create %s hardware device context: %s", av_hwdevice_get_type_name(accel_config->device_type),
	// 			error_str);
	// } else {
	// 	printf(" -- Using %s hardware acceleration with pixel format %s\n",
	// 			av_hwdevice_get_type_name(accel_config->device_type), av_get_pix_fmt_name(accel_config->pix_fmt));
	// }

	// from here https://github.com/Themaister/Granite/blob/master/video/ffmpeg_hw_device.cpp

	UtilityFunctions::print("allocating HW device: ", av_hwdevice_get_type_name(device_type));

	// codec_ctx->hw_device_ctx = av_hwdevice_ctx_alloc(device_type);
	auto hw_dev = av_hwdevice_ctx_alloc(device_type);
	auto hw_ctx = reinterpret_cast<AVHWDeviceContext *>(hw_dev->data);

	// self reference TODO
	// hwctx->user_opaque = this;

	auto *vk = static_cast<AVVulkanDeviceContext *>(hw_ctx->hwctx);
	// vk->get_proc_addr

	auto rd = RenderingServer::get_singleton()->get_rendering_device();

	// static const auto call_me = [&](VkInstance instance,
	// 							const char *pName) {
	// 	UtilityFunctions::print(pName);
	// 	UtilityFunctions::print("\n");
	// 	// return  nullptr;
	// 	return vkGetInstanceProcAddr(instance, pName);
	// };

	vk->get_proc_addr = get_it;
	// vkGetInstanceProcAddr;
	vk->inst = reinterpret_cast<VkInstance>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TOPMOST_OBJECT, {}, 0));
	// vkGetInstanceProcAddr(vk->inst, "vkCmdEncodeVideoKHR");
	// vkGetInstanceProcAddr(vk->inst, "vkGetDeviceProcAddr")
	// UtilityFunctions::print("test: ", );

	vk->act_dev = reinterpret_cast<VkDevice>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, {}, 0));
	vk->phys_dev = reinterpret_cast<VkPhysicalDevice>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE, {}, 0));

	// vk->nb_enabled_dev_extensions =
	// Load instance extensions that are available.
	// list of isntance extensions
	{
		uint32_t instance_extension_count = 0;
		VkResult err = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
		// ERR_FAIL_COND_V(err != VK_SUCCESS && err != VK_INCOMPLETE, ERR_CANT_CREATE);
		// ERR_FAIL_COND_V_MSG(instance_extension_count == 0, ERR_CANT_CREATE, "No instance extensions were found.");

		static Vector<VkExtensionProperties> instance_extensions;
		instance_extensions.resize(instance_extension_count);

		static Vector<const char *> instance_extension_names;
		err = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, instance_extensions.ptrw());
		if (err != VK_SUCCESS && err != VK_INCOMPLETE) {
			UtilityFunctions::printerr("vkEnumerateInstanceExtensionProperties failed");
			return nullptr;
		}

		const auto skip = [](const char *ext) {
			static Vector<const char *> enabled_extensions = {
				// taken from https://github.com/godotengine/godot/blob/2d113cc224cb9be07866d003819fcef2226a52ea/drivers/vulkan/rendering_context_driver_vulkan.cpp#L427
				VK_KHR_SURFACE_EXTENSION_NAME,
				VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
			};
			for (const auto &v : enabled_extensions) {
				if (strcmp(v, ext) == 0) {
					return false;
				}
			}
			return true;
		};

		for (const auto &extension : instance_extensions) {
			if (skip(extension.extensionName)) {
				// UtilityFunctions::print("Skipping instance extension: ", extension.extensionName);
				continue;
			}
			UtilityFunctions::print("Found instance extension:", extension.extensionName);
			instance_extension_names.push_back(extension.extensionName);
		}
		vk->enabled_inst_extensions = instance_extension_names.ptr();
		vk->nb_enabled_inst_extensions = instance_extension_names.size();
	}

	// list of device extensions
	{
		uint32_t device_extension_count = 0;
		VkResult err = vkEnumerateDeviceExtensionProperties(vk->phys_dev, nullptr, &device_extension_count, nullptr);
		// ERR_FAIL_COND_V(err != VK_SUCCESS, ERR_CANT_CREATE);
		// ERR_FAIL_COND_V_MSG(device_extension_count == 0, ERR_CANT_CREATE, "vkEnumerateDeviceExtensionProperties failed to find any extensions\n\nDo you have a compatible Vulkan installable client driver (ICD) installed?");
		if (err != VK_SUCCESS) {
			UtilityFunctions::print("vkEnumerateDeviceExtensionProperties failed");
			return nullptr;
		}

		static Vector<VkExtensionProperties> device_extensions;
		device_extensions.resize(device_extension_count);

		static Vector<const char *> device_extension_names;

		err = vkEnumerateDeviceExtensionProperties(vk->phys_dev, nullptr, &device_extension_count, device_extensions.ptrw());
		if (err != VK_SUCCESS) {
			UtilityFunctions::print("vkEnumerateDeviceExtensionProperties step 2 failed");
			return nullptr;
		}

		const auto skip = [](const char *ext) {
			static Vector<const char *> enabled_extensions = {
				// taken from godot source code
				// https://github.com/godotengine/godot/blob/2d113cc224cb9be07866d003819fcef2226a52ea/drivers/vulkan/rendering_device_driver_vulkan.cpp#L522

				VK_KHR_SWAPCHAIN_EXTENSION_NAME,
				VK_KHR_MULTIVIEW_EXTENSION_NAME,
				VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME,
				VK_EXT_FRAGMENT_DENSITY_MAP_EXTENSION_NAME,
				VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME,
				VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME,
				VK_KHR_STORAGE_BUFFER_STORAGE_CLASS_EXTENSION_NAME,
				VK_KHR_16BIT_STORAGE_EXTENSION_NAME,
				VK_KHR_IMAGE_FORMAT_LIST_EXTENSION_NAME,
				VK_KHR_MAINTENANCE_2_EXTENSION_NAME,
				VK_EXT_PIPELINE_CREATION_CACHE_CONTROL_EXTENSION_NAME,
				VK_EXT_SUBGROUP_SIZE_CONTROL_EXTENSION_NAME,
				VK_EXT_ASTC_DECODE_MODE_EXTENSION_NAME,
				VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME,
				VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME,
				VK_EXT_TEXTURE_COMPRESSION_ASTC_HDR_EXTENSION_NAME,

				// added in fork
				VK_KHR_VIDEO_DECODE_QUEUE_EXTENSION_NAME,
				VK_KHR_VIDEO_DECODE_AV1_EXTENSION_NAME,
				VK_KHR_VIDEO_QUEUE_EXTENSION_NAME,
				VK_KHR_VIDEO_DECODE_H264_EXTENSION_NAME,
				VK_KHR_VIDEO_DECODE_H265_EXTENSION_NAME

				// VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME,
				// VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME,
				// VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
				// VK_EXT_EXTERNAL_MEMORY_HOST_EXTENSION_NAME,
				// VK_EXT_IMAGE_DRM_FORMAT_MODIFIER_EXTENSION_NAME,
				// VK_EXT_DESCRIPTOR_BUFFER_EXTENSION_NAME,
				// VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME
			};
			for (const auto &v : enabled_extensions) {
				if (strcmp(v, ext) == 0) {
					return false;
				}
			}
			return true;
		};

		for (const auto &extension : device_extensions) {
			if (skip(extension.extensionName)) {
				continue;
			}
			UtilityFunctions::print("found device extension:", extension.extensionName);
			device_extension_names.push_back(extension.extensionName);
		}

		vk->enabled_dev_extensions = device_extension_names.ptr();

		vk->nb_enabled_dev_extensions = device_extension_names.size();
		// for (int i = 0; i < vk->nb_enabled_dev_extensions; i++) {
		// 	printf("%s\n", vk->enabled_dev_extensions[i]);
		// }
	}

	{
		// UtilityFunctions::print("load device features");
		// auto f = PFN_vkGetPhysicalDeviceFeatures2KHR(get_it(vk->inst, "vkGetPhysicalDeviceFeatures2KHR"));
		// f(vk->phys_dev, &vk->device_features);

		// auto ff = PFN_vkGetDeviceProcAddr(get_it(vk->inst, "vkGetDeviceProcAddr"));
		// UtilityFunctions::print("vkGetSemaphoreFdKHR", ff(vk->act_dev, "vkGetSemaphoreFdKHR") != nullptr, ff(vk->act_dev, "vkGetSemaphoreFdKHR"));

		// auto ff = PFN_vkGetSemaphoreFdKHR(get_it(vk->inst, "vkGetSemaphoreFdKHR"));
		// if (!ff) {
		// 	UtilityFunctions::printerr("vkCmdEncodeVideoKHR failed");
		// }
		// for (const auto feature:features) {
		// UtilityFunctions::print(feature.sType);
		// }
		// UtilityFunctions::print("features", features.features.geometryShader);
	}

	{
		// uint32_t queueFamilyIndex;
		// uint32_t queueFamilyCount;
		//
		// vkGetPhysicalDeviceQueueFamilyProperties2(vk->phys_dev, &queueFamilyCount, nullptr);

		// VkQueueFamilyProperties2* props = new VkQueueFamilyProperties2[queueFamilyCount];
		// // = calloc(queueFamilyCount,
		// // 	sizeof(VkQueueFamilyProperties2));
		// VkQueueFamilyVideoPropertiesKHR* videoProps = new VkQueueFamilyVideoPropertiesKHR[queueFamilyCount];
		// = calloc(queueFamilyCount,
		// 	sizeof(VkQueueFamilyVideoPropertiesKHR));

		// UtilityFunctions::print("Que family count is ", queueFamilyCount);

		// auto props = Vector<VkQueueFamilyProperties2>();
		// props.resize(queueFamilyCount);
		// auto video_props = Vector<VkQueueFamilyVideoPropertiesKHR>();
		// video_props.resize(queueFamilyCount);
		//
		// for (queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; ++queueFamilyIndex) {
		// 	props.write[queueFamilyIndex].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
		// 	props.write[queueFamilyIndex].pNext = (void*)&video_props[queueFamilyIndex];
		//
		// 	video_props.write[queueFamilyIndex].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_VIDEO_PROPERTIES_KHR;
		// }

		// const auto f = PFN_vkGetPhysicalDeviceQueueFamilyProperties2(get_it(vk->inst, "vkGetPhysicalDeviceQueueFamilyProperties2"));
		// f(vk->phys_dev, &queueFamilyCount, props.ptrw());

		// const auto props = static_cast<VkQueueFamilyProperties2 *>(alloca(
		// 		sizeof(VkQueueFamilyProperties2) * queueFamilyCount));
		//
		// const auto f = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties2>(get_it(vk->inst, "vkGetPhysicalDeviceQueueFamilyProperties2"));
		// f(vk->phys_dev, &queueFamilyCount, props);

		// for (queueFamilyIndex = 0; queueFamilyIndex < queueFamilyCount; ++queueFamilyIndex) {
		// 	if ((props[queueFamilyIndex].queueFamilyProperties.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0 &&
		// 		(video_props[queueFamilyIndex].videoCodecOperations & VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR) != 0) {
		// 		break;
		// 		}
		// }
		vk->nb_qf = 0;

		uint32_t queue_family_properties_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(vk->phys_dev, &queue_family_properties_count, nullptr);
		auto props = Vector<VkQueueFamilyProperties>();
		if (queue_family_properties_count > 0) {
			UtilityFunctions::print("found potential queue families:", queue_family_properties_count);
			props.resize(queue_family_properties_count);
			vkGetPhysicalDeviceQueueFamilyProperties(vk->phys_dev, &queue_family_properties_count, props.ptrw());

			for (auto j = 0; j < queue_family_properties_count; j++) {
				const auto v = props[j];
				// if(v.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {

				VkQueueFlagBits;
				VkQueueFlags;

				vk->qf[vk->nb_qf] = {
					j,
					1,

					// flags taken from here:
					// https://github.com/godotengine/godot/blob/2d113cc224cb9be07866d003819fcef2226a52ea/drivers/vulkan/rendering_device_driver_vulkan.cpp#L1050
					VkQueueFlagBits(v.queueFlags),

					VkVideoCodecOperationFlagBitsKHR(
							VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
							VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR |
							VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
				};
				;
				vk->nb_qf += 1;
				// vk->nb
				// }
			}
		}

		if (vk->nb_qf == 0) {
			UtilityFunctions::print("No queue family for video decoding found");
			return nullptr;
		}
		UtilityFunctions::print("found queue families:", vk->nb_qf);

		for (auto i = 0; i < vk->nb_qf; i++) {
			UtilityFunctions::print("QF: ", vk->qf[i].idx, " -- ", vk->qf[i].flags);
		}

		// if (queueFamilyIndex < queueFamilyCount) {
		// 	// Found appropriate queue family
		// 	UtilityFunctions::print("Found queue families:", queueFamilyCount);
		// } else {
		// 	// Did not find a queue family with the needed capabilities
		// 	UtilityFunctions::print("No queue families found");
		// }
	}

	{
		// auto &q = device->get_queue_info();
		//

		// vk->qf = {};

		// AVVulkanDeviceQueueFamily
		// const auto queue_family_index = rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_QUEUE_FAMILY, {}, 0);
		// UtilityFunctions::print(queue_family_index);
		// const auto queue = reinterpret_cast<VkQueue>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_COMMAND_QUEUE, {}, 1));
		//
		// // UtilityFunctions::print(queue);
		//
		// // // VkQueue queue;
		// // // vkGetDeviceQueue(vk->act_dev, queue_family, queue_index, &queue);
		// AVVulkanDeviceQueueFamily fam = {
		// 	static_cast<int>(queue_family_index),
		// 	1,
		//
		// 	// flags taken from here:
		// 	// https://github.com/godotengine/godot/blob/2d113cc224cb9be07866d003819fcef2226a52ea/drivers/vulkan/rendering_device_driver_vulkan.cpp#L1050
		// 	VkQueueFlagBits(
		// 			VK_QUEUE_GRAPHICS_BIT |
		// 			VK_QUEUE_COMPUTE_BIT |
		// 			VK_QUEUE_TRANSFER_BIT |
		// 			VK_QUEUE_VIDEO_DECODE_BIT_KHR),
		//
		// 	VkVideoCodecOperationFlagBitsKHR(
		// 			VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
		// 			VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR |
		// 			VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
		// };
		//
		// vk->qf[0] = fam;
		// vk->nb_qf = 1;
	}

	UtilityFunctions::print("allocating device context: ", av_hwdevice_get_type_name(device_type));

	// VK_KHR_get_physical_device_properties2 is required to use VK_KHR_driver_properties
	// since it's an instance extension it needs to be enabled before at VkInstance creation time
	// std::vector<const char*> instance_extensions;
	// instance_extensions.push_back(VK_KHR_);

	// VkPhysicalDeviceFeatures features;
	// vkGetPhysicalDeviceFeatures(vk->phys_dev, &features);
	// UtilityFunctions::print("Using device features: ", features.shaderFloat64);

	// vkGetInstanceProcAddr()
	// VK_KHR_video_decode_queue

	// vk->device_features = nullptr;
	// vk->enabled_inst_extensions = device->get_device_features().instance_extensions;
	// vk->nb_enabled_inst_extensions = int(device->get_device_features().num_instance_extensions);
	// vk->enabled_dev_extensions = device->get_device_features().device_extensions;
	// vk->nb_enabled_dev_extensions = int(device->get_device_features().num_device_extensions);
	// vk->qf = nullptr;
	// vk->nb_qf = 0;

	vk->lock_queue = [](AVHWDeviceContext *ctx, uint32_t, uint32_t) {
		// UtilityFunctions::print("locking queue");
		// auto *self = static_cast<Impl *>(ctx->user_opaque);
		// self->device->external_queue_lock();
	};

	vk->unlock_queue = [](AVHWDeviceContext *ctx, uint32_t, uint32_t) {
		// auto *self = static_cast<Impl *>(ctx->user_opaque);
		// UtilityFunctions::print("unlocking queue");
		// self->device->external_queue_unlock();
	};

	if (av_hwdevice_ctx_init(hw_dev) >= 0) {
		UtilityFunctions::print("Created custom Vulkan FFmpeg HW device.");
		// printf(" -- Using %s hardware acceleration with pixel format %s\n",
		// 		av_hwdevice_get_type_name(device_type), av_get_pix_fmt_name(pix_fmt));
	} else {
		UtilityFunctions::printerr("Failed to create Vulkan FFmpeg HW device.");
		return nullptr;
	}

	codec_ctx->hw_device_ctx = hw_dev;
	codec_ctx->pix_fmt = accel_config->pix_fmt;
	// }

	if (codecpar->codec_id == AV_CODEC_ID_VVC) {
		codec_ctx->strict_std_compliance = -2;

		/* Enable threaded decoding, VVC decode is slow */
		codec_ctx->thread_count = 4;
		codec_ctx->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);
	} else
		codec_ctx->thread_count = 1;

	auto frames = av_hwframe_ctx_alloc(codec_ctx->hw_device_ctx);
	if (!frames) {
		UtilityFunctions::printerr("Failed to allocate HW frame context.");
		return nullptr;
	}
	auto *ctx = reinterpret_cast<AVHWFramesContext *>(frames->data);
	ctx->format = accel_config->pix_fmt;
	ctx->width = codecpar->width;
	ctx->height = codecpar->height;
	ctx->sw_format = AV_PIX_FMT_YUV420P;

	if (ctx->format == AV_PIX_FMT_VULKAN) {
		// auto *vk = static_cast<AVVulkanFramesContext *>(ctx->hwctx);
		// vk->img_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
		// // XXX: FFmpeg header type bug.
		// vk->usage = static_cast<VkImageUsageFlagBits>(vk->usage | VK_IMAGE_USAGE_STORAGE_BIT |
		// 		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);

		// h264_encode.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
		//
		// profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
		// profile_info.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		// profile_info.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
		// profile_info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
		// profile_info.pNext = &h264_encode;
		//
		// profile_list_info.pProfiles = &profile_info;
		// profile_list_info.profileCount = 1;
		// vk->create_pnext = &profile_list_info;
	}

	if (av_hwframe_ctx_init(frames) != 0) {
		UtilityFunctions::printerr("Failed to initialize HW frame context.");
		// av_buffer_unref(&frames);
		// return false;
		return nullptr;
	}
	UtilityFunctions::print("Created Vulkan FFmpeg HW device.");
	codec_ctx->hw_frames_ctx = av_buffer_ref(frames);

	// UtilityFunctions::print("frame format is: ", av_get_pix_fmt_name(ctx->sw_format));

	// av_hwframe_ctx_init

	// if (codec_ctx->hw_device_ctx) {
	// 	int err = set_hwframe_ctx(codec_ctx, codec_ctx->hw_device_ctx, codec_ctx->pix_fmt, codecpar->width, codecpar->height);
	// 	if (err < 0) {
	// 		fprintf(stderr, "Failed to set hwframe context.\n");
	// 		return nullptr;
	// 	}
	// } else {
	// 	UtilityFunctions::printerr("Failed to create Vulkan FFmpeg HW device.");
	// }

	result = avcodec_open2(codec_ctx, decoder, NULL);
	if (result < 0) {
		char error_str[1024];
		av_make_error_string(error_str, sizeof(error_str), result);
		printf("Couldn't open codec %s: %s", avcodec_get_name(codec_ctx->codec_id), error_str);
		avcodec_free_context(&codec_ctx);
		return nullptr;
	}

	return codec_ctx;
}

bool DecodeFrame(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt) {
	// UtilityFunctions::print("Decoding frame");
	int ret = avcodec_send_packet(dec_ctx, pkt);
	if (ret < 0) {
		return false;
	}

	// UtilityFunctions::print("Sending a packet for decoding");

	while (ret >= 0) {
		ret = avcodec_receive_frame(dec_ctx, frame);
		if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
			continue;
		}

		// UtilityFunctions::print("Decoding frame returned: ", ret, " = ", av_err2str(ret));

		if (ret < 0) {
			std::cerr << "Error during decoding\n";
			return false;
		}

		// std::cout << "Decoded frame at " << frame->pts << "\n";
		switch (frame->format) {
			// case AV_PIX_FMT_YUV420P:
			// break;
			case AV_PIX_FMT_VAAPI:
			case AV_PIX_FMT_VULKAN:
			case AV_PIX_FMT_VDPAU:
				// if (width > 0 && height > 0)
				//   process_with_scaling(dec_ctx, frame, width, height);
				// else
				process_hardware_frame(dec_ctx, frame);
				return true;
			default:
				UtilityFunctions::printerr("frame is not hardware frame\n");
				// std::cerr << "Unknown pixel format " << av_get_pix_fmt_name((AVPixelFormat)frame->format) << std::endl;
				return false;
		}
	}

	return true;
}

void AvVideo::_process(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	AVPacket *pkt = av_packet_alloc();
	AVFrame *frame = av_frame_alloc();

	if (av_read_frame(fmt_ctx, pkt) >= 0) {
		if (pkt->stream_index == video_stream_index) {
			if (!DecodeFrame(dec_ctx, frame, pkt)) {
				av_packet_unref(pkt);
				// break;
			}
			av_packet_unref(pkt);
		}
	}

	av_frame_free(&frame);

	// DecodeFrame(dec_ctx, frame, nullptr);
}

void AvVideo::play() {
	// thread = std::thread([] {
	AVHWDeviceType hw_type = AV_HWDEVICE_TYPE_VULKAN;
	auto str = av_hwdevice_get_type_name(hw_type);

	// UtilityFunctions::printerr("asdasd");

	std::cout << "Using hw acceleration: " << (str ? str : "none") << std::endl;

	texture.instantiate();

	// texture = ImageTexture::create_from_image(Image::create_empty(3840, 2160, false, Image::FORMAT_RGB8));

	// texture.instantiate();
	// UtilityFunctions::print(texture->get_rid());
	// return ;

	// const auto filename = "input.mp4";

	const auto filename = "8k_h265.mp4";

	if (avformat_open_input(&fmt_ctx, filename, nullptr, nullptr) != 0) {
		std::cerr << "Could not open source file\n";
		return;
	}

	if (avformat_find_stream_info(fmt_ctx, nullptr) < 0) {
		std::cerr << "Could not find stream information\n";
		return;
	}

	// Display some basic information about the file and streams
	std::cout << "Container format: " << fmt_ctx->iformat->name << std::endl;
	std::cout << "Duration: " << fmt_ctx->duration << " microseconds" << std::endl;
	std::cout << "Number of streams: " << fmt_ctx->nb_streams << std::endl;

	for (unsigned i = 0; i < fmt_ctx->nb_streams; i++) {
		if (fmt_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream_index = i;
			break;
		}
	}

	if (video_stream_index == -1) {
		std::cerr << "Could not find a video stream\n";
		return;
	}

	dec_ctx = OpenVideoStream(fmt_ctx, video_stream_index, hw_type);
	if (!dec_ctx) {
		std::cerr << "Failed to open decoder" << std::endl;
		return;
	}

	//// create color transform filter graph
	auto filter_graph = avfilter_graph_alloc();
	if (!filter_graph) {
		UtilityFunctions::printerr("Could not allocate filter graph");
	}

	// const auto scale_filter = avfilter_get_by_name("scale_vulkan");
	// if (!scale_filter) {
	// 	UtilityFunctions::printerr("Could not get scale filter");
	// 	return;
	// }
	// filter_graph = avfilter_graph_alloc();
	//
	// if (!filter_graph) {
	// 	UtilityFunctions::printerr("Could not allocate filter graph");
	// }
	//
	// const auto args = "";
	// AVFilterContext* scale_input;
	// auto ret = avfilter_graph_create_filter(&scale_input, scale_filter, "in", args, nullptr, filter_graph);
	// if (ret < 0) {
	// 	UtilityFunctions::printerr("Could not create scale filter in");
	// }
	//
	//
	// AVFilterContext* scale_output;
	// ret = avfilter_graph_create_filter(&scale_output, scale_filter, "out",
	// 		nullptr, nullptr, filter_graph);
	//
	// if (ret < 0) {
	// 	UtilityFunctions::printerr("Could not create scale filter out");
	// }

	// AVFilterInOut *outputs = avfilter_inout_alloc();
	// AVFilterInOut *inputs = avfilter_inout_alloc();
	//
	// /*
	//  * Set the endpoints for the filter graph. The filter_graph will
	//  * be linked to the graph described by filters_descr.
	//  */
	//
	// /*
	//  * The buffer source output must be connected to the input pad of
	//  * the first filter described by filters_descr; since the first
	//  * filter input label is not specified, it is set to "in" by
	//  * default.
	//  */
	// // buffersrc_ctx->hw_device_ctx = dec_ctx->hw_device_ctx;
	// // buffersink_ctx->hw_device_ctx = dec_ctx->hw_device_ctx;
	//
	// outputs->name = av_strdup("in");
	// outputs->filter_ctx = scale_input;
	// outputs->pad_idx = 0;
	// outputs->next = nullptr;
	//
	// /*
	//  * The buffer sink input must be connected to the output pad of
	//  * the last filter described by filters_descr; since the last
	//  * filter output label is not specified, it is set to "out" by
	//  * default.
	//  */
	// inputs->name = av_strdup("out");
	// inputs->filter_ctx = scale_output;
	// inputs->pad_idx = 0;
	// inputs->next = outputs;

	//
	// AVFilterInOut *outputs = nullptr;
	// AVFilterInOut *inputs = nullptr;
	//
	// auto ret = avfilter_graph_parse_ptr(filter_graph, "scale_vulkan=w=1920:h=1080:scaler=bilinear:format=RGB",
	// 		&inputs, &outputs, nullptr);
	//
	// if (ret < 0) {
	// 	UtilityFunctions::printerr("Could not parse filter graph");
	// }
	//
	// ret = avfilter_graph_config(filter_graph, nullptr);
	// if (ret < 0) {
	// 	UtilityFunctions::printerr("Could not configure filter graph");
	// }
	//
	// UtilityFunctions:print_line("DONE");
	// exit(1);

	const auto make_filter_graph = false;
	if (make_filter_graph) {
		const AVFilter *buffersrc = avfilter_get_by_name("buffer");
		const AVFilter *buffersink = avfilter_get_by_name("buffersink");
		if (!buffersrc || !buffersink) {
			UtilityFunctions::printerr("Could not get buffer src and sink");
		}

		filter_graph = avfilter_graph_alloc();
		if (!filter_graph) {
			UtilityFunctions::printerr("Could not allocate filter graph");
		}

		char args[512];
		AVRational time_base = fmt_ctx->streams[video_stream_index]->time_base;
		snprintf(args, sizeof(args),
				"video_size=%dx%d:pix_fmt=%d:time_base=%d/%d:pixel_aspect=%d/%d",
				dec_ctx->width, dec_ctx->height, dec_ctx->pix_fmt,
				time_base.num, time_base.den,
				dec_ctx->sample_aspect_ratio.num, dec_ctx->sample_aspect_ratio.den);

		// auto ret = avfilter_graph_create_filter(&buffersrc_ctx, buffersrc, "in", "", nullptr, filter_graph);

		auto ret = avfilter_graph_create_filter(&buffersrc_ctx,
				avfilter_get_by_name("buffer"),
				"ffplay_buffer", args, nullptr,
				filter_graph);

		if (ret < 0) {
			UtilityFunctions::printerr("Could not create buffer input filter");
		}

		auto params = av_buffersrc_parameters_alloc();
		params->format = dec_ctx->pix_fmt;
		params->width = dec_ctx->width;
		params->height = dec_ctx->height;
		params->sample_aspect_ratio = dec_ctx->sample_aspect_ratio;
		params->time_base = time_base;
		params->hw_frames_ctx = dec_ctx->hw_frames_ctx;
		av_buffersrc_parameters_set(buffersrc_ctx, params);

		ret = avfilter_graph_create_filter(&buffersink_ctx, buffersink, "out",
				nullptr, nullptr, filter_graph);
		if (ret < 0) {
			UtilityFunctions::printerr("Could not create buffer output filter");
		}

		// AVPixelFormat pix_fmts[] = { AV_PIX_FMT_RGB8, AV_PIX_FMT_RGB24, AV_PIX_FMT_YUV420P, AV_PIX_FMT_NONE };
		// ret = av_opt_set_int_list(buffersink_ctx, "pix_fmts", pix_fmts,
		// 		AV_PIX_FMT_NONE, AV_OPT_SEARCH_CHILDREN);
		// if (ret < 0) {
		// 	UtilityFunctions::printerr("Could not set output pixel format");
		// }

		AVFilterInOut *outputs = avfilter_inout_alloc();
		AVFilterInOut *inputs = avfilter_inout_alloc();
		/*
		 * Set the endpoints for the filter graph. The filter_graph will
		 * be linked to the graph described by filters_descr.
		 */

		/*
		 * The buffer source output must be connected to the input pad of
		 * the first filter described by filters_descr; since the first
		 * filter input label is not specified, it is set to "in" by
		 * default.
		 */
		// buffersrc_ctx->hw_device_ctx = dec_ctx->hw_device_ctx;
		// buffersink_ctx->hw_device_ctx = dec_ctx->hw_device_ctx;

		outputs->name = av_strdup("in");
		outputs->filter_ctx = buffersrc_ctx;
		outputs->pad_idx = 0;
		outputs->next = nullptr;

		/*
		 * The buffer sink input must be connected to the output pad of
		 * the last filter described by filters_descr; since the last
		 * filter output label is not specified, it is set to "out" by
		 * default.
		 */
		inputs->name = av_strdup("out");
		inputs->filter_ctx = buffersink_ctx;
		inputs->pad_idx = 0;
		inputs->next = outputs;

		// ret = avfilter_graph_parse_ptr(filter_graph, "scale_vulkan=w=3840:h=2160:scaler=bilinear:format=yuv420p",
		// 		&inputs, &outputs, nullptr);
		ret = avfilter_graph_parse_ptr(filter_graph, "scale_vulkan=scaler=bilinear", //":format=yuv420p",
				&inputs, &outputs, nullptr);

		if (ret < 0) {
			UtilityFunctions::printerr("Could not parse filter graph");
		}

		ret = avfilter_graph_config(filter_graph, nullptr);
		if (ret < 0) {
			UtilityFunctions::printerr("Could not configure filter graph");
		} else {
			UtilityFunctions::print("created filter");
			has_filter_graph = true;
		}
	}
}
