#pragma once

#include <chrono>
#include <memory>
#include <string>


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavutil/frame.h>
}

std::string av_error_string(int error);

using AvFramePtr = std::shared_ptr<AVFrame>;
AvFramePtr av_frame_ptr();

AvFramePtr av_frame_clone(AvFramePtr src, AvFramePtr dst = av_frame_ptr());

using AvPacketPtr = std::shared_ptr<AVPacket>;
AvPacketPtr av_packet_ptr();

std::chrono::milliseconds av_get_frame_millis(const AvFramePtr &frame, const AVCodecContext *codec);

std::string av_thumbnail_path(const std::string &video_path);

struct AvPlaneInfo {
	int width, height, depth, line_size;
	size_t byte_size;
};

using AvPlaneInfos = std::vector<AvPlaneInfo>;

AvPlaneInfos av_get_plane_infos(const AVPixelFormat &pixel_format, const int width, const int height);

class TimeMeasure {
	std::chrono::high_resolution_clock::time_point start_time;
	std::string name;

public:
	explicit TimeMeasure(const std::string n) :
			start_time(std::chrono::high_resolution_clock::now()), name(n) {}
	~TimeMeasure();
};

// #define ENABLE_MEASURE 1

#ifdef ENABLE_MEASURE
#define MEASURE_N(name) TimeMeasure time_track_n(std::string(__FUNCTION__) + "::" + std::string(name))
#define MEASURE TimeMeasure time_track(__FUNCTION__)
#else
#define MEASURE void()
#define MEASURE_N(name) void()
#endif
