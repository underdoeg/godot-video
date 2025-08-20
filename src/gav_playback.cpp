//
// Created by phwhitfield on 05.08.25.
//

#include "gav_playback.h"

#include "helpers.h"
#include "vk_ctx.h"

#include <algorithm>
#include <godot_cpp/classes/image_texture.hpp>
#include <optional>

extern "C" {
#include <libavformat/avformat.h>
#include <libavutil/pixdesc.h>
// #include <libswscale/swscale.h>
// #include <libswresample/swresample.h>
}

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

RenderingDevice *GAVPlayback::decode_rd = nullptr;
RenderingDevice *GAVPlayback::conversion_rd = nullptr;

void GAVPlayback::_bind_methods() {
}
GAVPlayback::~GAVPlayback() {
	GAVPlayback::_stop();
	av_packet_unref(pkt);
	// if (thread.joinable()) {
	// 	thread.join();
	// }
	if (video_codec_ctx)
		avcodec_free_context(&video_codec_ctx);
}

bool GAVPlayback::load(const String &file_path) {
	if (!decode_rd) {
		decode_rd = RenderingServer::get_singleton()->get_rendering_device();
		// decode_rd = RenderingServer::get_singleton()->create_local_rendering_device();
	}
	if (!conversion_rd) {
		conversion_rd = decode_rd; // RenderingServer::get_singleton()->create_local_rendering_device();
	}
	// conversion_rd = RenderingServer::get_singleton()->get_rendering_device();

	const String path = ProjectSettings::get_singleton()->globalize_path(file_path);
	UtilityFunctions::print("GAVPlayback::load ", path);

	AVDictionary *options = nullptr;
	// av_dict_set(&options, "loglevel", "debug", 0);

	// av_log_set_level(AV_LOG_DEBUG);

	if (!ff_ok(avformat_open_input(&fmt_ctx, path.ascii().ptr(), nullptr, &options))) {
		UtilityFunctions::printerr("GAVPlayback could not open: ", path);
		return false;
	}

	if (!ff_ok(avformat_find_stream_info(fmt_ctx, NULL))) {
		UtilityFunctions::printerr("GAVPlayback could not find stream information.");
		return false;
	}

	// // Display some basic information about the file and streams
	// std::cout << "Container format: " << fmt_ctx->iformat->name << std::endl;
	// std::cout << "Duration: " << fmt_ctx->duration << " microseconds" << std::endl;
	// std::cout << "Number of streams: " << fmt_ctx->nb_streams << std::endl;

	for (int i = 0; i < fmt_ctx->nb_streams; i++) {
		AVStream *stream = fmt_ctx->streams[i];
		if (!has_video() && stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			video_stream_index = i;
		}
		if (!has_audio() && stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audio_stream_index = i;
		}
		if (has_video() && has_audio()) {
			break;
		}
	}

	if (has_video()) {
		UtilityFunctions::print("GAVPlayback:: video stream index: ", video_stream_index);
		if (!init_video()) {
			return false;
		}
	}
	if (has_audio()) {
		UtilityFunctions::print("GAVPlayback:: audio stream index: ", audio_stream_index);
		if (!init_audio()) {
			return false;
		}
	} else {
		UtilityFunctions::print("GAVPlayback:: no audio stream found");
	}

	return true;
}

