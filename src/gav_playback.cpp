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
#include <libavutil/pixdesc.h>
}

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

RenderingDevice *GAVPlayback::decode_rd = nullptr;
RenderingDevice *GAVPlayback::conversion_rd = nullptr;

void GAVPlayback::_bind_methods() {
	ADD_SIGNAL(MethodInfo("finished"));
}

GAVPlayback::~GAVPlayback() {
	GAVPlayback::_stop();
	av_packet_unref(pkt);
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
	init();

	return true;
}

// void GAVPlayback::thread_func() {
// 	UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Starting video thread");
// 	while (!request_stop) {
// 	}
// }
void GAVPlayback::read_next_packet() {
	// read the next frame in the file if all frame handlers are ready for a new frame
	bool all_handlers_ready = true;
	for (const auto &val : frame_handlers | std::views::values) {
		if (!val.is_ready()) {
			all_handlers_ready = false;
			break;
		}
	}

	if (!all_handlers_ready) {
		// cannot handle a new packet yet
		return;
	}

	const auto ret = av_read_frame(fmt_ctx, pkt);
	if (ret == AVERROR_EOF) {
		if (do_loop) {
			// TODO: not that efficient to recreate the entire video pipeline
			init();
			return;
		}
		decode_is_done = true;
		av_packet_unref(pkt);
		return;
	}
	if (ff_ok(ret) && frame_handlers.contains(pkt->stream_index)) {
		frame_handlers.at(pkt->stream_index).handle(pkt);
	} else {
		av_packet_unref(pkt);
	}
}

bool GAVPlayback::init() {
	if (!file_path_requested) {
		return false;
	}

	if (!decode_rd) {
		decode_rd = RenderingServer::get_singleton()->get_rendering_device();
	}
	if (!conversion_rd) {
		conversion_rd = decode_rd; // RenderingServer::get_singleton()->create_local_rendering_device();
	}

	cleanup();

	// conversion_rd = RenderingServer::get_singleton()->get_rendering_device();

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

	// device list reports as not implemented
	// if (!ff_ok(avdevice_list_devices(fmt_ctx, &devices))) {
	// 	UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "GAVPlayback could not list devices.");
	// 	return false;
	// }
	//
	// for (int i = 0; i < devices->nb_devices; i++) {
	// 	auto dev = devices->devices[i];
	// 	UtilityFunctions::print(filename, ": ", "potential decoding device: ", dev->device_name);
	// }

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
		UtilityFunctions::print(filename, ": ", "GAVPlayback:: video stream index: ", video_stream_index);
		if (!init_video()) {
			return false;
		}
	}
	if (has_audio()) {
		UtilityFunctions::print(filename, ": ", "GAVPlayback:: audio stream index: ", audio_stream_index);
		if (!init_audio()) {
			return false;
		}
	} else {
		UtilityFunctions::print(filename, ": ", "GAVPlayback:: no audio stream found");
	}

	file_path_loaded = file_path_requested;

	return true;
}
bool GAVPlayback::init_video() {
	// determine sensible prefered codec
	if (!av_vk_video_supported(decode_rd)) {
		// this is a dummy check ATM, it will always return false, needs godot with video extension loaded
		// auto detect v dpau or vaapi /nvidia or intel
		const auto device_vendor = conversion_rd->get_device_vendor_name().to_lower();
		UtilityFunctions::print(filename, ": ", "Device vendopr ", device_vendor);
		if (device_vendor == "nvidia" || device_vendor == "amd") {
			hw_preferred = AV_HWDEVICE_TYPE_VDPAU;
		} else {
			hw_preferred = AV_HWDEVICE_TYPE_VAAPI;
		}
	}

	video_ctx_ready = false;
	AVCodecParameters *codecpar = fmt_ctx->streams[video_stream_index]->codecpar;
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

	if (!ff_ok(avcodec_parameters_to_context(video_codec_ctx, fmt_ctx->streams[video_stream_index]->codecpar))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "avcodec_parameters_to_context failed: ");
		avcodec_free_context(&video_codec_ctx);
		return false;
	}
	video_codec_ctx->pkt_timebase = fmt_ctx->streams[video_stream_index]->time_base;

	std::vector<const AVCodecHWConfig *> configs;

	/* Look for supported hardware-accelerated configurations */
	int i = 0;

	const AVCodecHWConfig *config = nullptr;
	while ((config = avcodec_get_hw_config(decoder, i++)) != nullptr) {
		configs.push_back(config);
		UtilityFunctions::print(filename, ": ", "Detected hw device ", av_hwdevice_get_type_name(config->device_type));
	}

	if (configs.empty()) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "GAVPlayback:: no hardware video decoders found");
		return false;
	}

	UtilityFunctions::print(filename, ": ", "Detected ", configs.size(), " video decoding devices");

	// prefer a vulkan decoder
	for (const auto &c : configs) {
		if (c->device_type == hw_preferred) {
			accel_config = c;
			UtilityFunctions::print(filename, ": ", "using preferred hw device ", av_hwdevice_get_type_name(hw_preferred));
			break;
		}
	}

	auto create_hw_dev = [&](const AVCodecHWConfig *conf) {
		// return false;
		UtilityFunctions::print(filename, ": ", "Trying to setup HW device: ", av_hwdevice_get_type_name(conf->device_type));
		if (conf->device_type == AV_HWDEVICE_TYPE_VULKAN) {
			video_codec_ctx->hw_device_ctx = av_vk_create_device(decode_rd);
			if (!video_codec_ctx->hw_device_ctx) {
				UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "av_vk_create_device failed");
				return false;
			}
		} else if (!ff_ok(av_hwdevice_ctx_create(&video_codec_ctx->hw_device_ctx, conf->device_type, nullptr, nullptr, 0))) {
			UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Could not create HW device");
			video_codec_ctx->hw_device_ctx = nullptr;
			return false;
		}
		// video_codec_ctx->pix_fmt = conf->pix_fmt;
		UtilityFunctions::print(filename, ": ", "hw device created");
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
			// UtilityFunctions::print(filename, ": ", "HW decoder pixel format detected: ", av_get_pix_fmt_name(*p));
			// if (*p == AV_PIX_FMT_BGRA) {
			// 	sw_format = *p;
			// }
			// prefer nv12
			if (*p == AV_PIX_FMT_YUV420P) {
				sw_format = *p;
			}
		}
		// just use the first one for the moment
		UtilityFunctions::print(filename, ": ", "Using sw format ", av_get_pix_fmt_name(sw_format));
		ctx->sw_format = sw_format;

		if (av_hwframe_ctx_init(frames) != 0) {
			UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Failed to initialize HW frame context.");
			// av_buffer_unref(&frames);
			// return false;
			return false;
		}
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

	texture = std::make_shared<GAVTexture>();
	auto tex_rid = texture->setup(video_codec_ctx, conversion_rd);
	if (!tex_rid.is_valid()) {
		UtilityFunctions::printerr(filename, ": ", "Failed to setup texture");
		return false;
	}

	if (!texture_public.is_valid()) {
		texture_public.instantiate();
	}
	texture_public->set_texture_rd_rid(tex_rid);

	frame_handlers.emplace(video_stream_index, PacketDecoder(video_codec_ctx, [&](auto frame) {
		auto time = frame_time(frame);
		if (time <= Clock::now()) {
			// frame should be displayed. do it now;
			video_frame_to_show = frame;
			return true;
		}
		return false;
	}));

	video_ctx_ready = true;
	return true;
}

