//
// Created by phwhitfield on 26.09.25.
//

#include "av_wrapper/av_player.h"

#include "av_codecs.h"
#include "av_helpers.h"
#include "godot_cpp/variant/utility_functions.hpp"

#include <thread>

#include <map>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/pixdesc.h>
}

AvCodecs AvPlayer::codecs;

AvVideoFrame AvVideoFrame::copy(const AvFramePtr &av_frame) const {
	AvVideoFrame copy = *this;
	copy.frame = av_frame_clone(frame, av_frame);
	return copy;
}
AvAudioFrame AvAudioFrame::copy(const AvFramePtr &av_frame) const {
	AvAudioFrame copy = *this;
	copy.frame = av_frame_clone(frame, av_frame);
	return copy;
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

	paused = false;
	start_time.reset();
	video_frames.clear();
	audio_frames.clear();
	ready_for_playback = false;
	is_eof = false;

	codecs.release(video_codec);
	video_codec = nullptr;
	codecs.release(audio_codec);
	audio_codec = nullptr;

	if (fmt_ctx) {
		avformat_close_input(&fmt_ctx);
		log.info("close fmt_ctx");
	}
	video_codec = audio_codec = nullptr;
	fmt_ctx = nullptr;

	output_settings = {};
	filepath_loaded.reset();
	video_stream_index.reset();
	audio_stream_index.reset();

	video_stream = nullptr;
	audio_stream = nullptr;
	file_info = {};
	waiting_for_init = false;
	num_commands = 0;

	log.verbose("reset done");
}
void AvPlayer::fill_file_info() {
	if (video_stream_index && video_stream) {
		file_info.video.width = video_stream->codecpar->width;
		file_info.video.height = video_stream->codecpar->height;
		file_info.video.frame_rate = av_q2d(video_stream->codecpar->framerate);
		file_info.video.codec_name = avcodec_get_name(video_stream->codecpar->codec_id);
		auto seconds = std::chrono::duration<double>(static_cast<double>(video_stream->duration) * av_q2d(video_stream->time_base));
		file_info.duration_millis = std::chrono::duration_cast<std::chrono::milliseconds>(seconds).count();
		log.info("[Video] size: {}x{}, frame_rate: {}, codec: {}", file_info.video.width, file_info.video.height, file_info.video.frame_rate, file_info.video.codec_name);
		file_info.valid = true;
	} else {
		file_info.valid = false;
	}
	if (audio_stream_index && audio_stream) {
		file_info.audio.num_channels = audio_stream->codecpar->ch_layout.nb_channels;
		file_info.audio.sample_rate = audio_stream->codecpar->sample_rate;
		file_info.audio.codec_name = avcodec_get_name(audio_stream->codecpar->codec_id);
		log.info("[Audio] num channels: {}, sample rate: {}, codec: {}, sample format: {}",
				file_info.audio.num_channels, file_info.audio.sample_rate, file_info.audio.codec_name, audio_stream->codecpar->format);
	}
	// if (load_settings.events.file_info) {
	// 	load_settings.events.file_info(file_info);
	// }
}

bool AvPlayer::load(const AvPlayerLoadSettings &settings) {
	reset();

	// bool is_path_new = load_settings.file_path != settings.file_path;

	load_settings = settings;

	output_settings_requested = settings.output;
	output_settings = output_settings_requested;
	std::filesystem::path file_path = settings.file_path;

	if (!std::filesystem::exists(file_path)) {
		log.error("File not found {}", file_path.string());
		return false;
	}

	log.verbose("Begin loading of {}", file_path.string());

	AVDictionary *options = nullptr;
	// av_dict_set(&options, "loglevel", "debug", 0);
	// av_log_set_level(AV_LOG_DEBUG);
	if (!ff_ok(avformat_open_input(&fmt_ctx, file_path.c_str(), nullptr, &options))) {
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
		output_sample_rate = output_settings_requested.audio_sample_rate > 0 ? output_settings_requested.audio_sample_rate : audio_stream->codecpar->sample_rate;
		output_settings.audio_sample_rate = output_sample_rate;
		log.verbose("found audio stream at index: {}", audio_stream_index.value());
	}

	//
	// if (is_path_new)
	fill_file_info();

	///////
	const auto res = init();
	if (res == AvCodecs::ERROR) {
		log.error("Error initializing");
		return false;
	}

	playing = true;

	filepath_loaded = file_path;

	if (res == AvCodecs::AGAIN) {
		log.info("will try to init again");
		waiting_for_init = true;
	} else {
		log.info("file loaded {}", file_path.string());
	}

	return true;
}

