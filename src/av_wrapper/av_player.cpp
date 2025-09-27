//
// Created by phwhitfield on 26.09.25.
//

#include "av_wrapper/av_player.h"

#include "av_helpers.h"
#include "godot_cpp/variant/utility_functions.hpp"

#include <map>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/hwcontext_vaapi.h>
#include <libavutil/pixdesc.h>
}

constexpr int max_players = 24;
static std::atomic_int num_players = 0;

constexpr bool reusing_enabled = false;
static std::mutex cached_hw_devices_mtx;
static std::map<AVHWDeviceType, std::deque<AVBufferRef *>> cached_hw_devices;

AVBufferRef *get_used_hw_device(const AVHWDeviceType type) {
	if (!reusing_enabled)
		return nullptr;
	// printf("get device from cache: %s\n", av_hwdevice_get_type_name(type));
	std::scoped_lock<std::mutex> lock(cached_hw_devices_mtx);
	if (cached_hw_devices[type].empty()) {
		return nullptr;
	}
	auto ret = cached_hw_devices[type].front();
	cached_hw_devices[type].pop_front();
	return ret;
}

void reuse_hw_device(const AVHWDeviceType type, AVBufferRef *hw_device) {
	if (!reusing_enabled)
		return;

	// printf("add device to cache: %s\n", av_hwdevice_get_type_name(type));
	std::scoped_lock<std::mutex> lock(cached_hw_devices_mtx);
	cached_hw_devices[type].push_back(hw_device);
}

bool AvPlayer::ff_ok(int result, const std::string &prepend) const {
	if (result < 0) {
		const auto error_str = av_error_string(result);
		if (prepend.empty()) {
			log._error(error_str.c_str());
		} else {
			log.error("{}: {}", prepend, error_str);
		}
		return false;
	}
	return true;
}

AvPlayer::~AvPlayer() {
	reset();
}

void AvPlayer::reset() {
	log.verbose("reset begin");

	audio_frames.clear();
	video_frames.clear();

	// if (video_hw_frames_ref) {
	// 	av_buffer_unref(&video_hw_frames_ref);
	// 	video_hw_frames_ref = nullptr;
	// }

	AVBufferRef *hw_device = nullptr;
	if (video_codec) {
		if (reusing_enabled && video_codec->hw_device_ctx) {
			// reuse hw device types
			hw_device = video_codec->hw_device_ctx;
			video_codec->hw_device_ctx = nullptr;
		}
		avcodec_free_context(&video_codec);
	}

	if (audio_codec) {
		avcodec_free_context(&audio_codec);
	}

	if (fmt_ctx) {
		avformat_close_input(&fmt_ctx);
		--num_players;
	}
	video_codec = audio_codec = nullptr;
	fmt_ctx = nullptr;
	//
	// if (hw_device) {
	// 	reuse_hw_device(output_settings.video_hw_type, hw_device);
	// 	// log.info("added {}", av_hwdevice_get_type_name(output_settings.video_hw_type));
	// }

	output_settings = {};
	filepath_loaded.reset();
	video_stream_index.reset();
	audio_stream_index.reset();
	video_stream = nullptr;
	audio_stream = nullptr;
	file_info = {};
	events = {};

	log.verbose("reset end");
}
void AvPlayer::fill_file_info() {
	file_info.video.width = video_stream->codecpar->width;
	file_info.video.height = video_stream->codecpar->height;
	file_info.video.frame_rate = av_q2d(video_stream->codecpar->framerate);
	file_info.video.codec_name = avcodec_get_name(video_stream->codecpar->codec_id);
	log.info("[Video] size: {}x{}, frame_rate: {}, codec: {}", file_info.video.width, file_info.video.height, file_info.video.frame_rate, file_info.video.codec_name);

	if (audio_stream_index) {
		file_info.audio.num_channels = audio_stream->codecpar->ch_layout.nb_channels;
		file_info.audio.sample_rate = audio_stream->codecpar->sample_rate;
		file_info.audio.codec_name = avcodec_get_name(audio_stream->codecpar->codec_id);
		log.info("[Audio] num channels: {}, sample rate: {}, codec: {}, sample format: {}",
				file_info.audio.num_channels, file_info.audio.sample_rate, file_info.audio.codec_name, audio_stream->codecpar->format);
	}
	if (events.file_info) {
		events.file_info(file_info);
	}
}

