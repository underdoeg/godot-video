#include "gav_playback.h"
#include "gav_settings.h"
#include "gav_singleton.h"

#include <condition_variable>
#include <filesystem>

#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/image_texture.hpp>
#include <godot_cpp/classes/project_settings.hpp>
#include <godot_cpp/classes/rendering_server.hpp>

using namespace godot;

void GAVPlayback::_bind_methods() {
}

GAVPlayback::GAVPlayback() {
	if (gav_settings::verbose_logging()) {
		log.set_level(GAVLog::VERBOSE);
	}
	AvPlayer::codecs.set_reusing_enabled(gav_settings::reuse_decoders());
}
GAVPlayback::~GAVPlayback() {
	if (av) {
		log.info("destruct");
		av->shutdown();
	}
	thread_keep_running = false;
	if (thread.joinable()) {
		thread.join();
	}
}

bool GAVPlayback::load(const String &p_path) {
	threaded = gav_settings::use_threads();
	thread_keep_running = false;
	if (thread.joinable()) {
		thread.join();
	}

	keep_processing = true;

	auto path_global = ProjectSettings::get_singleton()->globalize_path(p_path);
	auto splits = path_global.split("/");
	log.set_name(splits[splits.size() - 1]);

	if (Engine::get_singleton()->is_editor_hint()) {
		auto thumb_path = av_thumbnail_path(path_global.ascii().ptr());
		auto img = Image::load_from_file(thumb_path.c_str());
		// img->load_from_file(thumb_path.c_str());
		thumb_texture = ImageTexture::create_from_image(img);
		return false;
	}

	// create av playback
	av = std::make_shared<AvPlayer>(AvWrapperLog{
			[&](const char *m) { log.verbose(m); },
			[&](const char *m) { log.info(m); },
			[&](const char *m) { log.warn(m); },
			[&](const char *m) { log.error(m); },
	});

	AvPlayerLoadSettings settings;
	settings.file_path = path_global.ascii().ptr();
	settings.output.frame_buffer_size = gav_settings::frame_buffer_size();
	// settings.output.audio_sample_rate = 48000;

	settings.events.end = [&]() {
		log.verbose("end callback");
		video_finished = true;
	};

	bool result = false;
	if (threaded) {
		std::mutex load_mtx;
		std::condition_variable cv;
		std::optional<AvFileInfo> info_from_thread;
		settings.events.video_frame = [&](const AvVideoFrame &frame) {
			AvFramePtr frame_ptr;
			{
				std::scoped_lock<std::mutex> lock(video_mutex);
				if (!video_frames_thread_to_reuse.empty()) {
					frame_ptr = video_frames_thread_to_reuse.front().frame;
					video_frames_thread_to_reuse.pop_front();
				}
			}
			auto copy = frame.copy(frame_ptr);
			{
				std::scoped_lock lock(video_mutex);
				video_frame_thread = copy;
			}
		};

		settings.events.audio_frame = [&](const AvAudioFrame &frame) {
			auto copy = frame.copy();
			std::scoped_lock lock(audio_mutex);
			audio_frames_thread.push_back(copy);
		};

		// settings.events.file_info = [&](const auto &info) {
		// 	std::unique_lock<std::mutex> lck(mtx);
		// 	info_from_thread = info;
		// };

		thread_keep_running = true;

		std::atomic_bool loading_complete = false;
		thread = std::thread([&]() {
			const auto res = av->load(settings);
			// if (!res) {
			info_from_thread = av->get_file_info();
			cv.notify_all();
			loading_complete = true;
			// }
			if (!res) {
				log.info("av->load() failed");
				return;
			}

			while (thread_keep_running) {
				auto sleep_until = std::chrono::high_resolution_clock::now() + std::chrono::milliseconds(1000 / 120);
				av->process();
				std::this_thread::sleep_until(sleep_until);
			}
		});

		log.verbose("------------- waiting for video info -----------------------");
		{
			auto end_time = std::chrono::high_resolution_clock::now() + std::chrono::seconds(10);
			std::unique_lock lck(load_mtx);
			if (!loading_complete) {
				const auto res = cv.wait_until(lck, end_time);
				if (res == std::cv_status::timeout) {
					log.error("av->load() timeout");
				}
			}
		}
		log.info("-------------- done waiting for video info ---------------------");
		if (info_from_thread) {
			log.verbose("received video info from thread");
			set_file_info(info_from_thread.value());
			result = true;
		} else {
			result = false;
		}
	} else {
		settings.events.video_frame = [this](auto &frame) { on_video_frame(frame); };
		settings.events.audio_frame = [this](auto &frame) { on_audio_frame(frame); };
		// settings.events.file_info = [this](auto &info) { set_file_info(info); };
		result = av->load(settings);
		set_file_info(av->get_file_info());
	}

	if (!result) {
		log.error("Failed to load", path_global.ptr());
		file_info.valid = false;
		set_file_info(file_info);
	}
	return result;
}

