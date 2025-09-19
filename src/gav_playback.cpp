//
// Created by phwhitfield on 05.08.25.
//

#include "gav_playback.h"

#include "helpers.h"
#include "vk_ctx.h"

#include <algorithm>
#include <format>
#include <godot_cpp/classes/image_texture.hpp>
#include <iomanip>
#include <optional>
#include <random>
#include <ranges>

extern "C" {
#include <libavdevice/avdevice.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/hwcontext_vdpau.h>
#include <libavutil/pixdesc.h>
}

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

RenderingDevice *GAVPlayback::decode_rd = nullptr;
RenderingDevice *GAVPlayback::conversion_rd = nullptr;

// constexpr int num_open_videos_max = 32;
// static int num_open_videos = 0;
// static int num_open_videos_total = 0;

static std::map<int, std::vector<AVBufferRef *>> hw_devices;

void GAVPlayback::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));
}

GAVPlayback::GAVPlayback() {
	// UtilityFunctions::print("Create new GAVPlayback");
}

GAVPlayback::~GAVPlayback() {
	GAVPlayback::_stop();
	cleanup(true);
	// av_frame_unref(audio_frame.get());
	// av_frame_unref(video_frame_to_show.get());
	av_packet_unref(pkt);
	av_packet_free(&pkt);
	// // if (thread.joinable()) {
	// // 	thread.join();
	// // }
	// if (video_codec_ctx)
	// 	avcodec_free_context(&video_codec_ctx);
	// if (audio_codec_ctx)
	// 	avcodec_free_context(&audio_codec_ctx);

	// if (audio_buffer) {
	// 	av_freep(audio_buffer);
	// }
}

bool GAVPlayback::load(const String &file_path) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return false;
	}
	const auto splits = file_path.split("/");
	filename = splits[splits.size() - 1];
	file_path_requested = file_path;
	if (texture_public.is_valid()) {
		texture_public->set_texture_rd_rid(RID());
	}

	cleanup(true);

	if (!decode_rd) {
		decode_rd = RenderingServer::get_singleton()->get_rendering_device();
	}
	if (!conversion_rd) {
		conversion_rd = decode_rd; // RenderingServer::get_singleton()->create_local_rendering_device();
	}

	// open the file but do not request_init decoders yet
	const String path = ProjectSettings::get_singleton()->globalize_path(file_path_requested);
	UtilityFunctions::print(filename, ": ", "GAVPlayback::load ", path);

	AVDictionary *options = nullptr;
	// av_dict_set(&options, "loglevel", "debug", 0);
	// av_log_set_level(AV_LOG_DEBUG);

	if (!ff_ok(avformat_open_input(&fmt_ctx, path.ascii().ptr(), nullptr, &options))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "GAVPlayback could not open: ", path);
		return false;
	}

	if (!ff_ok(avformat_find_stream_info(fmt_ctx, nullptr))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "GAVPlayback could not find stream information.");
		return false;
	}

	video_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

	// this is a bit dumb but godot asks for the texture right away so we must prepare it for when the video will eventually load
	if (video_stream_index != AVERROR_STREAM_NOT_FOUND) {
		auto stream = fmt_ctx->streams[video_stream_index];
		AVCodecParameters *codecpar = stream->codecpar;
		texture = std::make_shared<GAVTexture>();
		tex_rid = texture->setup(codecpar->width, codecpar->height, conversion_rd);
		if (texture_public.is_valid()) {
			texture_public->set_texture_rd_rid(tex_rid);
		}
	}

	if (audio_stream_index != AVERROR_STREAM_NOT_FOUND) {
		auto stream = fmt_ctx->streams[audio_stream_index];
		audio_num_channels = stream->codecpar->ch_layout.nb_channels;
		audio_sample_rate = stream->codecpar->sample_rate;
	}

	file_path_loaded = file_path;
	UtilityFunctions::print(filename, ": ", "GAVPlayback::load ", file_path_loaded);

	// request_init();
	return true;
}