bool AvPlayer::load(const AvPlayerLoadSettings &settings) {
	reset();

	events = std::move(settings.events);
	output_settings_requested = settings.output;
	output_settings = output_settings_requested;

	if (!std::filesystem::exists(settings.file_path)) {
		log.error("File not found {}", settings.file_path);
		return false;
	}

	log.verbose("Begin loading of {}", settings.file_path);

	AVDictionary *options = nullptr;
	// av_dict_set(&options, "loglevel", "debug", 0);
	// av_log_set_level(AV_LOG_DEBUG);
	if (!ff_ok(avformat_open_input(&fmt_ctx, settings.file_path.c_str(), nullptr, &options))) {
		log.error("avformat_open_input failed");
		return false;
	}

	log.verbose("avformat_open_input::ok");

	if (!ff_ok(avformat_find_stream_info(fmt_ctx, nullptr))) {
		log.error("avformat_find_stream_info failed");
		return false;
	}

	log.verbose("avformat_find_stream_info::ok");

	// detect stream indices
	auto video_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	auto audio_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);

	if (video_index == AVERROR_STREAM_NOT_FOUND) {
		log.error("Could not find video stream");
		video_stream_index.reset();
		return false;
	}

	video_stream_index = video_index;
	log.verbose("found video stream at index: {}", video_stream_index.value());

	video_stream = fmt_ctx->streams[video_stream_index.value()];

	if (audio_index == AVERROR_STREAM_NOT_FOUND) {
		log.info("No audio stream found");
		audio_stream_index.reset();
	} else {
		audio_stream_index = audio_index;
		audio_stream = fmt_ctx->streams[audio_stream_index.value()];
		log.verbose("found audio stream at index: {}", audio_stream_index.value());
	}

	//
	fill_file_info();

	///////

	filepath_loaded = settings.file_path;
	log.info("file loaded {}", settings.file_path);

	if (num_players < max_players) {
		return init();
	}

	log.info("too many open players {}, waiting with init untile they are closed", static_cast<int>(num_players));

	waiting_for_init = true;

	return true;
}

bool AvPlayer::init() {
	waiting_for_init = false;
	++num_players;

	log.verbose("number of players: {}", static_cast<int>(num_players));

	log.verbose("init begin");

	if (!init_video()) {
		log.error("Could not initialize video stream");
		return false;
	}

	if (audio_stream_index) {
		if (!init_audio()) {
			log.error("Could not initialize audio stream");
			return false;
		}
	}

	ready_for_playback = true;
	log.verbose("init complete");
	return true;
}

