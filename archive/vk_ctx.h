//
// Created by phwhitfield on 05.08.25.
//

#pragma once
#include <godot_cpp/classes/rendering_device.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

extern "C" {
	#include <libavutil/hwcontext.h>
	#include <libavutil/hwcontext_vulkan.h>
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_proc_addr(VkInstance instance, const char *pName);
bool av_vk_video_supported(godot::RenderingDevice *rd);
bool av_vk_ctx_setup(AVVulkanDeviceContext *ctx, godot::RenderingDevice *rd);
AVBufferRef *av_vk_create_device(godot::RenderingDevice *rd = godot::RenderingServer::get_singleton()->get_rendering_device());