// void GAVPlayback::thread_func() {
// 	UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Starting video thread");
// 	while (!request_stop) {
// 	}
// }

bool GAVPlayback::init() {
	if (!fmt_ctx) {
		UtilityFunctions::printerr(filename, ": ", "GAVPlayback cannot request_init, fmt ctx is undefined: ", file_path_requested);
		return false;
	}

	waiting_for_init = false;
	decode_is_done = false;

	if (has_video()) {
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "GAVPlayback:: video stream index: ", video_stream_index);
		if (!init_video()) {
			return false;
		}
	}
	if (has_audio()) {
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "GAVPlayback:: audio stream index: ", audio_stream_index);
		if (!init_audio()) {
			return false;
		}
	} else {
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "GAVPlayback:: no audio stream found");
	}
	file_path_loaded = file_path_requested;
	return true;
}

bool GAVPlayback::request_init() {
	if (!file_path_requested) {
		return false;
	}

	_stop();

	cleanup(false);

	decoder_threaded = true;
#if GODOT_VULKAN_PATCHED
	decoder_threaded = false;
#endif

	if (decoder_threaded) {
		// run a thread
		decoder_thread = std::thread([&] {
			decoder_threaded_func();
		});

		return true;
	}

	return init();
}
bool GAVPlayback::init_video() {
	// determine sensible prefered codec
	if (!av_vk_video_supported(decode_rd)) {
		// this is a dummy check ATM, it will always return false, needs godot with video extension loaded
		// auto detect v dpau or vaapi /nvidia or intel
		const auto device_vendor = conversion_rd->get_device_vendor_name().to_lower();
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "Device vendor ", device_vendor);
		if (device_vendor == "nvidia") {
			hw_preferred = AV_HWDEVICE_TYPE_VDPAU;
		} else {
			hw_preferred = AV_HWDEVICE_TYPE_VAAPI;
		}
		// hw_preferred = AV_HWDEVICE_TYPE_VULKAN;
		if constexpr (GODOT_VULKAN_PATCHED) {
			hw_preferred = AV_HWDEVICE_TYPE_VULKAN;
		}
	}

	video_ctx_ready = false;
	auto stream = fmt_ctx->streams[video_stream_index];
	AVCodecParameters *codecpar = stream->codecpar;
	const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
	if (!decoder) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Video Decoder not found");
		return false;
	}
	// Print codec information
	// std::cout << "Video codec: " << avcodec_get_name(codecpar->codec_id) << std::endl;
	// std::cout << "Width: " << codecpar->width << " Height: " << codecpar->height << std::endl;
	// std::cout << "Bitrate: " << codecpar->bit_rate << std::endl;
	UtilityFunctions::print(filename, ": ", "Video codec: ", avcodec_get_name(codecpar->codec_id));
	UtilityFunctions::print(filename, ": ", "Video size: ", codecpar->width, "x", codecpar->height);

	video_codec_ctx = avcodec_alloc_context3(nullptr);
	if (!video_codec_ctx) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "avcodec_alloc_context3 failed");
		return false;
	}

	if (!ff_ok(avcodec_parameters_to_context(video_codec_ctx, stream->codecpar))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "avcodec_parameters_to_context failed: ");
		avcodec_free_context(&video_codec_ctx);
		return false;
	}
	video_codec_ctx->pkt_timebase = stream->time_base;

	std::vector<const AVCodecHWConfig *> configs;

	/* Look for supported hardware-accelerated configurations */
	int i = 0;

	const AVCodecHWConfig *config = nullptr;
	while ((config = avcodec_get_hw_config(decoder, i++)) != nullptr) {
		configs.push_back(config);
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "Detected hw device ", av_hwdevice_get_type_name(config->device_type));
	}

	if (configs.empty()) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "GAVPlayback:: no hardware video decoders found");
		return false;
	}

	if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "Detected ", configs.size(), " video decoding devices");

	// prefer a vulkan decoder
	for (const auto &c : configs) {
		if (c->device_type == hw_preferred) {
			accel_config = c;
			if (verbose_logging)
				UtilityFunctions::print(filename, ": ", "using preferred hw device ", av_hwdevice_get_type_name(hw_preferred));
			break;
		}
	}

	auto create_hw_dev = [&](const AVCodecHWConfig *conf) {
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "Trying to setup HW device: ", av_hwdevice_get_type_name(conf->device_type));

		// if (hw_device_ctx) {
		// 	av_buffer_unref(&hw_device_ctx);
		// 	hw_device_ctx = nullptr;
		// }

		AVBufferRef *hw_device_ctx = nullptr;

		if (!hw_devices[conf->device_type].empty()) {
			// use preexisting devices
			hw_device_ctx = hw_devices[conf->device_type].back();
			hw_devices[conf->device_type].pop_back();
			UtilityFunctions::print("using existing HW decoder");
		} else {
			// THis only works with the patched godot version, TODO check
			if (conf->device_type == AV_HWDEVICE_TYPE_VULKAN && GODOT_VULKAN_PATCHED) {
				hw_device_ctx = av_vk_create_device(decode_rd);
				if (!hw_device_ctx) {
					UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "av_vk_create_device failed");
					return false;
				}
				UtilityFunctions::print(filename, ": ", "Using Vulkan Video API");
			} else {
				// auto hw_device_ctx = av_hwdevice_ctx_alloc(conf->device_type);
				// reinterpret_cast<AVHWDeviceContext*>(hw_device_ctx)->free = [](AVHWDeviceContext* ctx) {
				// 	UtilityFunctions::print("Context free called");
				// };
				// if (!ff_ok(av_hwdevice_ctx_init(hw_device_ctx))) {
				// 	av_buffer_unref(&hw_device_ctx);
				// 	return false;
				// }
				if (!ff_ok(av_hwdevice_ctx_create(&hw_device_ctx, conf->device_type, nullptr, nullptr, 0))) {
					UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Could not create HW device");
					if (hw_device_ctx) {
						av_buffer_unref(&hw_device_ctx);
					}
					hw_device_ctx = nullptr;
					video_codec_ctx->hw_device_ctx = nullptr;
					return false;
				}
			}
		}

		if (!hw_device_ctx) {
			return false;
		}

		video_codec_ctx->hw_device_ctx = hw_device_ctx;
		hw_device_type = conf->device_type;
		UtilityFunctions::print(filename, ": ", "Created HW device of type: ", av_hwdevice_get_type_name(hw_device_type));

		video_codec_ctx->pix_fmt = conf->pix_fmt;

		// if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "hw device created: ", av_hwdevice_get_type_name(hw_device_type));
		return true;
	};

	if (accel_config) {
		if (!create_hw_dev(accel_config)) {
			accel_config = nullptr;
		}
	}

	if (!accel_config) {
		// just take the first one that works
		// accel_config = configs[0];
		// UtilityFunctions::print(filename, ": ", "preferred HW device not found ", av_hwdevice_get_type_name(hw_preferred), " - using: ", av_hwdevice_get_type_name(accel_config->device_type));
		for (const auto &c : configs) {
			if (c->device_type == hw_preferred) {
				// already tried above
				continue;
			}
			if (create_hw_dev(c)) {
				accel_config = c;
				break;
			}
		}
	}

	// if (!video_codec_ctx->hw_device_ctx) {
	// 	UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "No HW device found, need to fallback to SW decoding but that is a TODO");
	// 	return false;
	// }
	// }

	if (codecpar->codec_id == AV_CODEC_ID_VVC) {
		video_codec_ctx->strict_std_compliance = -2;

		/* Enable threaded decoding, VVC decode is slow */
		video_codec_ctx->thread_count = 4;
		video_codec_ctx->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);
	} else {
		video_codec_ctx->thread_count = 1;
	}

	// if (accel_config->device_type == AV_HWDEVICE_TYPE_VAAPI) {
	// 	video_codec_ctx->hw_device_ctx =  av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
	// 	if (!video_codec_ctx->hw_device_ctx) {
	// 		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "av_hwdevice_ctx_alloc failed");
	// 		return false;
	// 	}
	// 	auto wctx = reinterpret_cast<AVHWDeviceContext*>(video_codec_ctx->hw_device_ctx);
	// 	AVVAAPIDeviceContext *vactx = hwctx->hwctx;
	// 	vactx->display = va_display;
	// 	if (av_hwdevice_ctx_init(hw_device_ctx) < 0) {
	// 		fail("av_hwdevice_ctx_init");
	// 	}
	// }

	if (video_codec_ctx->hw_device_ctx) {
		auto frames = av_hwframe_ctx_alloc(video_codec_ctx->hw_device_ctx);
		if (!frames) {
			UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Failed to allocate HW frame context.");
			return false;
		}
		auto *ctx = reinterpret_cast<AVHWFramesContext *>(frames->data);
		ctx->format = accel_config->pix_fmt;
		ctx->width = codecpar->width;
		ctx->height = codecpar->height;
		ctx->sw_format = video_codec_ctx->sw_pix_fmt;
		// ctx->sw_format = accel_config->pix_fmt;
		// ctx->sw_format = AV_PIX_FMT_YUV420P;

		// detect valid sw formats
		AVHWFramesConstraints *hw_frames_const = av_hwdevice_get_hwframe_constraints(video_codec_ctx->hw_device_ctx, nullptr);
		auto sw_format = hw_frames_const->valid_sw_formats[0];
		for (AVPixelFormat *p = hw_frames_const->valid_sw_formats;
				*p != AV_PIX_FMT_NONE; p++) {
			if (verbose_logging)
				UtilityFunctions::print(filename, ": ", "HW decoder pixel format detected: ", av_get_pix_fmt_name(*p));
			// if (*p == AV_PIX_FMT_BGRA) {
			// 	sw_format = *p;
			// }
			// prefer nv12
			if (*p == AV_PIX_FMT_YUV420P) {
				sw_format = *p;
			}
		}
		// just use the first one for the moment
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "Using sw format ", av_get_pix_fmt_name(sw_format));
		ctx->sw_format = sw_format;

		if (av_hwframe_ctx_init(frames) != 0) {
			UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Failed to initialize HW frame context.");
			av_buffer_unref(&frames);
			// return false;
			return false;
		}
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "Created video decoder context HW device.");
		video_codec_ctx->hw_frames_ctx = av_buffer_ref(frames);
	} else {
		UtilityFunctions::print(filename, ": ", "using software decoder");
	}
	// UtilityFunctions::print(filename, ": ", "frame format is: ", av_get_pix_fmt_name(codec_ctx->sw_format));

	if (!ff_ok(avcodec_open2(video_codec_ctx, decoder, nullptr))) {
		UtilityFunctions::print(filename, ": ", "Couldn't open codec %s: %s", avcodec_get_name(video_codec_ctx->codec_id));
		avcodec_free_context(&video_codec_ctx);
		return false;
	}

	// if (!texture || texture->get_width() != video_codec_ctx->width || texture->get_height() != video_codec_ctx->height) {
	// 	// only create a new texture if needed
	// 	texture = std::make_shared<GAVTexture>();
	// 	tex_rid = texture->setup(video_codec_ctx->width, video_codec_ctx->height, conversion_rd);
	// 	if (!tex_rid.is_valid()) {
	// 		UtilityFunctions::printerr(filename, ": ", "Failed to setup texture");
	// 		return false;
	// 	}
	// }
	// texture->codec_ctx = video_codec_ctx;
	// if (texture_public.is_valid()) {
	// 	texture_public->set_texture_rd_rid(tex_rid);
	// }

	const auto time_base = stream->time_base;

	frame_handlers.emplace(video_stream_index, PacketDecoder(video_codec_ctx, [&, time_base](auto frame) {
		auto time = frame_time(frame, time_base);
		if (time <= Clock::now()) {
			// frame should be displayed. do it now;
			VideoFrameType type = VideoFrameType::SW;
			if (accel_config) {
				if (accel_config->device_type == AV_HWDEVICE_TYPE_VULKAN && GODOT_VULKAN_PATCHED) {
					type = VideoFrameType::VK;
				}else {
					type = VideoFrameType::HW;
				}
			}{
				if (decoder_threaded) {
					video_frame_to_show_thread = {frame, {}, type};
				}else {
					video_frame_to_show = {frame, {}, type};
				}
				// av_frame_copy(video_frame_to_show.get(), frame.get());
				auto pos = time - start_time;
				progress_millis = std::chrono::duration_cast<std::chrono::milliseconds>(pos).count();
			}
			return true;
		}
		return false; }, 20));

	video_ctx_ready = true;
	return true;
}

