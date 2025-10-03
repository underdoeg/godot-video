#pragma once

#include <functional>

#include "av_helpers.h"

#include <atomic>
#include <deque>
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}

class AvCodecs {
	std::mutex mutex;

	const int max_open_codecs = 36;
	std::atomic_int num_open_codecs = 0;

	std::atomic_bool reusing_enabled = true;

	struct CodecInfo {
		AVMediaType type;
		AVCodecID codec;
		int width;
		int height;
		int sample_rate;
		int channels;
		bool operator==(const CodecInfo &other) const {
			return type == other.type && codec == other.codec && width == other.width && height == other.height && sample_rate == other.sample_rate && channels == other.channels;
		}
	};

	struct CodecEntry {
		CodecInfo info;
		AvCodecContextPtr context;
	};

	std::deque<CodecEntry> codecs;

public:
	enum ResultType {
		OK,
		AGAIN,
		ERROR
	};

	using Result = std::tuple<AvCodecContextPtr, ResultType>;

	Result get_or_create(AVStream *stream, const std::function<AvCodecContextPtr()> &create);
	void release(AvCodecContextPtr codec);
	void set_reusing_enabled(bool reusing_enabled);
	void cleanup();
};