AvCodecs::ResultType AvPlayer::init() {
	log.verbose("init begin");

	auto res = init_video();
	if (res == AvCodecs::AGAIN) {
		log.warn("could not yet initialize video stream. will try again");
		return res;
	}
	if (res != AvCodecs::OK) {
		log.error("Could not initialize video stream");
		return res;
	}

	if (audio_stream_index) {
		res = init_audio();
		if (res != AvCodecs::OK) {
			if (video_codec) {
				video_codec.reset();
			}
			if (res == AvCodecs::AGAIN) {
				log.warn("could not yet initialize video stream. will try again");
				return res;
			}
			log.error("Could not initialize audio stream");
			return res;
		}
	}

	ready_for_playback = true;
	waiting_for_init = false;
	log.verbose("init complete");
	return res;
}
AvCodecContextPtr AvPlayer::create_video_codec_context() {
	// setup the decoder
	const AVCodec *decoder = avcodec_find_decoder(video_stream->codecpar->codec_id);
	if (!decoder) {
		log.error("Could not find videodecoder");
		return {};
	}

	// create the video codec context
	auto codec = avcodec_context_ptr(decoder);
	if (!codec) {
		log.error("Could not allocate video codec context");
		return {};
	}

	if (!ff_ok(avcodec_parameters_to_context(codec.get(), video_stream->codecpar))) {
		log.error("Could not set video codec params");
		return {};
	}
	codec->time_base = video_stream->time_base;

	if (video_stream->codecpar->codec_id == AV_CODEC_ID_VVC) {
		codec->strict_std_compliance = -2;

		/* Enable threaded decoding, VVC decode is slow */
		codec->thread_count = 4;
		codec->thread_type = (FF_THREAD_FRAME | FF_THREAD_SLICE);
	} else {
		codec->thread_count = 1;
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
		return {};
	}

	AVBufferRef *hw_device = nullptr;
	auto create_hw_device = [&](const AVCodecHWConfig *conf) {
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
		auto video_hw_frames_ref = av_hwframe_ctx_alloc(hw_device);
		output_settings.video_hw_enabled = true;

		auto *ctx = reinterpret_cast<AVHWFramesContext *>(video_hw_frames_ref->data);
		ctx->format = hw_config->pix_fmt;
		ctx->width = codec->width;
		ctx->height = codec->height;
		ctx->sw_format = codec->sw_pix_fmt;

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
			codec->hw_device_ctx = hw_device;
			codec->hw_frames_ctx = video_hw_frames_ref;
			// video_codec->pix_fmt = ctx;
		}
	}

	if (!codec->hw_device_ctx || !codec->hw_frames_ctx->data) {
		output_settings.video_hw_enabled = false;
		output_settings_requested.video_hw_type = AV_HWDEVICE_TYPE_NONE;
		//log.warn("could not allocate hw device, will use software decoder. this is going to be slow");
		return {};
	}

	if (!ff_ok(avcodec_open2(codec.get(), decoder, nullptr))) {
		log.error("Could not open video decoder");
		return {};
	}
	return codec;
}

AvCodecs::ResultType AvPlayer::init_video() {
	// file_info.video.codec = video_stream->codecpar->codec_type
	log.verbose("begin init_video");
	if (!video_stream) {
		return AvCodecs::ERROR;
	}

	const auto [codec, result] = codecs.get_or_create(video_stream, std::bind(&AvPlayer::create_video_codec_context, this));
	if (result != AvCodecs::OK) {
		return result;
	}
	video_codec = codec;
	log.verbose("end init_video");
	return AvCodecs::OK;
}