bool GAVPlayback::init_audio() {
	audio_ctx_ready = false;

	// Set output format to stereo
	constexpr AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLT;

	const auto audio_stream = fmt_ctx->streams[audio_stream_index];

	AVCodecParameters *codecpar = audio_stream->codecpar;

	const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
	const auto codec_name = avcodec_get_name(codecpar->codec_id);
	if (!decoder) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Audio Decoder not found: ", codec_name);
		return false;
	}
	UtilityFunctions::print(filename, ": ", "Audio Codec: ", codec_name);
	// std::cout << "Audio Bitrate: " << codecpar->bit_rate << std::endl;

	audio_codec_ctx = avcodec_alloc_context3(nullptr);
	if (!audio_codec_ctx) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "avcodec_alloc_context3 failed for audio");
		return false;
	}

	if (!ff_ok(avcodec_parameters_to_context(audio_codec_ctx, audio_stream->codecpar))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "avcodec_parameters_to_context failed for audio");
		avcodec_free_context(&audio_codec_ctx);
		return false;
	}

	audio_codec_ctx->pkt_timebase = audio_stream->time_base;
	audio_codec_ctx->request_sample_fmt = out_sample_fmt;

	if (!ff_ok(avcodec_open2(audio_codec_ctx, decoder, nullptr))) {
		UtilityFunctions::print(filename, ": ", "Couldn't open audio codec %s: %s", avcodec_get_name(audio_codec_ctx->codec_id));
		avcodec_free_context(&audio_codec_ctx);
		return false;
	}

	if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "Created audio decoder");

	// create resampler
	if (!audio_resampler) {
		audio_resampler = swr_alloc();
	}

	auto channel_layout = codecpar->ch_layout;
	auto sample_rate = codecpar->sample_rate;

	if (verbose_logging) {
		UtilityFunctions::print(filename, " Sample rate: ", sample_rate);
		UtilityFunctions::print(filename, " Num channels: ", channel_layout.nb_channels);
		UtilityFunctions::print(filename, " Sample format:", av_get_sample_fmt_name(out_sample_fmt));
	}
	// create the resampler
	swr_alloc_set_opts2(&audio_resampler,
			&channel_layout, out_sample_fmt, sample_rate,
			&audio_codec_ctx->ch_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
			0, nullptr);

	if (!ff_ok(swr_init(audio_resampler))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Failed to initialize audio resampler");
		swr_free(&audio_resampler);
		return false;
	}

	// audio_num_channels = channel_layout.nb_channels;
	// audio_sample_rate = sample_rate;

	// create an audio frame to hold converted data

	// auto f = FileAccess::open("/home/phwhitfield/test.wav", FileAccess::WRITE);

	const auto time_base = audio_stream->time_base;
	frame_handlers.emplace(audio_stream_index, PacketDecoder(audio_codec_ctx, [&, time_base, channel_layout](auto frame) {
		// audio could be handled in a thread
		auto time = frame_time(frame, time_base);
		if (time > Clock::now()) {
			return false;
		}

		if (!audio_frame) {
			audio_frame = av_frame_ptr();
		}else {
			av_frame_unref(audio_frame.get());
		}

		audio_frame->format = out_sample_fmt;
		audio_frame->ch_layout = channel_layout;
		audio_frame->sample_rate = audio_sample_rate;
		audio_frame->nb_samples = frame->nb_samples;

		if (!ff_ok(swr_convert_frame(audio_resampler, audio_frame.get(), frame.get()))) {
			UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Failed to convert audio resampled data");
			return true;
		}

		//
		if (audio_frame->format != out_sample_fmt) {
			UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Audio resampled data format doesn't match");
			return true;
		}

		// // copy data
		PackedFloat32Array buff;
		int line_size = 0;
		const auto byte_size = av_samples_get_buffer_size(&line_size, channel_layout.nb_channels, audio_frame->nb_samples, out_sample_fmt, 0);
		if (buff.size() < byte_size/sizeof(float)) {
			buff.resize(byte_size / sizeof(float));
		}
		memcpy(buff.ptrw(), audio_frame->data[0], byte_size);
		mix_audio(audio_frame->nb_samples, buff, 0);
		return true; }, 20));

	audio_ctx_ready = true;
	return true;
}