void GAVPlayback::set_file_info(const AvFileInfo &info) {
	file_info = info;
	if (!texture) {
		texture = std::make_shared<GAVTexture>(RenderingServer::get_singleton()->get_rendering_device());
	}

	if (!file_info.valid) {
		texture->setup({ AV_PIX_FMT_NONE,
				64,
				64 });
		return;
	}
	texture->log.set_level(log.get_level());
	texture->log.set_name(log.get_name() + String(" - texture"));
	texture->setup(info.video);
}

void GAVPlayback::on_video_frame(const AvVideoFrame &frame) const {
	MEASURE;
	// log.info("video");
	if (!texture) {
		log.error("received video frame before texture was created. this should not happen.");
		return;
	}
	texture->update(frame);
}

void convert_audio_samples(ltcsnd_sample_t *outbuffer, uint8_t **inbuffer, uint32_t size, uint32_t channels, AVSampleFormat fmt, uint32_t channel = 0) {
	switch (fmt) {
		case AV_SAMPLE_FMT_S16P: {
			int16_t *lbuf((int16_t *)(inbuffer[channel]));
			for (uint32_t k = 0; k < size; ++k) {
				outbuffer[k] = 128 + 0.00387573 * lbuf[k];
			}
			break;
		}
		case AV_SAMPLE_FMT_S16: {
			int16_t *lbuf((int16_t *)(inbuffer[0]));
			for (uint32_t k = 0; k < size; ++k) {
				outbuffer[k] = 128 + 0.00387573 * lbuf[k * channels + channel];
			}
			break;
		}
		case AV_SAMPLE_FMT_U8: {
			uint8_t *lbuf((uint8_t *)inbuffer);
			for (uint32_t k = 0; k < size; ++k)
				outbuffer[k] = lbuf[k * channels + channel];
			break;
		}
		case AV_SAMPLE_FMT_S32: {
			int32_t *lbuf((int32_t *)(inbuffer[0]));
			for (uint32_t k = 0; k < size; ++k)
				outbuffer[k] = 128 + 5.9139e-08 * lbuf[k * channels + channel];
			break;
		}
		case AV_SAMPLE_FMT_FLT: {
			float *lbuf((float *)(inbuffer[0]));
			for (uint32_t k = 0; k < size; ++k)
				outbuffer[k] = 128 + 127 * lbuf[k * channels + channel];
			break;
		}
		case AV_SAMPLE_FMT_FLTP: {
			float *lbuf((float *)(inbuffer[channel]));
			for (uint32_t k = 0; k < size; ++k) {
				outbuffer[k] = 128 + 127 * lbuf[k];
			}
			break;
		}
		default:
			print_error("Unsupported sample format \"%s\".",
					av_get_sample_fmt_name(fmt));
	}
}