bool AvPlayer::video_frame_received(const AvFramePtr &frame) {
	const auto millis = av_get_frame_millis_ptr(frame, video_codec);
	video_frames.push_back({ frame,
			millis,
			frame->hw_frames_ctx ? HW_BUFFER : SW_BUFFER,
			video_codec->colorspace });
	return frame_needs_emit(video_frames.back());
}

void AvPlayer::emit_video_frame(const AvVideoFrame &frame) {
	MEASURE;
	// log.info("emit_video_frame");
	//  only emit software frame buffers for now
	if (!load_settings.events.video_frame) {
		log.error("no video frame event listener");
		return;
	}

	if (frame.type == HW_BUFFER) {
		MEASURE_N("hw transfer");
		auto target = frame;
		if (!video_transfer_frame)
			video_transfer_frame = av_frame_ptr();
		target.frame = video_transfer_frame;
		if (!ff_ok(av_hwframe_transfer_data(target.frame.get(), frame.frame.get(), 0))) {
			log.error("Could not transfer_data from hw to sw");
		}
		target.type = SW_BUFFER;
		load_settings.events.video_frame(target);
	} else {
		load_settings.events.video_frame(frame);
	}
	av_frame_unref(frame.frame.get());
	video_frames_to_reuse.push_back(frame.frame);
}

AvCodecs::ResultType AvPlayer::init_audio() {
	log.verbose("begin init_audio");

	const auto [codec, res] = codecs.get_or_create(audio_stream, std::bind(&AvPlayer::create_audio_codec_context, this));

	if (res != AvCodecs::OK) {
		return res;
	}

	audio_codec = codec;

	if (!audio_resampler) {
		// create the resampler
		swr_alloc_set_opts2(&audio_resampler,
				&audio_codec->ch_layout,
				output_settings.audio_sample_fmt,
				output_sample_rate,
				&audio_codec->ch_layout, audio_codec->sample_fmt, audio_codec->sample_rate,
				0, nullptr);
		if (!ff_ok(swr_init(audio_resampler))) {
			log.error("Could not initialize audio resampler");
			return AvCodecs::ERROR;
		}
	}

	log.verbose("end init_audio");
	return AvCodecs::OK;
}

AvCodecContextPtr AvPlayer::create_audio_codec_context() {
	// Set output format to stereo
	const auto audio_decoder = avcodec_find_decoder(audio_stream->codecpar->codec_id);
	if (!audio_decoder) {
		log.error("Could not find audio decoder for codec {}", file_info.audio.codec_name);
		return {};
	}

	auto codec = avcodec_context_ptr(audio_decoder);
	if (!codec) {
		log.error("Could not allocate audio codec context for codec {}", file_info.audio.codec_name);
		return {};
	}

	if (!ff_ok(avcodec_parameters_to_context(codec.get(), audio_stream->codecpar))) {
		log.error("could not set audio_codec parameters");
		return {};
	}

	codec->pkt_timebase = audio_stream->time_base;
	codec->request_sample_fmt = output_settings_requested.audio_sample_fmt;

	if (!ff_ok(avcodec_open2(codec.get(), audio_decoder, nullptr))) {
		log.error("Could not open audio codec for codec {}", file_info.audio.codec_name);
		return {};
	}
	return codec;
}