GAVPlayback::Clock::time_point GAVPlayback::frame_time(const AVFramePtr &frame, AVRational time_base) {
	// const auto pts = frame->pts;
	// (av_q2d(video.av_stream->time_base) * double(pts)) : double(pts) * 1e-6
	// auto pts = frame->best_effort_timestamp;
	auto pts = static_cast<double>(frame->best_effort_timestamp);
	pts *= av_q2d(time_base);
	auto seconds = std::chrono::duration<double>(pts);
	// auto seconds = std::chrono::duration<double>(static_cast<double>(ts) * av_q2d(video_codec_ctx->pkt_timebase));
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(seconds);
	// UtilityFunctions::print(filename, ": ", "seconds: ", seconds.count(), " ", pts, " ", av_q2d(time_base));

	if (waiting_for_start_time) {
		start_time = Clock::now() - millis;
		waiting_for_start_time = false;
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "start time set");
	}

	return start_time + millis;
}

void GAVPlayback::set_state(State new_state) {
	state = new_state;
	if (state == PLAYING) {
		waiting_for_start_time = true;
	}
}
void GAVPlayback::cleanup(bool with_format_ctx) {
	if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "Cleanup");
	if (decoder_thread.joinable()) {
		UtilityFunctions::print(filename, ": cleanup wait for decoder thread");
		decoder_thread.join();
	}

	if (video_codec_ctx) {
		avcodec_flush_buffers(video_codec_ctx);
		if (video_codec_ctx->hw_device_ctx && hw_device_type != AV_HWDEVICE_TYPE_NONE) {
			hw_devices[hw_device_type].push_back(video_codec_ctx->hw_device_ctx);
			video_codec_ctx->hw_device_ctx = nullptr;
			video_codec_ctx->hw_frames_ctx = nullptr;
		}
		avcodec_free_context(&video_codec_ctx);
	}
	if (audio_resampler)
		swr_free(&audio_resampler);
	if (audio_codec_ctx) {
		avcodec_flush_buffers(audio_codec_ctx);
		avcodec_free_context(&audio_codec_ctx);
	}

	audio_resampler = nullptr;
	audio_codec_ctx = nullptr;
	audio_frame.reset();
	video_codec_ctx = audio_codec_ctx = nullptr;
	video_frame_to_show.reset();
	// audio_frame.reset();
	frame_handlers.clear();

	if (with_format_ctx) {
		if (fmt_ctx) {
			// num_open_videos--;
			avformat_flush(fmt_ctx);
			avformat_close_input(&fmt_ctx);
			avformat_free_context(fmt_ctx);
			fmt_ctx = nullptr;
		}
		fmt_ctx = nullptr;
	}

	// if (verbose_logging)
	// UtilityFunctions::print(filename, ": ", "Cleanup done");
}
void GAVPlayback::_stop() {
	if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "Stopping playback");
	set_state(STOPPED);
	if (!fmt_ctx) {
		return;
	}
	for (auto f : frame_handlers) {
		f.second.clear();
	}
	video_frame_to_show.reset();
	avformat_flush(fmt_ctx);
	_seek(0);
	if (texture) {
		texture->set_black();
	}
	// cleanup();
}