void GAVPlayback::on_audio_frame(const AvAudioFrame &frame) {
	// read the ltc code
	// if (!ltc_decoder) {
	// 	ltc_decoder = ltc_decoder_create(file_info.audio.sample_rate * file_info.time_base.den / std::max(file_info.time_base.num, 1), 160000);
	// 	log.info("ltc decoder created");
	// }
	//
	// LTCFrameExt ltc_frame;
	// ltcsnd_sample_t ltcsamples[frame.frame->nb_samples];
	// convert_audio_samples(ltcsamples, frame.frame->data, frame.frame->nb_samples, 1, AV_SAMPLE_FMT_FLT);
	// ltc_decoder_write(ltc_decoder, ltcsamples, frame.frame->nb_samples, 0);
	//
	// while (ltc_decoder_read(ltc_decoder, &ltc_frame)) {
	// 	SMPTETimecode stime;
	// 	ltc_frame_to_time(&stime, &ltc_frame.ltc, false);
	// 	// log.info(ltc_frame.ltc.user1);
	// 	// log.info(ltc_frame.ltc.user2);
	// 	// log.info(ltc_frame.ltc.user3);
	// 	// log.info(ltc_frame.ltc.user4);
	// 	log.info(ltc_frame_get_user_bits(&ltc_frame.ltc));
	// 	// log.info("SALI");
	// }
	//
	// // LTCFrameExt ltc_frame;
	// // ltcsnd_sample_t sound[1024];
	// // ltc_decoder_write_float(ltc_decoder, reinterpret_cast<float *>(frame.frame->data[0]), frame.frame->nb_samples, 0);
	// //
	// // if (!ltc_decoder_read(ltc_decoder, &ltc_frame)) {
	// // log.error("ltc decoder read failed");
	// // }
	// //
	// // while (ltc_decoder_read(ltc_decoder, &ltc_frame)) {
	// // 	SMPTETimecode ltc_timecode;
	// // 	ltc_frame_to_time(&ltc_timecode, &ltc_frame.ltc, 1);
	// // 	log.info(ltc_timecode.secs);
	// // }

	// if (!ltc_encoder) {
	// 	ltc_encoder = ltc_encoder_create(av->sample_rate(), file_info.video.frame_rate, LTC_TV_625_50, LTC_BGF_DONT_TOUCH);
	// 	ltc_encoder_set_volume(ltc_encoder, -6.0);
	//
	// 	// ltc_encoder = std::make_shared();
	// 	ltc_encoder_set_buffersize(ltc_encoder, av->sample_rate(), file_info.video.frame_rate);
	// }
	// SMPTETimecode timecode;
	// auto seconds = std::chrono::duration_cast<std::chrono::seconds>(frame.millis).count();
	// // auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(frame.millis).count();
	//
	// sprintf(timecode.timezone, "%c%02d%02d", '+', 0, 0);
	//
	// timecode.hours = static_cast<int>(floor(seconds / 3600.0));
	// timecode.mins = static_cast<int>(floor((seconds - 3600.0 * floor(seconds / 3600.0)) / 60.0));
	// timecode.secs = static_cast<int>(floor(seconds)) % 60;
	// const double frame_duration_millis = 1000.0 / file_info.video.frame_rate;
	// timecode.frame = floor((frame.millis.count() % 1000) / frame_duration_millis);
	//
	// ltc_encoder_set_timecode(ltc_encoder, &timecode);
	//
	// ltcsnd_sample_t *buf;
	// ltc_encoder_encode_frame(ltc_encoder);
	// const auto len = ltc_encoder_get_bufferptr(ltc_encoder, &buf, 1);
	//
	// audio_buffer.resize(len);
	//
	// for (int i = 0; i < len; i++) {
	// 	audio_buffer.ptrw()[i] = (buf[i] - 128.f) / 255.f; // / 255.f;
	// }
	// mix_audio(len, audio_buffer, 0);
	//
	// ltc_encoder_inc_timecode(ltc_encoder);
}

void GAVPlayback::_stop() {
	if (!av)
		return;

	// if (threaded) {
	// 	thread_keep_running = false;
	// 	if (thread.joinable()) {
	// 		thread.join();
	// 	}
	// }
	av->stop();
}
void GAVPlayback::_play() {
	if (!av)
		return;
	keep_processing = true;
	av->play();
}

void GAVPlayback::_set_paused(bool p_paused) {
	if (!av)
		return;
	keep_processing = true;
	av->set_paused(p_paused);
}

bool GAVPlayback::_is_paused() const {
	if (!av)
		return false;
	return av->is_paused();
}

bool GAVPlayback::_is_playing() const {
	// godot video player is a bit weird here imho. is playing should always be true (except on stop), otherwise _update is not being called
	// if (!av)
	// 	return false;
	// return av->is_playing();
	return keep_processing;
}

double GAVPlayback::_get_length() const {
	// if (!av) {
	// 	return 0;
	// }
	// return av->duration_seconds();
	return file_info.duration_millis / 1000.0;
}