bool GAVPlayback::init_audio() {
	audio_ctx_ready = false;

	// Set output format to stereo
	constexpr AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_FLT;

	AVCodecParameters *codecpar = fmt_ctx->streams[audio_stream_index]->codecpar;
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

	if (!ff_ok(avcodec_parameters_to_context(audio_codec_ctx, fmt_ctx->streams[audio_stream_index]->codecpar))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "avcodec_parameters_to_context failed for audio");
		avcodec_free_context(&audio_codec_ctx);
		return false;
	}

	audio_codec_ctx->pkt_timebase = fmt_ctx->streams[audio_stream_index]->time_base;
	audio_codec_ctx->request_sample_fmt = out_sample_fmt;

	if (!ff_ok(avcodec_open2(audio_codec_ctx, decoder, nullptr))) {
		UtilityFunctions::print(filename, ": ", "Couldn't open audio codec %s: %s", avcodec_get_name(audio_codec_ctx->codec_id));
		avcodec_free_context(&audio_codec_ctx);
		return false;
	}

	UtilityFunctions::print(filename, ": ", "Created audio decoder");

	// create resampler
	if (!audio_resampler) {
		audio_resampler = swr_alloc();
	}

	// create an audio frame to hold converted data
	audio_frame = av_frame_ptr();
	audio_frame->format = out_sample_fmt;
	audio_frame->ch_layout = codecpar->ch_layout;
	audio_frame->sample_rate = codecpar->sample_rate;
	audio_frame->nb_samples = 0;

	// create the resampler
	swr_alloc_set_opts2(&audio_resampler,
			&audio_frame->ch_layout, static_cast<AVSampleFormat>(audio_frame->format), audio_frame->sample_rate,
			&audio_codec_ctx->ch_layout, audio_codec_ctx->sample_fmt, audio_codec_ctx->sample_rate,
			0, nullptr);

	if (!ff_ok(swr_init(audio_resampler))) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Failed to initialize audio resampler");
		swr_free(&audio_resampler);
		return false;
	}

	frame_handlers.emplace(audio_stream_index, PacketDecoder(audio_codec_ctx, [&](auto frame) {

		// TODO, do we need tiem checks for audio?
		auto time = frame_time(frame);
		if (time > Clock::now()) {
			return false;
		}

		audio_frame->nb_samples = frame->nb_samples;
		// std::cout << frame->sample_rate << " - " << audio_frame->sample_rate << std::endl;
		// std::cout << frame->sample_rate << " - " << audio_frame->sample_rate << std::endl;

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
		const auto byte_size = av_samples_get_buffer_size(&line_size, audio_frame->ch_layout.nb_channels, audio_frame->nb_samples, static_cast<AVSampleFormat>(frame->format), 0);
		buff.resize(byte_size / sizeof(float));
		memcpy(buff.ptrw(), audio_frame->data[0], byte_size);
		mix_audio(audio_frame->nb_samples, buff, 0);
		return true; }, 10));

	audio_ctx_ready = true;
	return true;
}

