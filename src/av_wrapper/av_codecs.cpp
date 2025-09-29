//
// Created by phwhitfield on 29.09.25.
//

#include "av_codecs.h"

#include <mutex>

AvCodecs::Result AvCodecs::get_or_create(AVStream *stream, const std::function<AvCodecContextPtr()> &create) {
	const auto create_result = [&]() -> Result {
		++num_open_codecs;
		if (auto res = create()) {
			return { res, OK };
		}
		--num_open_codecs;
		return { {}, ERROR };
	};

	if (!reusing_enabled) {
		if (num_open_codecs < max_open_codecs) {
			return create_result();
		}
		printf("cannot create a new codec => max open codecs reached: %d\n", max_open_codecs);
		return { {}, AGAIN };
	}
	auto codecpar = stream->codecpar;
	const CodecInfo info = {
		codecpar->codec_type,
		codecpar->codec_id,
		codecpar->width,
		codecpar->height,
		codecpar->sample_rate,
		codecpar->ch_layout.nb_channels,
	};
	const auto find_codec = [&] {
		int index = -1;
		for (int i = 0; i < codecs.size(); ++i) {
			if (codecs[i].info == info) {
				index = i;
				break;
			}
		}
		if (index >= 0) {
			auto ret = codecs[index];
			codecs.erase(codecs.begin() + index);
			return ret.context;
		}
		return AvCodecContextPtr();
	};

	{
		// scoped locking
		std::unique_lock lock(mutex);
		if (auto codec = find_codec()) {
			return { codec, OK };
		}
	}

	// keep create out of scoped lock
	if (num_open_codecs < max_open_codecs) {
		return create_result();
	}

	{
		// scoped
		std::unique_lock lock(mutex);
		if (codecs.empty()) {
			printf("cannot create a new codec => max open codecs reached: %d\n", max_open_codecs);
			return { {}, AGAIN };
		}
		printf("max open codecs reached (%d). will remove an incompatible codec from cache\n", max_open_codecs);
		codecs.front().context.reset();
		codecs.pop_front();
	}
	return create_result();
}
void AvCodecs::release(AvCodecContextPtr codec) {
	if (!codec) {
		return;
	}
	if (!reusing_enabled) {
		--num_open_codecs;
		codec.reset();
		return;
	}
	const CodecInfo info = {
		codec->codec_type,
		codec->codec_id,
		codec->width,
		codec->height,
		codec->sample_rate,
		codec->ch_layout.nb_channels,
	};
	avcodec_flush_buffers(codec.get());
	std::unique_lock<std::mutex> lock(mutex);
	codecs.emplace_back(info, codec);
	--num_open_codecs;
}
void AvCodecs::set_reusing_enabled(bool p_reusing_enabled) {
	reusing_enabled = p_reusing_enabled;
	if (!reusing_enabled) {
		std::lock_guard<std::mutex> lock(mutex);
		codecs.clear();
	}
}