bool GAVPlayback::init_video() {
	video_ctx_ready = false;
	AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
	const AVCodec *decoder = avcodec_find_decoder(codecpar->codec_id);
	if (!decoder) {
		UtilityFunctions::printerr("Video Decoder not found");
		return false;
	}

	// Print codec information
	std::cout << "Video codec: " << avcodec_get_name(codecpar->codec_id) << std::endl;
	std::cout << "Width: " << codecpar->width << " Height: " << codecpar->height << std::endl;
	std::cout << "Bitrate: " << codecpar->bit_rate << std::endl;

	video_codec_ctx = avcodec_alloc_context3(nullptr);
	if (!video_codec_ctx) {
		UtilityFunctions::printerr("avcodec_alloc_context3 failed");
		return false;
	}

	if (!ff_ok(avcodec_parameters_to_context(video_codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar))) {
		UtilityFunctions::printerr("avcodec_parameters_to_context failed: ");
		avcodec_free_context(&video_codec_ctx);
		return false;
	}
	video_codec_ctx->pkt_timebase = fmt_ctx->streams[video_stream_index]->time_base;

	constexpr auto device_type = AV_HWDEVICE_TYPE_VULKAN;

	/* Look for supported hardware-accelerated configurations */
	int i = 0;
	const AVCodecHWConfig *accel_config = nullptr;
	{
		const AVCodecHWConfig *config = nullptr;
		while ((config = avcodec_get_hw_config(decoder, i++)) != nullptr) {
			// UtilityFunctions::print("Found hardware acceleration with pixel format: ", av_hwdevice_get_type_name(config->device_type), " - ",
			// 		av_get_pix_fmt_name(config->pix_fmt));

			if (config->device_type != device_type || !(config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX)) {
				continue;
			}
			accel_config = config;
		}
	}

	if (!accel_config) {
		UtilityFunctions::printerr("Unable to find acceleration config for vulkan");
		return false;
	}

	video_codec_ctx->hw_device_ctx = av_vk_create_device(decode_rd);
	video_codec_ctx->pix_fmt = accel_config->pix_fmt;
	// }

	if (codecpar->codec_id == AV_CODEC_ID_VVC) {
		video_codec_ctx->strict_std_compliance = -2;

		/* Enable threaded decoding, VVC decode is slow */
		video_codec_ctx->thread_count = 4;
		video_codec_ctx->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);
	} else
		video_codec_ctx->thread_count = 1;

	auto frames = av_hwframe_ctx_alloc(video_codec_ctx->hw_device_ctx);
	if (!frames) {
		UtilityFunctions::printerr("Failed to allocate HW frame context.");
		return false;
	}
	auto *ctx = reinterpret_cast<AVHWFramesContext *>(frames->data);
	ctx->format = accel_config->pix_fmt;
	ctx->width = codecpar->width;
	ctx->height = codecpar->height;
	ctx->sw_format = AV_PIX_FMT_YUV420P;

	// if (codec_ctx->format == AV_PIX_FMT_VULKAN) {
	// 	// auto *vk = static_cast<AVVulkanFramesContext *>(codec_ctx->hwctx);
	// 	// vk->img_flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT | VK_IMAGE_CREATE_EXTENDED_USAGE_BIT;
	// 	// // XXX: FFmpeg header type bug.
	// 	// vk->usage = static_cast<VkImageUsageFlagBits>(vk->usage | VK_IMAGE_USAGE_STORAGE_BIT |
	// 	// 		VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_VIDEO_ENCODE_SRC_BIT_KHR);
	//
	// 	// h264_encode.stdProfileIdc = STD_VIDEO_H264_PROFILE_IDC_HIGH;
	// 	//
	// 	// profile_info.videoCodecOperation = VK_VIDEO_CODEC_OPERATION_ENCODE_H264_BIT_KHR;
	// 	// profile_info.chromaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
	// 	// profile_info.lumaBitDepth = VK_VIDEO_COMPONENT_BIT_DEPTH_8_BIT_KHR;
	// 	// profile_info.chromaSubsampling = VK_VIDEO_CHROMA_SUBSAMPLING_420_BIT_KHR;
	// 	// profile_info.pNext = &h264_encode;
	// 	//
	// 	// profile_list_info.pProfiles = &profile_info;
	// 	// profile_list_info.profileCount = 1;
	// 	// vk->create_pnext = &profile_list_info;
	// }

	if (av_hwframe_ctx_init(frames) != 0) {
		UtilityFunctions::printerr("Failed to initialize HW frame context.");
		// av_buffer_unref(&frames);
		// return false;
		return false;
	}
	UtilityFunctions::print("Created Vulkan FFmpeg HW device.");
	video_codec_ctx->hw_frames_ctx = av_buffer_ref(frames);

	// UtilityFunctions::print("frame format is: ", av_get_pix_fmt_name(codec_ctx->sw_format));

	if (!ff_ok(avcodec_open2(video_codec_ctx, decoder, nullptr))) {
		UtilityFunctions::print("Couldn't open codec %s: %s", avcodec_get_name(video_codec_ctx->codec_id));
		avcodec_free_context(&video_codec_ctx);
		return false;
	}

	texture.setup(video_codec_ctx, conversion_rd);

	video_ctx_ready = true;
	return true;
}
bool GAVPlayback::init_audio() {
	UtilityFunctions::print("TODO init audio");
	return false;
}
// void GAVPlayback::thread_func() {
// 	UtilityFunctions::printerr("Starting video thread");
// 	while (!request_stop) {
// 	}
// }
void GAVPlayback::decode_next_frame() {
	const auto ret = av_read_frame(fmt_ctx, pkt);
	if (ret == AVERROR_EOF) {
		decode_is_done = true;
		av_packet_unref(pkt);
		return;
	}
	if (ff_ok(ret)) {
		// UtilityFunctions::print(pkt->stream_index);
		if (pkt->stream_index == video_stream_index) {
			// UtilityFunctions::print("new video frame");
			decode_video_frame(pkt);
		} else {
			// decode_audio_frame(pkt);
			// UtilityFunctions::print("cannot handle av packet");
		}
	}
	av_packet_unref(pkt);
}