void GAVPlayback::_play() {
	if (!file_path_requested) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Cannot play. no filepath given");
		return;
	}
	// if (file_path_requested != file_path_loaded) {
	// 	request_init();
	// } else {
	// 	UtilityFunctions::print(filename, ": ", "Restart");
	// 	avformat_seek_file(fmt_ctx, 0, 0, 0, 0, AVSEEK_FLAG_ANY);
	// }
	request_init();

	if (!has_video() && !has_audio() && !waiting_for_init) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Cannot play. No video or audio stream");
		return;
	}
	set_state(State::PLAYING);
}

bool GAVPlayback::read_next_packet() {
	// read the next frame in the file if all frame handlers are ready for a new frame
	bool any_handler_full = false;
	for (const auto &val : frame_handlers | std::views::values) {
		if (val.is_full()) {
			any_handler_full = true;
			break;
		}
	}

	if (any_handler_full) {
		// cannot handle a new packet yet
		return false;
	}

	const auto ret = av_read_frame(fmt_ctx, pkt);
	if (ret == AVERROR_EOF) {
		if (do_loop) {
			// TODO: not that efficient to recreate the entire video pipeline
			request_init();
			// av_packet_unref(pkt);
			return false;
		}
		decode_is_done = true;
		av_packet_unref(pkt);
		return false;
	}
	if (ff_ok(ret) && frame_handlers.contains(pkt->stream_index)) {
		frame_handlers.at(pkt->stream_index).handle(pkt);
		av_packet_unref(pkt);
	} else {
		av_packet_unref(pkt);
	}
	return true;
}
void GAVPlayback::read_packets() {
	if (!fmt_ctx || !pkt) {
		return;
	}

	int read_count = 0;
	while (read_next_packet() && read_count < 30) {
		read_count++;
	}

	// process frame handlers
	// for (auto &val : frame_handlers | std::views::values) {
	// val.process();
	// }
	// }

	for (auto &val : frame_handlers | std::views::values) {
		val.offer_frames();
	}
}
bool GAVPlayback::show_active_video_frame() {
	// process might have given us a new video frame, update_from_vulkan the texture

	// TOO: not necessary when not in threaded mode, but leave for now
	std::scoped_lock lock(decoder_mtx);

	if (!video_frame_to_show)
		return false;

	texture->codec_ctx = video_codec_ctx;
	switch (video_frame_to_show->type) {
		case SW:
			texture->update_from_sw(video_frame_to_show->frame);
			break;
		case HW:
			texture->update_from_hw(video_frame_to_show->frame);
			break;
		case VK:
			texture->update_from_vulkan(video_frame_to_show->frame);
			break;
		case BUFF:
			texture->update_from_buffers(*video_frame_to_show->buffer, video_frame_to_show->pixel_format);
	}
	video_frame_to_show.reset();
	return true;
}