bool AvPlayer::init_video() {
	// file_info.video.codec = video_stream->codecpar->codec_type
	log.verbose("begin init_video");

	// setup the decoder
	const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
	if (!decoder) {
		log.error("Could not find videodecoder");
		return false;
	}

	// create the video codec context
	video_codec = avcodec_alloc_context3(decoder);
	if (!video_codec) {
		log.error("Could not allocate video codec context");
		return false;
	}

	if (!ff_ok(avcodec_parameters_to_context(video_codec, video_stream->codecpar))) {
		log.error("Could not set video codec params");
		return false;
	}
	video_codec->time_base = video_stream->time_base;

	if (video_stream->codecpar->codec_id == AV_CODEC_ID_VVC) {
		video_codec->strict_std_compliance = -2;

		/* Enable threaded decoding, VVC decode is slow */
		video_codec->thread_count = 4;
		video_codec->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);
	} else {
		video_codec->thread_count = 1;
	}

	std::vector<const AVCodecHWConfig *> hw_configs;
	// find the best hardware accelerated video config
	int i = 0;
	const AVCodecHWConfig *hw_config = nullptr;
	while ((hw_config = avcodec_get_hw_config(decoder, i++)) != nullptr) {
		if (hw_config->device_type == AV_HWDEVICE_TYPE_CUDA) {
			// cuda is slow, TODO: try to not even compile it into ffmpeg?
			continue;
		}
		hw_configs.push_back(hw_config);
		log.verbose("found hw config: '{}'", av_hwdevice_get_type_name(hw_config->device_type));
	}

	if (hw_configs.empty()) {
		log.error("no supported hw acceleration found");
		return false;
	}

	AVBufferRef *hw_device = nullptr;
	auto create_hw_device = [&](const AVCodecHWConfig *conf) {
		if (auto cached = get_used_hw_device(conf->device_type)) {
			log.verbose("using cached  hw device");
			hw_device = cached;
			return true;
		}

		log.verbose("attempting to create hw device '{}'", av_hwdevice_get_type_name(conf->device_type));

		if (!ff_ok(av_hwdevice_ctx_create(&hw_device, conf->device_type, nullptr, nullptr, 0))) {
			if (hw_device) {
				av_buffer_unref(&hw_device);
				hw_device = nullptr;
			}
		}

		if (hw_device) {
			log.verbose("created hw device: '{}'", av_hwdevice_get_type_name(conf->device_type));
			output_settings.video_hw_type = conf->device_type;
			hw_config = conf;
			return true;
		}
		log.warn("could not create device");
		return false;
	};

	if (output_settings_requested.video_hw_type != AV_HWDEVICE_TYPE_NONE) {
		for (const auto &hw : hw_configs) {
			if (hw->device_type == output_settings.video_hw_type) {
				if (!create_hw_device(hw)) {
					log.warn("Could not create requested hw device type: {}", av_hwdevice_get_type_name(hw->device_type));
				}
				break;
			}
		}
	}

	if (!hw_device) {
		log.verbose("request hw device was not created, attempt autodetect");
		for (const auto &hw : hw_configs) {
			if (create_hw_device(hw)) {
				break;
			}
		}
	}

	if (hw_device) {
		log.verbose("allocate hw device");
		video_hw_frames_ref = av_hwframe_ctx_alloc(hw_device);
		output_settings.video_hw_enabled = true;

		auto *ctx = reinterpret_cast<AVHWFramesContext *>(video_hw_frames_ref->data);
		ctx->format = hw_config->pix_fmt;
		ctx->width = video_codec->width;
		ctx->height = video_codec->height;
		ctx->sw_format = video_codec->sw_pix_fmt;

		// detect valid sw formats
		AVHWFramesConstraints *hw_frames_const = av_hwdevice_get_hwframe_constraints(hw_device, nullptr);
		for (AVPixelFormat *p = hw_frames_const->valid_sw_formats;
				*p != AV_PIX_FMT_NONE; p++) {
			log.verbose("HW decoder pixel supported pixel format: {}", av_get_pix_fmt_name(*p));
			if (ctx->sw_format == AV_PIX_FMT_NONE || *p == AV_PIX_FMT_YUV420P) {
				ctx->sw_format = *p;
			}
		}

		log.verbose("HW decoder using pixel format: {}", av_get_pix_fmt_name(ctx->sw_format));

		if (!ff_ok(av_hwframe_ctx_init(video_hw_frames_ref))) {
			log.error("Could not initialize hw frame");
		} else {
			video_codec->hw_device_ctx = hw_device;
			video_codec->hw_frames_ctx = video_hw_frames_ref;
			// video_codec->pix_fmt = ctx;
		}
	}

	if (!video_codec->hw_device_ctx || !video_codec->hw_frames_ctx->data) {
		output_settings.video_hw_enabled = false;
		output_settings_requested.video_hw_type = AV_HWDEVICE_TYPE_NONE;
		log.warn("could not allocate hw device, will use software decoder. this is going to be slow");
	}

	if (!ff_ok(avcodec_open2(video_codec, decoder, nullptr))) {
		log.error("Could not open video decoder");
		return false;
	}

	log.verbose("end init_video");
	return true;
}

void AvPlayer::video_frame_received(const AvFramePtr &frame) {
	const auto millis = av_get_frame_millis(frame, video_codec);
	video_frames.push_back({ frame,
			millis,
			frame->hw_frames_ctx ? HW_BUFFER : SW_BUFFER,
			video_codec->colorspace });
	if (frame_needs_emit(video_frames.back())) {
		emit_frames();
	}
}

