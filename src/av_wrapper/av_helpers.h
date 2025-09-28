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

struct AvPlaneInfo {
	int width, height, depth, line_size;
	size_t byte_size;
};

using AvPlaneInfos = std::vector<AvPlaneInfo>;

AvPlaneInfos av_get_plane_infos(const AVPixelFormat &pixel_format, const int width, const int height);