void AvPlayer::emit_audio_frame(const AvAudioFrame &frame) {
	MEASURE;
	// log.info("emit_audio_frame");
	if (!load_settings.events.audio_frame) {
		log.error("no audio frame event listener");
		return;
	}
	// log.info("format: ")
	AvAudioFrame target = frame;
	if (audio_frames_to_reuse.size()) {
		target.frame = audio_frames_to_reuse.front();
		audio_frames_to_reuse.pop_front();
	} else {
		target.frame = av_frame_ptr();
	}

	target.frame->format = output_settings.audio_sample_fmt;
	target.frame->ch_layout = frame.frame->ch_layout;
	target.frame->sample_rate = output_settings.audio_sample_rate;
	target.frame->nb_samples = frame.frame->nb_samples;

	if (!ff_ok(swr_convert_frame(audio_resampler, target.frame.get(), frame.frame.get()), "swr_convert_frame")) {
		return;
	}

	if (target.frame->format != output_settings.audio_sample_fmt) {
		log.error("Audio resampled data format doesn't match");
		return;
	}
	if (target.frame->sample_rate != output_settings.audio_sample_rate) {
		log.error("Audio sample rate doesn't match");
		return;
	}

	int line_size = 0;
	target.byte_size = av_samples_get_buffer_size(
			&line_size,
			target.frame->ch_layout.nb_channels,
			target.frame->nb_samples,
			static_cast<AVSampleFormat>(target.frame->format),
			0);

	load_settings.events.audio_frame(target);
	av_frame_unref(target.frame.get());
	audio_frames_to_reuse.push_back(target.frame);
}

bool AvPlayer::read_next_frames() {
	av_packet_unref(packet.get());

	const auto ret = av_read_frame(fmt_ctx, packet.get());
	if (ret == AVERROR_EOF) {
		is_eof = true;
		return false;
	}
	if (!ff_ok(ret)) {
		log.error("Could not read frame");
		return false;
	}

	// get the correct codec context for the stream index
	AvCodecContextPtr codec;
	bool is_audio = false;
	if (packet->stream_index == video_stream_index.value()) {
		codec = video_codec;
	} else if (audio_stream_index && packet->stream_index == audio_stream_index.value()) {
		codec = audio_codec;
		is_audio = true;
	}

	if (!codec) {
		// we do not care about this stream
		return false;
	}

	if (!ff_ok(avcodec_send_packet(codec.get(), packet.get()), "avcodec_send_packet")) {
		return false;
	}

	const auto get_frame_ptr = [&] {
		// reuse frames if possible
		auto &frames_to_reuse = is_audio ? audio_frames_to_reuse : video_frames_to_reuse;
		if (frames_to_reuse.empty()) {
			return av_frame_ptr();
		}
		auto frame = frames_to_reuse.front();
		frames_to_reuse.pop_front();
		return frame;
	};

	// AvFramePtr current_frame;
	int receive_res = 0;
	bool frame_need_emit = false;
	while (receive_res == 0) {
		const auto frame = get_frame_ptr();
		receive_res = avcodec_receive_frame(codec.get(), frame.get());
		if (receive_res == 0) {
			if (frame_received(frame, packet->stream_index)) {
				frame_need_emit = true;
			}
		}
	}

	if (receive_res == AVERROR(EAGAIN)) {
		// all good
		return frame_need_emit;
	}

	if (receive_res == AVERROR_EOF) {
		is_eof = true;
		return frame_need_emit;
	}

	// a fatal error occured, finish it
	log.error("Error receiving frame {}", av_error_string(receive_res));
	is_eof = true;

	return frame_need_emit;
}

bool AvPlayer::frame_received(const AvFramePtr &frame, const int stream_index) {
	// find millis of frame

	if (stream_index == video_stream_index.value()) {
		return video_frame_received(frame);
	}
	if (audio_stream_index && stream_index == audio_stream_index.value()) {
		return audio_frame_received(frame);
	}
	return false;
}

bool AvPlayer::audio_frame_received(const AvFramePtr &frame) {
	const auto millis = av_get_frame_millis_ptr(frame, audio_codec);
	audio_frames.push_back({ frame, millis, 0 });
	return frame_needs_emit(audio_frames.back());
}

bool AvPlayer::frame_needs_emit(const AvBaseFrame &f) const {
	if (!has_started())
		return false;
	auto frame_time = start_time.value() + f.millis;
	// auto due_in = frame_time - Clock::now();
	return frame_time < Clock::now() + std::chrono::milliseconds(1000 / 60);
}

void AvPlayer::stop() {
	add_command(STOP);
}
void AvPlayer::play() {
	add_command(PLAY);
}
void AvPlayer::set_paused(bool state) {
	log.info("set paused {}", state);
	if (state) {
		add_command(PAUSE);
	} else {
		play();
	}
}

