#pragma once

#include "tc/video/internal/tcVideoDecodeLimits.h"
extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixdesc.h>
}

namespace trussc::internal {

// Configure these before open/probe; duration is deliberately unrestricted.
inline bool openVideoInput(AVFormatContext** context, const char* path, VideoDecodeBudget& budget) {
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
    if (result < 0) { budget.reject(0, VideoDecodeLimits::inputBytes); avformat_close_input(context); return false; }
    for (unsigned i = 0; i < (*context)->nb_streams; ++i) {
        const auto* parameters = (*context)->streams[i]->codecpar;
        size_t bytes = 0;
        if (parameters->codec_type == AVMEDIA_TYPE_VIDEO && parameters->width && parameters->height &&
            !VideoDecodeLimits::rgba(parameters->width, parameters->height, bytes)) {
            budget.reject(bytes, VideoDecodeLimits::frameBytes);
            avformat_close_input(context); return false;
        }
        if (parameters->extradata_size < 0 || size_t(parameters->extradata_size) > VideoDecodeLimits::inputBytes) {
            budget.reject(parameters->extradata_size < 0 ? 0 : size_t(parameters->extradata_size), VideoDecodeLimits::inputBytes);
            avformat_close_input(context); return false;
        }
    }
    // Do not let stream probing create unconstrained automatic decoder threads.
    std::vector<AVDictionary*> codecOptions((*context)->nb_streams, nullptr);
    for (auto& entry : codecOptions) {
        av_dict_set(&entry, "threads", "1", 0);
        av_dict_set(&entry, "max_pixels", "33554432", 0);
    }
    result = avformat_find_stream_info(*context, codecOptions.data());
    for (auto& entry : codecOptions) av_dict_free(&entry);
    if (result < 0) { budget.reject(0, VideoDecodeLimits::inputBytes); avformat_close_input(context); return false; }
    return true;
}

struct VideoFrameAllocation {
    std::shared_ptr<VideoDecodeBudget> budget;
    size_t bytes;
    static void freeBuffer(void* opaque, uint8_t* pixels) {
        auto* allocation = static_cast<VideoFrameAllocation*>(opaque);
        av_free(pixels);
        allocation->budget->release(allocation->bytes);
        delete allocation;
    }
};

inline int boundedVideoBuffer(AVCodecContext* context, AVFrame* frame, int flags) {
    auto& budget = *static_cast<std::shared_ptr<VideoDecodeBudget>*>(context->opaque);
    size_t rgbaBytes = 0;
    if (!VideoDecodeLimits::rgba(frame->width, frame->height, rgbaBytes)) {
        budget->reject(rgbaBytes, VideoDecodeLimits::frameBytes);
        return AVERROR(EINVAL);
    }
    // Hardware surfaces are SDK-owned. Keep that route, validate dimensions,
    // and bound every CPU transfer separately at the accepting boundary.
    const auto* format = av_pix_fmt_desc_get(AVPixelFormat(frame->format));
    if (frame->hw_frames_ctx || (format && (format->flags & AV_PIX_FMT_FLAG_HWACCEL)))
        return avcodec_default_get_buffer2(context, frame, flags);
    int width = frame->width, height = frame->height;
    int alignments[AV_NUM_DATA_POINTERS]{};
    avcodec_align_dimensions2(context, &width, &height, alignments);
    int alignment = 64;
    for (int value : alignments) alignment = (std::max)(alignment, value);
    if (width <= 0 || height <= 0 || alignment > 4096 || width > 16384 || height > 16384)
        return AVERROR(EINVAL);
    width = (width + alignment - 1) / alignment * alignment;
    int imageBytes = av_image_get_buffer_size(AVPixelFormat(frame->format), width, height, alignment);
    if (imageBytes < 0 || size_t(imageBytes) > VideoDecodeLimits::frameBytes ||
        !budget->acquire(size_t(imageBytes) + AV_INPUT_BUFFER_PADDING_SIZE)) {
        budget->reject(imageBytes < 0 ? 0 : size_t(imageBytes), VideoDecodeLimits::workingBytes);
        return AVERROR(ENOMEM);
    }
    size_t bytes = size_t(imageBytes) + AV_INPUT_BUFFER_PADDING_SIZE;
    auto* allocation = new (std::nothrow) VideoFrameAllocation{budget, bytes};
    auto* pixels = static_cast<uint8_t*>(av_mallocz(bytes));
    if (!allocation || !pixels) {
        delete allocation;
        av_free(pixels);
        budget->release(bytes);
        return AVERROR(ENOMEM);
    }
    frame->buf[0] = av_buffer_create(pixels, bytes, VideoFrameAllocation::freeBuffer, allocation, 0);
    if (!frame->buf[0]) { VideoFrameAllocation::freeBuffer(allocation, pixels); return AVERROR(ENOMEM); }
    int result = av_image_fill_arrays(frame->data, frame->linesize, pixels,
                                     AVPixelFormat(frame->format), width, height, alignment);
    if (result < 0) { av_buffer_unref(&frame->buf[0]); return result; }
    frame->extended_data = frame->data;
    return 0;
}

inline void configureVideoCodec(AVCodecContext* context, std::shared_ptr<VideoDecodeBudget>& budget) {
    context->max_pixels = VideoDecodeLimits::frameBytes / 4;
    context->thread_count = 1;
    context->opaque = &budget;
    context->get_buffer2 = boundedVideoBuffer;
}

// av_frame_get_buffer(..., 64) aligns rows and pads plane heights. Reserve
// the image with height rounded up to 64 (covers FFmpeg's 32-row padding),
// plus its four 64-byte plane gaps, before asking FFmpeg to allocate it.
inline bool videoTransferLayout(int width, int height, AVPixelFormat format,
                                size_t& bytes, VideoDecodeBudget& budget, int* linesizes = nullptr) {
    bytes = 0;
    const auto* description = av_pix_fmt_desc_get(format);
    int imageBytes = -1;
    if (width > 0 && height > 0 && width <= int(VideoDecodeLimits::axis) &&
        height <= int(VideoDecodeLimits::axis) && description &&
        !(description->flags & AV_PIX_FMT_FLAG_HWACCEL))
        imageBytes = av_image_get_buffer_size(format, width, (height + 63) / 64 * 64, 64);
    if (imageBytes >= 0) bytes = size_t(imageBytes) + 4 * size_t(64);
    // Bound (security): CPU transfers must fit the allocated hardware surface, including alignment and plane padding. Overflow rejects the whole transfer and is counted.
    if (!bytes || bytes > VideoDecodeLimits::frameBytes) {
        budget.reject(bytes, VideoDecodeLimits::frameBytes);
        return false;
    }
    if (linesizes) {
        if (av_image_fill_linesizes(linesizes, format, width) < 0) return false;
        for (int plane = 0; plane < 4; ++plane)
            linesizes[plane] = (linesizes[plane] + 63) / 64 * 64;
    }
    return true;
}

// Keep the credit until after the CPU frame is freed, including on failure.
class VideoCPUTransfer {
public:
    explicit VideoCPUTransfer(std::shared_ptr<VideoDecodeBudget> budget) : budget_(std::move(budget)) {}
    ~VideoCPUTransfer() { av_frame_free(&frame_); budget_->release(bytes_); }
    VideoCPUTransfer(const VideoCPUTransfer&) = delete;
    VideoCPUTransfer& operator=(const VideoCPUTransfer&) = delete;
    AVFrame* frame() const { return frame_; }
    int transfer(const AVFrame* source) {
        if (frame_ || !source->hw_frames_ctx) return AVERROR(EINVAL);
        const auto* hardware = reinterpret_cast<const AVHWFramesContext*>(source->hw_frames_ctx->data);
        size_t bytes = 0;
        if (!videoTransferLayout(hardware->width, hardware->height, hardware->sw_format, bytes, *budget_))
            return AVERROR(EINVAL);
        if (source->width <= 0 || source->height <= 0 || source->width > hardware->width || source->height > hardware->height) {
            budget_->reject(0, VideoDecodeLimits::frameBytes);
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
        if (!videoTransferLayout(hardware->width, hardware->height, format, bytes, *budget_, linesizes)) return AVERROR(EINVAL);
        if (!budget_->acquire(bytes)) {
            budget_->reject(bytes, VideoDecodeLimits::workingBytes);
            return AVERROR(ENOMEM);
        }
        bytes_ = bytes;
        frame_ = av_frame_alloc();
        if (!frame_) return AVERROR(ENOMEM);
        frame_->format = format;
        frame_->width = hardware->width;
        frame_->height = hardware->height;
        // Set the admitted row layout explicitly. Otherwise FFmpeg may align
        // pixel width first (e.g. RGB24) and choose larger rows than our layout.
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
    std::shared_ptr<VideoDecodeBudget> budget_;
    AVFrame* frame_ = nullptr;
    size_t bytes_ = 0;
};

inline bool validVideoFrame(const AVFrame* frame, VideoDecodeBudget& budget) {
    size_t rgbaBytes = 0;
    int bytes = av_image_get_buffer_size(AVPixelFormat(frame->format), frame->width, frame->height, 1);
    if (!VideoDecodeLimits::rgba(frame->width, frame->height, rgbaBytes) ||
        bytes < 0 || size_t(bytes) > VideoDecodeLimits::frameBytes || !frame->data[0]) {
        budget.reject(bytes < 0 ? 0 : size_t(bytes), VideoDecodeLimits::frameBytes);
        return false;
    }
    int expected[4]{};
    if (av_image_fill_linesizes(expected, AVPixelFormat(frame->format), frame->width) < 0) return false;
    for (int plane = 0; plane < 4; ++plane) {
        // Negative strides are legal (bottom-up frames); widen before negation.
        int64_t stride = frame->linesize[plane];
        if (stride < 0) stride = -stride;
        if (expected[plane] && (!frame->data[plane] || stride < expected[plane] ||
            uint64_t(stride) * size_t(frame->height) > VideoDecodeLimits::frameBytes)) {
            budget.reject(size_t(stride), VideoDecodeLimits::frameBytes);
            return false;
        }
    }
    return true;
}

} // namespace trussc::internal