void AvPlayer::emit_video_frame(const AvVideoFrame &frame) const {
	// log.info("emit_video_frame");
	//  only emit software frame buffers for now
	if (!events.video_frame) {
		log.error("no video frame event listener");
		return;
	}

	if (frame.type == HW_BUFFER) {
		auto target = frame;
		target.frame = av_frame_ptr();
		if (!ff_ok(av_hwframe_transfer_data(target.frame.get(), frame.frame.get(), 0))) {
			log.error("Could not transfer_data from hw to sw");
		}
		target.type = SW_BUFFER;
		events.video_frame(target);
	} else {
		events.video_frame(frame);
	}
}

bool AvPlayer::init_audio() {
	log.verbose("begin init_audio");

	// Set output format to stereo
	const auto audio_decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
	if (!audio_decoder) {
		log.error("Could not find audio decoder for codec {}", file_info.audio.codec_name);
		return false;
	}

	audio_codec = avcodec_alloc_context3(audio_decoder);
	if (!audio_codec) {
		log.error("Could not allocate audio codec context for codec {}", file_info.audio.codec_name);
		return false;
	}

	if (!ff_ok(avcodec_parameters_to_context(audio_codec, audio_stream->codecpar))) {
		log.error("could not set audio_codec parameters");
		return false;
	}

	audio_codec->pkt_timebase = audio_stream->time_base;
	audio_codec->request_sample_fmt = output_settings_requested.audio_sample_fmt;

	if (!ff_ok(avcodec_open2(audio_codec, audio_decoder, nullptr))) {
		log.error("Could not open audio codec for codec {}", file_info.audio.codec_name);
		return false;
	}

	output_settings.audio_sample_rate = output_settings_requested.audio_sample_rate > 0 ? output_settings_requested.audio_sample_rate : audio_codec->sample_rate;

	if (!audio_resampler) {
		// create the resampler
		swr_alloc_set_opts2(&audio_resampler,
				&audio_codec->ch_layout,
				output_settings.audio_sample_fmt,
				output_settings.audio_sample_rate,
				&audio_codec->ch_layout, audio_codec->sample_fmt, audio_codec->sample_rate,
				0, nullptr);
		if (!ff_ok(swr_init(audio_resampler))) {
			log.error("Could not initialize audio resampler");
			return false;
		}
	}

	log.verbose("end init_audio");
	return true;
}

void AvPlayer::emit_audio_frame(const AvAudioFrame &frame) const {
	// log.info("emit_audio_frame");
	if (!events.audio_frame) {
		log.error("no audio frame event listener");
		return;
	}
	// log.info("format: ")
	auto target = frame;
	target.frame = av_frame_ptr();

	target.frame->format = output_settings.audio_sample_fmt;
	target.frame->ch_layout = frame.frame->ch_layout;
	target.frame->sample_rate = output_settings.audio_sample_rate;
	target.frame->nb_samples = frame.frame->nb_samples;

	if (!ff_ok(swr_convert_frame(audio_resampler, target.frame.get(), frame.frame.get()), "swr_convert_frame")) {
		return;
	}

	int line_size = 0;
	target.byte_size = av_samples_get_buffer_size(
			&line_size,
			target.frame->ch_layout.nb_channels,
			target.frame->nb_samples,
			static_cast<AVSampleFormat>(target.frame->format),
			0);

	events.audio_frame(target);
}