void AvPlayer::fill_buffers() {
	MEASURE;
	// read new frames
	// if the video has not yet started, the process is simple, fill up the  buffer
	// if (!has_started()) {
	// 	while (!is_eof && buffer_size() < output_settings.frame_buffer_size) {
	// 		if (!read_next_frames()) break;
	// 	}
	// } else {
	// also check if the newest frame should be displayed, if so it is a buffer underrun and handle it right away
	while (!is_eof && buffer_size() < output_settings.frame_buffer_size) {
		if (read_next_frames()) {
			break;
		}
	}

	if (!is_eof) {
		const auto buff_size = buffer_size();
		if (buff_size < output_settings.frame_buffer_size) {
			// log.warn("buffer underrun: {} / {}", buff_size, output_settings.frame_buffer_size);
		}
	}
	// }
}

void AvPlayer::emit_frames() {
	MEASURE;
	// only start the clock if we have some frames
	if (video_frames.size() || audio_frames.size()) {
		if (!has_started()) {
			start_time = Clock::now();
			// log.verbose("start time set to: {}", start_time.value());
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
			static std::atomic_int frame_drop_counter = 0;
			if (frame_drop_counter % 30 == 0) {
				log.warn("dropped {} video frames", frame_drops);
			}
			++frame_drop_counter;
		}
		if (result) {
			position_millis = result.value().millis.count();
			emit_video_frame(result.value());
		}
	} else if (is_playing()) {
		if (is_eof) {
			log.info("video is done");
			// video is done, and all frames were submitted
			playing = false;
			paused = false;
			if (load_settings.events.end) {
				log.verbose("call end callback");
				load_settings.events.end();
			}
		}
	}
}

void AvPlayer::add_command(Command command) {
	switch (command) {
		case STOP:
			log.verbose("add_command: STOP");
			break;
		case PLAY:
			log.verbose("add_command: PLAY");
			break;
		case PAUSE:
			log.verbose("add_command: PAUSE");
			break;
		case SHUTDOWN:
			log.verbose("add_command: SHUTDOWN");
			break;
	}
	std::unique_lock lock(command_queue_mutex);
	if (num_commands >= command_queue.size()) {
		log.warn("cannot add new command, command queue is full");
		return;
	}
	command_queue[num_commands] = command;
	num_commands++;
}

void AvPlayer::handle_commands() {
	MEASURE;

	command_queue_mutex.lock();
	const auto commands = command_queue;
	const auto count = num_commands;
	num_commands = 0;
	command_queue_mutex.unlock();

	for (int i = 0; i < count; i++) {
		auto cmd = commands[i];
		switch (cmd) {
			case STOP:
				log.info("stop");
				reset();
				break;
			case PAUSE:
				if (!playing) {
					log.warn("cannot pause, video is not playing");
					break;
				}
				log.info("pause");
				if (!paused) {
					pause_time = Clock::now();
				}
				paused = true;
				break;
			case PLAY:
				log.info("play");
				if (!filepath_loaded && !load_settings.file_path.empty() || is_eof) {
					log.info("file is not loaded. try to load {}", load_settings.file_path);
					load(load_settings);
				} else if (paused && start_time.has_value()) {
					auto duration = (Clock::now() - pause_time);
					log.verbose("pause lasted {} seconds", std::chrono::duration_cast<std::chrono::seconds>(duration).count());
					start_time = start_time.value() + duration;
				}
				paused = false;
				playing = true;
				break;
			case SHUTDOWN:
				log.info("shutdown");
				reset();
				load_settings = {};
		}
	}
}

void AvPlayer::process() {
	MEASURE;

	handle_commands();

	if (waiting_for_init) {
		init();
	}

	// log.info("ready_for_playback: {}, playing: {}, paused: {}", ready_for_playback.load(), playing.load(), paused.load());

	if (!ready_for_playback)
		return;
	if (!playing)
		return;
	if (paused)
		return;
	emit_frames();
	fill_buffers();
}
