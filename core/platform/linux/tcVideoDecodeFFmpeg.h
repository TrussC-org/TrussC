#pragma once

#include "tc/video/internal/tcVideoDecodeLimits.h"
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace trussc::internal {

// Configure these before open/probe; duration is deliberately unrestricted.
inline bool openVideoInput(AVFormatContext** context, const char* path, VideoDecodeStatus& status) {
    *context = avformat_alloc_context();
    if (!*context) return false;
    // Bound (security): demux probing/indexes retain external metadata; configure FFmpeg before opening. Overflow rejects the whole input and is counted.
    (*context)->probesize = 4 * 1024 * 1024;
    (*context)->format_probesize = 1024 * 1024;
    (*context)->max_analyze_duration = 5 * AV_TIME_BASE;
    (*context)->max_index_size = VideoDecodeLimits::inputBytes / (std::max)(1, (*context)->max_streams);
    (*context)->max_picture_buffer = VideoDecodeLimits::inputBytes;
    (*context)->max_probe_packets = 64;
    AVDictionary* options = nullptr;
    int result = avformat_open_input(context, path, nullptr, &options);
    av_dict_free(&options);
    if (result < 0) { status.reject(0, VideoDecodeLimits::inputBytes); avformat_close_input(context); return false; }
    for (unsigned i = 0; i < (*context)->nb_streams; ++i) {
        const auto* parameters = (*context)->streams[i]->codecpar;
        size_t bytes = 0;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && parameters->width && parameters->height &&
            !VideoDecodeLimits::rgba(parameters->width, parameters->height, bytes)) {
            status.reject(bytes, 0);
            avformat_close_input(context); return false;
        }
        if (parameters->extradata_size < 0 || size_t(parameters->extradata_size) > VideoDecodeLimits::inputBytes) {
            status.reject(parameters->extradata_size < 0 ? 0 : size_t(parameters->extradata_size), VideoDecodeLimits::inputBytes);
            avformat_close_input(context); return false;
        }
    }
    result = avformat_find_stream_info(*context, nullptr);
    if (result < 0) { status.reject(0, VideoDecodeLimits::inputBytes); avformat_close_input(context); return false; }
    return true;
}

// Some FFmpeg library versions initialize contexts with one thread. Zero
// requests FFmpeg's automatic choice instead of fixing the worker count.
inline void configureVideoCodec(AVCodecContext* context) {
    context->thread_count = 0;
}

// Check FFmpeg image layout arithmetic, including row and plane alignment.
inline bool videoTransferLayout(int width, int height, AVPixelFormat format,
                                size_t& bytes, VideoDecodeStatus& status, int* linesizes = nullptr) {
    bytes = 0;
    const auto* description = av_pix_fmt_desc_get(format);
    int imageBytes = -1;
    if (width > 0 && height > 0 && height <= (std::numeric_limits<int>::max)() - 63 && description &&
        !(description->flags & AV_PIX_FMT_FLAG_HWACCEL))
        imageBytes = av_image_get_buffer_size(format, width, (height + 63) / 64 * 64, 64);
    if (imageBytes >= 0) bytes = size_t(imageBytes) + 4 * size_t(64);
    // Bound (security): CPU transfers must fit the allocated hardware surface, including alignment and plane padding. Overflow rejects the whole transfer and is counted.
    if (!bytes) {
        status.reject(bytes, 0);
        return false;
    }
    if (linesizes) {
        if (av_image_fill_linesizes(linesizes, format, width) < 0) return false;
        for (int plane = 0; plane < 4; ++plane)
            linesizes[plane] = (linesizes[plane] + 63) / 64 * 64;
    }
    return true;
}

// Own the CPU frame throughout a hardware transfer, including on failure.
class VideoCPUTransfer {
public:
    explicit VideoCPUTransfer(std::shared_ptr<VideoDecodeStatus> status) : status_(std::move(status)) {}
    ~VideoCPUTransfer() { av_frame_free(&frame_); }
    VideoCPUTransfer(const VideoCPUTransfer&) = delete;
    VideoCPUTransfer& operator=(const VideoCPUTransfer&) = delete;
    AVFrame* frame() const { return frame_; }
    int transfer(const AVFrame* source) {
        if (frame_ || !source->hw_frames_ctx) return AVERROR(EINVAL);
        const auto* hardware = reinterpret_cast<const AVHWFramesContext*>(source->hw_frames_ctx->data);
        size_t bytes = 0;
        if (!videoTransferLayout(hardware->width, hardware->height, hardware->sw_format, bytes, *status_))
            return AVERROR(EINVAL);
        if (source->width <= 0 || source->height <= 0 || source->width > hardware->width || source->height > hardware->height) {
            status_->reject(0, 0);
            return AVERROR(EINVAL);
        }
        AVPixelFormat* formats = nullptr;
        int result = av_hwframe_transfer_get_formats(source->hw_frames_ctx,
            AV_HWFRAME_TRANSFER_DIRECTION_FROM, &formats, 0);
        if (result < 0) return result;
        AVPixelFormat format = formats[0];
        for (auto* entry = formats; *entry != AV_PIX_FMT_NONE; ++entry)
            if (*entry == hardware->sw_format) { format = *entry; break; }
        av_free(formats);
        int linesizes[4]{};
        if (!videoTransferLayout(hardware->width, hardware->height, format, bytes, *status_, linesizes)) return AVERROR(EINVAL);
        frame_ = av_frame_alloc();
        if (!frame_) return AVERROR(ENOMEM);
        frame_->format = format;
        frame_->width = hardware->width;
        frame_->height = hardware->height;
        // Set the row layout explicitly to match the allocated hardware surface.
        std::copy_n(linesizes, 4, frame_->linesize);
        result = av_frame_get_buffer(frame_, 64);
        if (result < 0) return result;
        // Transfer matching display rectangles into the already allocated full
        // surface. With data set, transfer_data cannot allocate a destination.
        frame_->width = source->width;
        frame_->height = source->height;
        return av_hwframe_transfer_data(frame_, source, 0);
    }
private:
    std::shared_ptr<VideoDecodeStatus> status_;
    AVFrame* frame_ = nullptr;
};

inline bool validVideoFrame(const AVFrame* frame, VideoDecodeStatus& status) {
    size_t rgbaBytes = 0;
    int bytes = av_image_get_buffer_size(AVPixelFormat(frame->format), frame->width, frame->height, 1);
    if (!VideoDecodeLimits::rgba(frame->width, frame->height, rgbaBytes) ||
        bytes < 0 || !frame->data[0]) {
        status.reject(bytes < 0 ? 0 : size_t(bytes), 0);
        return false;
    }
    int expected[4]{};
    if (av_image_fill_linesizes(expected, AVPixelFormat(frame->format), frame->width) < 0) return false;
    for (int plane = 0; plane < 4; ++plane) {
        // Negative strides are legal (bottom-up frames); widen before negation.
        int64_t stride = frame->linesize[plane];
        if (stride < 0) stride = -stride;
        if (expected[plane] && (!frame->data[plane] || stride < expected[plane])) {
            status.reject(size_t(stride), 0);
            return false;
        }
    }
    return true;
}

} // namespace trussc::internal