void AvPlayer::read_next_frames() {
	av_packet_unref(packet.get());

	const auto ret = av_read_frame(fmt_ctx, packet.get());
	if (ret == AVERROR_EOF) {
		log.info("eof");
		is_eof = true;
		return;
	}
	if (!ff_ok(ret)) {
		log.error("Could not read frame");
		return;
	}

	// get the correct codec context for the stream index
	AVCodecContext *codec = nullptr;
	if (packet->stream_index == video_stream_index.value()) {
		codec = video_codec;
	} else if (audio_stream_index && packet->stream_index == audio_stream_index.value()) {
		codec = audio_codec;
	}

	if (!codec) {
		// we do not care about this stream
		return;
	}

	if (!ff_ok(avcodec_send_packet(codec, packet.get()), "avcodec_send_packet")) {
		return;
	}

	// AvFramePtr current_frame;
	int receive_counter = 0;
	int receive_res = 0;
	while (receive_res == 0) {
		const auto frame = av_frame_ptr();
		if (receive_counter > 100) {
			log.verbose("breaking out of receive loop: {}", receive_counter);
		}
		receive_res = avcodec_receive_frame(codec, frame.get());
		if (receive_res == 0) {
			frame_received(frame, packet->stream_index);
		}
		receive_counter++;
	}
}

void AvPlayer::frame_received(const AvFramePtr &frame, const int stream_index) {
	// find millis of frame

	if (stream_index == video_stream_index.value()) {
		video_frame_received(frame);
	} else if (audio_stream_index && stream_index == audio_stream_index.value()) {
		audio_frame_received(frame);
	}
}

void AvPlayer::audio_frame_received(const AvFramePtr &frame) {
	auto millis = av_get_frame_millis(frame, audio_codec);
	// log.info("audio_frame_received - num channels: {}, sample rate: {}, sample format: {}",
	// 		frame->ch_layout.nb_channels, frame->sample_rate, static_cast<int>(frame->format));

	audio_frames.push_back({ frame, millis, 0 });
	if (frame_needs_emit(audio_frames.back())) {
		emit_frames();
	}
}

bool AvPlayer::frame_needs_emit(const AvBaseFrame &f) const {
	if (!has_started())
		return false;
	return start_time.value() + f.millis <= Clock::now();
}

void AvPlayer::stop() {
	reset();
}

void AvPlayer::fill_buffers() {
	// read new frames
	// if the video has not yet started, the process is simple, fill up the  buffer
	if (!has_started()) {
		while (!is_eof && buffer_size() < output_settings.frame_buffer_size) {
			read_next_frames();
		}
	} else {
		// also check if the newest frame should be displayed, if so it is a buffer underrun and handle it right away
		while (!is_eof && buffer_size() < output_settings.frame_buffer_size) {
			read_next_frames();
			// // check if a frame needs showing, abort if true
			// for (const auto &f : audio_frames) {
			// 	if (frame_needs_emit(f)) {
			// 		break;
			// 	}
			// }
			// for (const auto &f : video_frames) {
			// 	if (frame_needs_emit(f)) {
			// 		break;
			// 	}
			// }
		}

		if (!is_eof) {
			const auto buff_size = buffer_size();
			if (buff_size < output_settings.frame_buffer_size) {
				log.warn("buffer underrun: {} / {}", buff_size, output_settings.frame_buffer_size);
			}
		}
	}
}

void AvPlayer::emit_frames() {
	// only start the clock if we have some frames
	if (video_frames.size() || audio_frames.size()) {
		if (!has_started()) {
			start_time = Clock::now();
		}

		// first handle audio
		while (!audio_frames.empty()) {
			if (frame_needs_emit(audio_frames.front())) {
				emit_audio_frame(audio_frames.front());
				audio_frames.pop_front();
			} else {
				break;
			}
		}

		// then video with frame dropping
		std::optional<AvVideoFrame> result;
		int frame_drops = -1;
		while (!video_frames.empty()) {
			if (frame_needs_emit(video_frames.front())) {
				result = video_frames.front();
				video_frames.pop_front();
				frame_drops++;
			} else {
				break;
			}
		}
		if (frame_drops > 0) {
			log.verbose("dropped {} video frames", frame_drops);
		}
		if (result) {
			emit_video_frame(result.value());
		}
	} else if (is_playing()) {
		if (is_eof) {
			// video is done, and all frames were submitted
			if (events.end) {
				events.end();
			}
		}
	}
}

void AvPlayer::process() {
	if (waiting_for_init && num_players < max_players) {
		init();
	}
	if (!playing)
		return;

	if (!ready_for_playback) {
		return;
	}
	emit_frames();
	fill_buffers();
}