void GAVPlayback::decoder_threaded_func() {
	if (!init()) {
		UtilityFunctions::printerr(filename, ": request_init error => abort threaded decoder");
		return;
	}
	UtilityFunctions::print(filename, ": run threaded decoder");

	std::array<GAVTexture::BuffersPtr, 3> buffers = {};
	for (auto &buff : buffers) {
		buff = std::make_shared<GAVTexture::Buffers>();
	}
	int buffers_index = 0;

	while (state != State::STOPPED) {
		auto now = std::chrono::high_resolution_clock::now();
		auto next = now + std::chrono::milliseconds(1000 / 120);
		read_packets();

		if (video_frame_to_show_thread) {
			// copy to cache
			auto target = av_frame_ptr();
			const auto frame = video_frame_to_show_thread->frame;

			if (!frame->hw_frames_ctx) {
				target = frame;
			} else {
				if (!ff_ok(av_hwframe_transfer_data(target.get(), frame.get(), 0))) {
					UtilityFunctions::printerr("Could not transfer_data from hw to sw");
				}
			}

			// video_frame_to_show_thread.reset();
			//
			// std::scoped_lock lock(decoder_mtx);
			// video_frame_to_show = {
			// 	target,
			// 	{},
			// 	VideoFrameType::SW,
			// 	static_cast<AVPixelFormat>(target->format)
			// };

			buffers_index++;
			buffers_index %= buffers.size();
			auto buff = buffers[buffers_index];
			texture->frame_to_buffers(target, *buff);
			video_frame_to_show_thread.reset();

			std::scoped_lock lock(decoder_mtx);
			video_frame_to_show = {
				{},
				buff,
				VideoFrameType::BUFF,
				static_cast<AVPixelFormat>(target->format)
			};
		} else {
		}

		std::this_thread::sleep_until(next);
	}
}