bool GAVPlayback::decode_video_frame(AVPacket *pkt) {
	if (!pkt || !video_codec_ctx) {
		UtilityFunctions::printerr("GAVPlayback::decode_video_frame: null packet");
		return false;
	}

	AVFrame *tmp_frame = nullptr;

	auto cleanup = [&] {
		// UtilityFunctions::print("Decode video frame done");
		if (tmp_frame) {
			av_frame_free(&tmp_frame);
		}
		if (active_frame) {
			av_frame_free(&active_frame);
		}
		active_frame = nullptr;
	};

	// UtilityFunctions::print("decode_video_frame");
	auto send = [&] {
		while (true) {
			// UtilityFunctions::print("send");
			auto ret = avcodec_send_packet(video_codec_ctx, pkt);
			if (ret == AVERROR(EAGAIN)) {
				// UtilityFunctions::print("----------------------- GAVPlayback::send_video_frame: EAGAIN");
				break;
			}
			if (!ff_ok(ret)) {
				UtilityFunctions::printerr("Failed to send packet to decoder");
				return false;
			}
			break;
		}
		return true;
	};
	if (!send()) {
		UtilityFunctions::printerr("Failed to send packet to decoder");
		return false;
	}
	// UtilityFunctions::print("packet sent");

	// UtilityFunctions::print("Decode video frame start");

	auto receive = [&] {
		// if (!active_frame) {
		// active_frame = av_frame_alloc();
		// }

		while (true) {
			// UtilityFunctions::print("receive");
			tmp_frame = av_frame_alloc();
			auto ret = avcodec_receive_frame(video_codec_ctx, tmp_frame);
			if (ret == AVERROR(EAGAIN)) {
				// UtilityFunctions::print("----------------------- GAVPlayback::decode_video_frame: EAGAIN");
				break;
			}
			if (ret == AVERROR_EOF) {
				decode_is_done = true;
				return false;
			}
			if (!ff_ok(ret)) {
				UtilityFunctions::printerr("avcodec_receive_frame failed");
				cleanup();
				return false;
			}
			active_frame = tmp_frame;
			tmp_frame = nullptr;
		}
		return true;
	};

	if (!receive()) {
		UtilityFunctions::printerr("Failed to receive frame");
		cleanup();
		return false;
	}

	if (!active_frame) {
		cleanup();
		return false;
	}

	if (tmp_frame) {
		av_frame_free(&tmp_frame);
	}

	if (active_frame->format != AV_PIX_FMT_VULKAN) {
		// UtilityFunctions::printerr("Unsupported pixel format", av_get_pix_fmt_name(static_cast<AVPixelFormat>(active_frame->format)));
		cleanup();
		return false;
	}

	auto time = frame_time(active_frame);
	if (time < Clock::now()) {
		// UtilityFunctions::print("GAVPlayback frame drop");
		cleanup();
		return false;
	}

	// UtilityFunctions::print("GAVPlayback::decode_video_frame 1 ", std::chrono::duration_cast<std::chrono::milliseconds>(time - Clock::now()).count());

	frame_buffer.push_front({ active_frame, time });

	// UtilityFunctions::print("GAVPlayback::decode_video_frame 2 ", std::chrono::duration_cast<std::chrono::milliseconds>(frame_buffer.back().timestamp - Clock::now()).count());
	active_frame = nullptr;
	return true;
}

