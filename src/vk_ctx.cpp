//
// Created by phwhitfield on 05.08.25.
//

#include "vk_ctx.h"

#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL vk_proc_addr(
		VkInstance instance,
		const char *pName) {
	// auto ret = vkGetInstanceProcAddr(instance, pName);
	// UtilityFunctions::print("get_it: ", pName, " - ", ret != nullptr);
	// return ret;
	return vkGetInstanceProcAddr(instance, pName);
}

bool av_vk_ctx_setup(AVVulkanDeviceContext *ctx, godot::RenderingDevice *rd) {
	ctx->get_proc_addr = vk_proc_addr;
	ctx->inst = reinterpret_cast<VkInstance>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_TOPMOST_OBJECT, {}, 0));
	ctx->act_dev = reinterpret_cast<VkDevice>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_LOGICAL_DEVICE, {}, 0));
	ctx->phys_dev = reinterpret_cast<VkPhysicalDevice>(rd->get_driver_resource(RenderingDevice::DRIVER_RESOURCE_PHYSICAL_DEVICE, {}, 0));

	// Load instance extensions that are available.
	{
		uint32_t instance_extension_count = 0;
		// VkResult err = vkEnumerateInstanceExtensionProperties(nullptr, &instance_extension_count, nullptr);
		// ERR_FAIL_COND_V(err != VK_SUCCESS && err != VK_INCOMPLETE, ERR_CANT_CREATE);
		// ERR_FAIL_COND_V_MSG(instance_extension_count == 0, ERR_CANT_CREATE, "No instance extensions were found.");

		static Vector<VkExtensionProperties> instance_extensions;
		instance_extensions.resize(instance_extension_count);

		auto vk_enum = reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(vk_proc_addr(ctx->inst, "vkEnumerateInstanceExtensionProperties"));
		static Vector<const char *> instance_extension_names;
		auto err = vk_enum(nullptr, &instance_extension_count, instance_extensions.ptrw());
		if (err != VK_SUCCESS && err != VK_INCOMPLETE) {
			UtilityFunctions::printerr("vkEnumerateInstanceExtensionProperties failed");
			return false;
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
			// UtilityFunctions::print("Found instance extension:", extension.extensionName);
			instance_extension_names.push_back(extension.extensionName);
		}
		ctx->enabled_inst_extensions = instance_extension_names.ptr();
		ctx->nb_enabled_inst_extensions = instance_extension_names.size();
	}

	// list of device extensions
	{
		uint32_t device_extension_count = 0;
		VkResult err = vkEnumerateDeviceExtensionProperties(ctx->phys_dev, nullptr, &device_extension_count, nullptr);
		// ERR_FAIL_COND_V(err != VK_SUCCESS, ERR_CANT_CREATE);
		// ERR_FAIL_COND_V_MSG(device_extension_count == 0, ERR_CANT_CREATE, "vkEnumerateDeviceExtensionProperties failed to find any extensions\n\nDo you have a compatible Vulkan installable client driver (ICD) installed?");
		if (err != VK_SUCCESS) {
			UtilityFunctions::printerr("vkEnumerateDeviceExtensionProperties failed");
			return false;
		}

		static Vector<VkExtensionProperties> device_extensions;
		device_extensions.resize(device_extension_count);

		static Vector<const char *> device_extension_names;

		err = vkEnumerateDeviceExtensionProperties(ctx->phys_dev, nullptr, &device_extension_count, device_extensions.ptrw());
		if (err != VK_SUCCESS) {
			UtilityFunctions::printerr("vkEnumerateDeviceExtensionProperties step 2 failed");
			return false;
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
				VK_KHR_VIDEO_DECODE_VP9_EXTENSION_NAME,
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
			// UtilityFunctions::print("found device extension:", extension.extensionName);
			device_extension_names.push_back(extension.extensionName);
		}

		ctx->enabled_dev_extensions = device_extension_names.ptr();

		ctx->nb_enabled_dev_extensions = device_extension_names.size();
		// for (int i = 0; i < vk->nb_enabled_dev_extensions; i++) {
		// 	printf("%s\n", vk->enabled_dev_extensions[i]);
		// }
	}

	{
		ctx->nb_qf = 0;

		// uint32_t queue_family_properties_count = 0;

		// auto props = Vector<VkQueueFamilyProperties2>();
		// props.resize(32);
		// dev_props(ctx->phys_dev, &queue_family_properties_count, nullptr);

		// if (queue_family_properties_count > 0) {
		// UtilityFunctions::print("found potential queue families:", queue_family_properties_count);
		// props.resize(queue_family_properties_count);

		auto dev_props = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(vk_proc_addr(ctx->inst, "vkGetPhysicalDeviceQueueFamilyProperties"));

		uint32_t queueFamilyCount = 0;
		dev_props(ctx->phys_dev, &queueFamilyCount, NULL);
		std::vector<VkQueueFamilyProperties> familyProperties(queueFamilyCount);
		dev_props(ctx->phys_dev, &queueFamilyCount, familyProperties.data());

		// dev_props(ctx->phys_dev, &queue_family_properties_count, nullptr);
		//
		// UtilityFunctions::print("num queue families: ", queue_family_properties_count);
		//
		// if (!dev_props) {
		// 	UtilityFunctions::printerr("vkGetPhysicalDeviceQueueFamilyProperties2 failed");
		// 	return false;
		// }
		// Vector<VkQueueFamilyProperties2> props;
		// props.resize(queue_family_properties_count);
		// auto dev_props2 = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties2>(vk_proc_addr(ctx->inst, "vkGetPhysicalDeviceProperties2"));
		// dev_props2(ctx->phys_dev, &queue_family_properties_count, props.ptrw());

		for (auto j = 0; j < queueFamilyCount; j++) {
			const auto v = familyProperties[j];
			// if(v.queueFlags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) {

			VkQueueFlagBits;
			VkQueueFlags;

			ctx->qf[ctx->nb_qf] = {
				j,
				1,

				// flags taken from here:
				// https://github.com/godotengine/godot/blob/2d113cc224cb9be07866d003819fcef2226a52ea/drivers/vulkan/rendering_device_driver_vulkan.cpp#L1050
				VkQueueFlagBits(v.queueFlags),

				VkVideoCodecOperationFlagBitsKHR(
						VK_VIDEO_CODEC_OPERATION_DECODE_H264_BIT_KHR |
						VK_VIDEO_CODEC_OPERATION_DECODE_H265_BIT_KHR |
						VK_VIDEO_CODEC_OPERATION_DECODE_VP9_BIT_KHR |
						VK_VIDEO_CODEC_OPERATION_DECODE_AV1_BIT_KHR)
			};
			;
			ctx->nb_qf += 1;
			// vk->nb
			// }
		}

		if (ctx->nb_qf == 0) {
			UtilityFunctions::print("No queue family for video decoding found");
			return false;
		}

		// UtilityFunctions::print("found queue families:", ctx->nb_qf);
		// for (auto i = 0; i < ctx->nb_qf; i++) {
		// UtilityFunctions::print("QF: ", ctx->qf[i].idx, " -- ", ctx->qf[i].flags);
		// }
	}

	// TODO locking mechanism
	ctx->lock_queue = [](AVHWDeviceContext *ctx, uint32_t, uint32_t) {
		// UtilityFunctions::print("locking queue");
		// auto *self = static_cast<Impl *>(ctx->user_opaque);
		// self->device->external_queue_lock();
	};

	ctx->unlock_queue = [](AVHWDeviceContext *ctx, uint32_t, uint32_t) {
		// auto *self = static_cast<Impl *>(ctx->user_opaque);
		// UtilityFunctions::print("unlocking queue");
		// self->device->external_queue_unlock();
	};

	return true;
}

AVBufferRef *av_vk_create_device(godot::RenderingDevice *rd) {
	static AVBufferRef *hw_dev = nullptr;
	if (hw_dev) {
		return hw_dev;
	}

	auto device_type = AV_HWDEVICE_TYPE_VULKAN;
	hw_dev = av_hwdevice_ctx_alloc(device_type);
	auto hw_ctx = reinterpret_cast<AVHWDeviceContext *>(hw_dev->data);
	auto *vk = static_cast<AVVulkanDeviceContext *>(hw_ctx->hwctx);

	av_vk_ctx_setup(vk, rd);
	// TODO free the hwdevice and general cleanup
	if (av_hwdevice_ctx_init(hw_dev) >= 0) {
		UtilityFunctions::print("Created custom Vulkan FFmpeg HW device.");
		return hw_dev;
	}

	UtilityFunctions::printerr("Failed to create Vulkan FFmpeg HW device.");
	return nullptr;
}