GAVPlayback::Clock::time_point GAVPlayback::frame_time(const AVFramePtr &frame) {
	// const auto pts = frame->pts;
	// (av_q2d(video.av_stream->time_base) * double(pts)) : double(pts) * 1e-6
	auto seconds = std::chrono::duration<double>(frame->best_effort_timestamp * av_q2d(video_codec_ctx->pkt_timebase));
	auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(seconds);
	// UtilityFunctions::print(filename, ": ", "millis: ", millis.count());

	// auto frame_time = std::chrono::duration<double>(pts_seconds);
	if (waiting_for_start_time) {
		start_time = Clock::now() - millis;
		waiting_for_start_time = false;
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
void GAVPlayback::cleanup() {
	UtilityFunctions::print(filename, ": ", "Cleanup");
	if (fmt_ctx)
		avformat_close_input(&fmt_ctx);
	if (video_codec_ctx)
		avcodec_free_context(&video_codec_ctx);
	if (audio_codec_ctx)
		avcodec_free_context(&audio_codec_ctx);
	fmt_ctx = nullptr;
	video_codec_ctx = audio_codec_ctx = nullptr;
	video_frame_to_show.reset();
	audio_frame.reset();
	frame_handlers.clear();
}
void GAVPlayback::_stop() {
	// UtilityFunctions::print(filename, ": ", "Stopping playback");
	set_state(STOPPED);
}

void GAVPlayback::_play() {
	if (!file_path_requested) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Cannot play. no filepath given");
		return;
	}
	if (file_path_requested != file_path_loaded) {
		init();
	}
	if (!has_video() && !has_audio()) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "Cannot play. No video or audio stream");
		return;
	}
	set_state(State::PLAYING);
}

void GAVPlayback::_update(double p_delta) {
	if (Engine::get_singleton()->is_editor_hint()) {
		return;
	}

	// make sure the buffer has some data
	for (int i = 0; i < 10; i++) {
		// read more data from the file
		read_next_packet();

		// process frame handlers
		for (auto &val : frame_handlers | std::views::values) {
			val.process();
		}
	}

	// process might have given us a new video frame, update_from_vulkan the texture
	if (video_frame_to_show) {
		// UtilityFunctions::print(filename, ": ", video_frame_to_show->width, "x", video_frame_to_show->height);
		if (accel_config) {
			if (accel_config->device_type == AV_HWDEVICE_TYPE_VULKAN) {
				texture->update_from_vulkan(video_frame_to_show);
			} else {
				texture->update_from_hw(video_frame_to_show);
			}
		} else {
			texture->update_from_sw(video_frame_to_show);
		}
		video_frame_to_show.reset();
	}

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
	UtilityFunctions::print(filename, ": ", "TODO get playback position");
	return 0;
}
void GAVPlayback::_seek(double p_time) {
	UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "TODO: seek, it probably does not work right ATM.");
	// TODO convert p_Time to video timestamp format, maybe this is right, doubt it though
	auto pts = p_time / av_q2d(video_codec_ctx->pkt_timebase);
	avformat_seek_file(fmt_ctx, -1, INT64_MIN, pts, pts, 0);
}

void GAVPlayback::_set_audio_track(int32_t p_idx) {
	UtilityFunctions::print(filename, ": ", "set audio track ", p_idx);
	if (p_idx != 0) {
		UtilityFunctions::UtilityFunctions::printerr(filename, ": ", "set_audio_track > 0 is not supported");
	}
}

Ref<Texture2D> GAVPlayback::_get_texture() const {
	if (!texture_public.is_valid()) {
		texture_public.instantiate();
	}
	return texture_public;
}

int32_t GAVPlayback::_get_channels() const {
	if (!audio_frame) {
		return 0;
	}
	return audio_frame->ch_layout.nb_channels;
}
int32_t GAVPlayback::_get_mix_rate() const {
	if (!audio_frame) {
		return 0;
	}
	return audio_frame->sample_rate;
}