double GAVPlayback::_get_playback_position() const {
	if (!av) {
		return 0;
	}
	return av->position_seconds();
}

void GAVPlayback::_seek(double p_time) {
}
void GAVPlayback::_set_audio_track(int32_t p_idx) {
}

Ref<Texture2D> GAVPlayback::_get_texture() const {
	if (Engine::get_singleton()->is_editor_hint()) {
		return thumb_texture;
	}
	if (!texture) {
		log.error("_get_texture called before GAVTexture was created. This should not happen");
		return nullptr;
	}
	return texture->get_texture();
}

int32_t GAVPlayback::_get_channels() const {
	log.info("_get_channels ", file_info.audio.num_channels);
	return file_info.audio.num_channels;
}

int32_t GAVPlayback::_get_mix_rate() const {
	if (!av)
		return 0;
	log.info("_get_mix_rate ", av->sample_rate());
	return av->sample_rate();
}
void GAVPlayback::_update(double p_delta) {
	MEASURE_N("MAIN");
	if (video_finished) {
		callbacks.ended();
		video_finished = false;
		keep_processing = false;
	}

	if (threaded) {
		std::optional<AvVideoFrame> frame;
		{
			std::scoped_lock lock(video_mutex);
			if (video_frame_thread) {
				frame = video_frame_thread.value();
				video_frame_thread.reset();
			}
		}
		if (frame) {
			on_video_frame(frame.value());
			{
				std::scoped_lock lock(video_mutex);
				video_frames_thread_to_reuse.push_back(frame.value());
			}
		}
		{
			std::deque<AvAudioFrame> audio_frames;
			{
				std::scoped_lock lock(audio_mutex);
				if (!audio_frames_thread.empty()) {
					audio_frames.insert(audio_frames.end(), audio_frames_thread.begin(), audio_frames_thread.end());
				}
				audio_frames_thread.clear();
			}
			if (!audio_frames.empty()) {
				for (const auto &f : audio_frames) {
					on_audio_frame(f);
				}
			}
		}
	} else {
		if (!av) {
			return;
		}
		av->process();
	}

	if (next_timecode_gen > AvPlayer::Clock::now()) {
		return;
	}

	if (!av->position_seconds()) {
		return;
	}

	if (!ltc_encoder) {
		log.info("create ltc encoder");
		ltc_encoder = ltc_encoder_create(av->sample_rate(), file_info.video.frame_rate, LTC_TV_625_50, LTC_BGF_DONT_TOUCH);
		ltc_encoder_set_volume(ltc_encoder, -3.0);
		ltc_encoder_set_user_bits(ltc_encoder, 550);
	}

	if (!ltc_encoder) {
		log.error("Could not create ltc encoder");
	}

	SMPTETimecode timecode;

	// auto seconds = std::chrono::duration_cast<std::chrono::seconds>(av->position_seconds().millis).count();
	// auto microsec = std::chrono::duration_cast<std::chrono::microseconds>(frame.millis).count();

	auto seconds = av->position_seconds();

	sprintf(timecode.timezone, "%c%02d%02d", '+', 0, 0);

	timecode.hours = static_cast<int>(floor(seconds / 3600.0));
	timecode.mins = static_cast<int>(floor((seconds - 3600.0 * floor(seconds / 3600.0)) / 60.0));
	timecode.secs = static_cast<int>(floor(seconds)) % 60;
	const double frame_duration_millis = 1000.0 / file_info.video.frame_rate;
	timecode.frame = floor(static_cast<int>(floor(seconds * 1000)) % 1000 / frame_duration_millis);

	ltc_encoder_set_timecode(ltc_encoder, &timecode);

	ltc_encoder_encode_frame(ltc_encoder);
	ltcsnd_sample_t *buf;
	ltc_encoder_encode_frame(ltc_encoder);
	const auto len = ltc_encoder_get_bufferptr(ltc_encoder, &buf, 1);

	audio_buffer.resize(len);

	for (int i = 0; i < len; i++) {
		audio_buffer.ptrw()[i] = (buf[i] - 128.f) / 255.f; // / 255.f;
	}
	mix_audio(len, audio_buffer, 0);

	next_timecode_gen += std::chrono::milliseconds(static_cast<int64_t>(frame_duration_millis));
}