void GAVPlayback::_update(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	if (waiting_for_init) {
		UtilityFunctions::print("waiting for request_init...");
		request_init();
	}

	if (!decoder_threaded) {
		read_packets();
	}

	show_active_video_frame();

	if (decode_is_done) {
		// check if all frames have been displayed
		bool all_done = true;
		for (auto handler : frame_handlers) {
			if (handler.second.queue_empty()) {
				all_done = false;
				break;
			}
		}
		if (all_done) {
			if (!do_loop) {
				UtilityFunctions::print(filename, ": ", "all done");
				_stop();
				if (finished_callback) {
					finished_callback();
				}
				emit_signal("finished");
			}
		}
	}
}
bool GAVPlayback::_is_playing() const {
	return state == State::PLAYING;
}
void GAVPlayback::_set_paused(bool p_paused) {
	set_state(State::PAUSED);
}
bool GAVPlayback::_is_paused() const {
	return state == State::PAUSED;
}
double GAVPlayback::_get_length() const {
	UtilityFunctions::print(filename, ": ", "TODO get length");
	return video_info.duration;
}
double GAVPlayback::_get_playback_position() const {
	// UtilityFunctions::print(filename, ": ", "TODO get playback position");
	return progress_millis / 1000.0;
}
void GAVPlayback::_seek(double p_time) {
	double pts = p_time;
	if (video_codec_ctx) {
		pts = p_time / av_q2d(video_codec_ctx->pkt_timebase);
	}
	// UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "TODO: seek, it probably does not work right ATM.");
	// TODO convert p_Time to video timestamp format, maybe this is right, doubt it though
	avformat_seek_file(fmt_ctx, -1, INT64_MIN, pts, pts, 0);
}

void GAVPlayback::_set_audio_track(int32_t p_idx) {
	if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "set audio track ", p_idx);
	if (p_idx != 0) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "set_audio_track > 0 is not supported");
	}
}

Ref<Texture2D> GAVPlayback::_get_texture() const {
	if (!texture_public.is_valid()) {
		if (verbose_logging)
			UtilityFunctions::print(filename, ": ", "_get_texture::create new texture");
		texture_public.instantiate();
	}
	if (verbose_logging)
		UtilityFunctions::print(filename, ": ", "_get_texture");
	texture_public->set_texture_rd_rid(tex_rid);
	return texture_public;
}

int32_t GAVPlayback::_get_channels() const {
	UtilityFunctions::print("get_channels(): ", filename, " - ", audio_num_channels);
	return audio_num_channels;
}
int32_t GAVPlayback::_get_mix_rate() const {
	UtilityFunctions::print("get_mix_rate(): ", filename, " - ", audio_sample_rate);
	return audio_sample_rate;
}