GAVPlayback::Clock::time_point GAVPlayback::frame_time(AVFrame *frame) {
	// const auto pts = frame->pts;

	// (av_q2d(video.av_stream->time_base) * double(pts)) : double(pts) * 1e-6
	auto seconds = std::chrono::duration<double>(frame->best_effort_timestamp * av_q2d(video_codec_ctx->pkt_timebase));
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(seconds);
	// UtilityFunctions::print("millis: ", millis.count());

	// auto frame_time = std::chrono::duration<double>(pts_seconds);
	if (waiting_for_start_time) {
		start_time = Clock::now();
		waiting_for_start_time = false;
	}

	return start_time + millis;
}

void GAVPlayback::set_state(State new_state) {
	state = new_state;
	if (state == PLAYING) {
		waiting_for_start_time = true;
	}
}
void GAVPlayback::_stop() {
	request_stop = true;
}

void GAVPlayback::_play() {
	if (!has_video() && !has_audio()) {
		UtilityFunctions::printerr("Cannot play. No video or audio stream");
		return;
	}
	set_state(State::PLAYING);
}

void GAVPlayback::_update(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	if (!video_ctx_ready) {
		return;
	}

	if (frame_buffer.size() < max_frame_buffer_size) {
		decode_next_frame();
	}

	if (state == State::PAUSED) {
		return;
	}

	// check for new video frames
	const auto now = Clock::now();

	std::optional<Frame> video_frame;
	while (!frame_buffer.empty()) {
		// std::cout << (frame_buffer.front().timestamp  <= now) << " " << (now - frame_buffer.front().timestamp).count() << " " << std::endl;
		// UtilityFunctions::print("GAVPlayback::decode_video_frame ", std::chrono::duration_cast<std::chrono::milliseconds>(frame_buffer.back().timestamp - Clock::now()).count());
		// UtilityFunctions::print(frame_buffer.back().timestamp <= now);
		if (auto f = frame_buffer.back(); f.timestamp <= now) {
			if (video_frame) {
				av_frame_free(&video_frame->frame);
			}
			video_frame = f;
			frame_buffer.pop_back();
		} else {
			break;
		}
	}

	if (video_frame) {
		// UtilityFunctions::print("new video frame");
		texture.update(video_frame->frame);
		av_frame_free(&video_frame->frame);
	} else {
		// UtilityFunctions::print("no frame");
	}

	// UtilityFunctions::print("after: ", frame_buffer.size());

	if (frame_buffer.empty() && decode_is_done) {
		set_state(State::STOPPED);
	}

	// Keep buffer filled
	// for (int i = 0; i < 3; i++) {
	// }
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
	UtilityFunctions::print("TODO get length");
	return video_info.duration;
}
double GAVPlayback::_get_playback_position() const {
	UtilityFunctions::print("TODO get playback position");
	return 0;
}
void GAVPlayback::_seek(double p_time) {
	UtilityFunctions::printerr("TODO: seek");
	// TODO convert p_Time to video timestamp format, maybe this is right, doubt it though
	auto pts = p_time / av_q2d(video_codec_ctx->pkt_timebase);
	avformat_seek_file(fmt_ctx, -1, INT64_MIN, pts, pts, 0);
}

void GAVPlayback::_set_audio_track(int32_t p_idx) {
	UtilityFunctions::printerr("set_audio_track is not supported");
}

Ref<Texture2D> GAVPlayback::_get_texture() const {
	return texture.get_texture();
}

int32_t GAVPlayback::_get_channels() const {
	return 0;
}
int32_t GAVPlayback::_get_mix_rate() const {
	return 0;
}