#pragma once
#include "tc/utils/tcOnceGate.h"
#include "tc/sound/internal/tcAudioDecodeBudget.h"

// Linux-only miniaudio backend for audio inside video containers. Included by
// tcAudio_impl.cpp after miniaudio; never decoded on the audio callback thread.
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
}

namespace trussc::internal {
namespace {
struct FFmpegAudio {
    ma_data_source_base base; // miniaudio requires this first
    AudioDecodeBudget localBudget{16384 * 2 * sizeof(float) + 65536, true};
    AudioDecodeBudget& budget;
    explicit FFmpegAudio(AudioDecodeBudget* owner = nullptr) : budget(owner ? *owner : localBudget) {}
    int sourceChannels = 0, sourceRate = 0;
    AVSampleFormat sourceFormat = AV_SAMPLE_FMT_NONE;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    SwrContext* resampler = nullptr;
    AVFrame* frame = nullptr;
    AVPacket* packet = nullptr;
    int stream = -1;
    int rate = 0;
    bool pending = false;
    bool demuxEnd = false;
    bool draining = false;
    bool ended = false;
    bool seeking = false;
    int64_t seekFrame = 0;
    ma_uint64 cursor = 0;
    ma_uint64 length = 0;
    bool exactLength = false;
    std::vector<float> samples;
    size_t offset = 0;
    OnceGate corruptPacketWarned;

    void warnCorruptPacket() {
        if (corruptPacketWarned.isFirstTime())
            logWarning("FFmpegAudio") << "Dropping corrupt audio packet; continuing playback";
    }

    ~FFmpegAudio() {
        av_packet_free(&packet);
        av_frame_free(&frame);
        swr_free(&resampler);
        avcodec_free_context(&codec);
        avformat_close_input(&format);
    }

    struct FrameAllocation { size_t bytes; };
    // Keep the payload aligned as av_malloc returned it; the prefix is also charged.
    static constexpr size_t framePrefixBytes = AV_INPUT_BUFFER_PADDING_SIZE;
    static void freeFrame(void* opaque, uint8_t* data) {
        auto& owner = *static_cast<AudioDecodeBudget*>(opaque);
        auto* base = data - framePrefixBytes;
        owner.live -= reinterpret_cast<FrameAllocation*>(base)->bytes;
        av_free(base);
    }
    static int getFrameBuffer(AVCodecContext* ctx, AVFrame* frame, int) {
        auto& self = *static_cast<FFmpegAudio*>(ctx->opaque);
        if (frame->nb_samples < 0 || !self.budget.format(frame->ch_layout.nb_channels, frame->sample_rate) ||
            (self.sourceChannels && frame->ch_layout.nb_channels != self.sourceChannels) ||
            (self.sourceRate && frame->sample_rate != self.sourceRate)) {
            self.budget.reject("stream format change", frame->ch_layout.nb_channels, self.sourceChannels);
            return AVERROR(EINVAL);
        }
        int lineBytes = 0;
        const int bytes = av_samples_get_buffer_size(&lineBytes, frame->ch_layout.nb_channels,
            frame->nb_samples, AVSampleFormat(frame->format), 32);
        if (bytes < 0 || !self.budget.admit(size_t(bytes) + framePrefixBytes, self.samples.capacity() * sizeof(float)))
            return AVERROR(ENOMEM);
        auto* base = static_cast<uint8_t*>(av_mallocz(size_t(bytes) + framePrefixBytes));
        if (!base) return AVERROR(ENOMEM);
        reinterpret_cast<FrameAllocation*>(base)->bytes = size_t(bytes) + framePrefixBytes;
        self.budget.live += size_t(bytes) + framePrefixBytes;
        auto* data = base + framePrefixBytes;
        frame->buf[0] = av_buffer_create(data, bytes, freeFrame, &self.budget, 0);
        if (!frame->buf[0]) { freeFrame(&self.budget, data); return AVERROR(ENOMEM); }
        const int planes = av_sample_fmt_is_planar(AVSampleFormat(frame->format)) ? frame->ch_layout.nb_channels : 1;
        if (planes > AV_NUM_DATA_POINTERS) {
            // At most 32 pointers; accounted by the fixed decoder overhead.
            frame->extended_data = static_cast<uint8_t**>(av_calloc(planes, sizeof(uint8_t*)));
            if (!frame->extended_data) return AVERROR(ENOMEM);
        } else frame->extended_data = frame->data;
        frame->linesize[0] = lineBytes;
        for (int i = 0; i < planes; ++i) {
            frame->extended_data[i] = data + size_t(i) * lineBytes;
            if (i < AV_NUM_DATA_POINTERS) frame->data[i] = frame->extended_data[i];
        }
        return 0;
    }

    ma_result open(const char* path) {
        format = avformat_alloc_context();
        if (!format) return MA_OUT_OF_MEMORY;
        // Bound (security): probing uses a resident window independent of track duration. Overflow returns a decoder error; working-budget rejection is counted.
        format->probesize = std::min<int64_t>(format->probesize, int64_t(budget.limit() / 4));
        // Bound (cache): seek indexes use a resident window independent of track duration. Overflow evicts or replaces.
        format->max_index_size = std::min<unsigned>(format->max_index_size, unsigned(budget.limit() / 4));
        if (avformat_open_input(&format, path, nullptr, nullptr) < 0) return MA_INVALID_FILE;
        const int candidate = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (candidate >= 0) {
            const auto* parameters = format->streams[candidate]->codecpar;
            if (!budget.format(parameters->ch_layout.nb_channels ? parameters->ch_layout.nb_channels : 1,
                               parameters->sample_rate ? parameters->sample_rate : 1)) return MA_INVALID_DATA;
        }
        if (avformat_find_stream_info(format, nullptr) < 0) return MA_INVALID_FILE;
        stream = av_find_best_stream(format, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (stream < 0) return MA_INVALID_FILE;
        AVStream* st = format->streams[stream];
        // Zero in container metadata means unknown; the bounded codec open
        // can resolve it. Validate every known field before asking the codec.
        if (!budget.format(st->codecpar->ch_layout.nb_channels ? st->codecpar->ch_layout.nb_channels : 1,
                           st->codecpar->sample_rate ? st->codecpar->sample_rate : 1)) return MA_INVALID_DATA;
        const AVCodec* decoder = avcodec_find_decoder(st->codecpar->codec_id);
        if (!decoder) return MA_NOT_IMPLEMENTED;
        codec = avcodec_alloc_context3(decoder);
        frame = av_frame_alloc();
        packet = av_packet_alloc();
        if (!codec || !frame || !packet) return MA_OUT_OF_MEMORY;
        if (avcodec_parameters_to_context(codec, st->codecpar) < 0) return MA_INVALID_FILE;
        codec->opaque = this;
        codec->get_buffer2 = getFrameBuffer;
        // Bound (security): codec frame size and worker count must stay within a per-stream working set. Overflow returns a decoder error; frame-budget rejection is counted.
        codec->thread_count = 1;
        codec->max_samples = int64_t(budget.limit() / sizeof(double) /
            std::max(1, codec->ch_layout.nb_channels));
        if (avcodec_open2(codec, decoder, nullptr) < 0) return MA_INVALID_FILE;
        sourceFormat = codec->sample_fmt;
        rate = codec->sample_rate;
        if (!budget.format(codec->ch_layout.nb_channels, rate)) return MA_INVALID_FILE;
        // The codec may refine container metadata (e.g. AAC output rate).
        // Changes after this negotiated format are rejected during reads.
        sourceChannels = codec->ch_layout.nb_channels;
        sourceRate = rate;
        AVChannelLayout stereo = AV_CHANNEL_LAYOUT_STEREO;
        if (swr_alloc_set_opts2(&resampler, &stereo, AV_SAMPLE_FMT_FLT, rate,
                &codec->ch_layout, codec->sample_fmt, rate, 0, nullptr) < 0 ||
            swr_init(resampler) < 0) return MA_OUT_OF_MEMORY;
        // Query metadata only: no scan of the audio track to determine length.
        int64_t duration = st->duration;
        if (duration != AV_NOPTS_VALUE && duration > 0) {
            length = av_rescale_q(duration, st->time_base, AVRational{1, rate});
            exactLength = true;
        } else if (format->duration != AV_NOPTS_VALUE && format->duration > 0) {
            length = av_rescale_q(format->duration, AV_TIME_BASE_Q, AVRational{1, rate});
        }
        return MA_SUCCESS;
    }

    // A decoder can reference its input packet instead of using get_buffer2.
    // Charge those observable SDK buffers too, once per backing buffer/span.
    bool frameWorkingBytes(size_t& bytes) {
        bytes = 0;
        if (frame->nb_extended_buf < 0 || frame->nb_extended_buf > int(audioChannelLimit) ||
            (frame->nb_extended_buf && !frame->extended_buf))
            return budget.reject("stream audio frame buffers", frame->nb_extended_buf, audioChannelLimit);
        struct Span { const AVBuffer* buffer; uintptr_t first, last; };
        Span spans[AV_NUM_DATA_POINTERS + audioChannelLimit]{};
        size_t spanCount = 0;
        auto add = [&](AVBufferRef* ref) {
            if (!ref || av_buffer_get_opaque(ref) == &budget) return true;
            const uintptr_t first = reinterpret_cast<uintptr_t>(ref->data);
            const size_t size = size_t(ref->size);
            if (size > budget.limit() || size > UINTPTR_MAX - first)
                return budget.reject("stream audio buffer bytes", size, budget.limit());
            size_t index = 0;
            while (index < spanCount && spans[index].buffer != ref->buffer) ++index;
            if (index == spanCount) spans[spanCount++] = {ref->buffer, first, first};
            auto& span = spans[index];
            span.first = std::min(span.first, first);
            span.last = std::max(span.last, first + size);
            return true;
        };
        for (auto* ref : frame->buf) if (!add(ref)) return false;
        for (int i = 0; i < frame->nb_extended_buf; ++i) if (!add(frame->extended_buf[i])) return false;
        for (size_t i = 0; i < spanCount; ++i) {
            const uintptr_t span = spans[i].last - spans[i].first;
            if (span > budget.limit() - bytes) return budget.reject("stream audio buffer bytes", span, budget.limit());
            bytes += size_t(span);
        }
        return true;
    }

    bool prepareSamples(int capacity, bool withFrame = true) {
        if (capacity < 0 || uint64_t(capacity) > budget.limit() / (2 * sizeof(float)))
            return budget.reject("stream resampler frames", capacity, budget.limit() / (2 * sizeof(float)));
        size_t sourceBytes = 0;
        if (withFrame && !frameWorkingBytes(sourceBytes)) return false;
        const size_t resampleBytes = size_t(capacity) * 2 * sizeof(float);
        if (!growAudioVector(samples, size_t(capacity) * 2, budget, resampleBytes + sourceBytes)) return false;
        samples.resize(size_t(capacity) * 2);
        return true;
    }

    // Return one bounded decoded frame, draining both codec and resampler at EOF.
    ma_result fill() {
        samples.clear();
        offset = 0;
        if (ended) return MA_AT_END;
        for (;;) {
            int r = avcodec_receive_frame(codec, frame);
            if (r == 0 || r == AVERROR_EOF) {
                const bool flush = r == AVERROR_EOF;
                if (!flush && sourceFormat == AV_SAMPLE_FMT_NONE) sourceFormat = AVSampleFormat(frame->format);
                if (!flush && (!budget.format(frame->ch_layout.nb_channels, frame->sample_rate) ||
                        frame->ch_layout.nb_channels != sourceChannels || frame->sample_rate != sourceRate ||
                        frame->format != sourceFormat || frame->nb_samples < 0)) {
                    budget.reject("stream format change", frame->ch_layout.nb_channels, sourceChannels);
                    return MA_INVALID_DATA;
                }
                int count = flush ? 0 : frame->nb_samples;
                int capacity = swr_get_out_samples(resampler, count);
                if (!prepareSamples(capacity, !flush)) return MA_OUT_OF_MEMORY;
                uint8_t* out[] = {reinterpret_cast<uint8_t*>(samples.data())};
                int n = swr_convert(resampler, out, capacity,
                    flush ? nullptr : const_cast<const uint8_t**>(frame->extended_data), count);
                if (n < 0) return MA_ERROR;
                samples.resize(static_cast<size_t>(n) * 2);
                if (seeking && !flush) {
                    AVStream* st = format->streams[stream];
                    int64_t pts = frame->best_effort_timestamp;
                    if (pts == AV_NOPTS_VALUE) return MA_NOT_IMPLEMENTED;
                    const int64_t start = st->start_time == AV_NOPTS_VALUE ? 0 : st->start_time;
                    int64_t first = av_rescale_q(pts - start, st->time_base, AVRational{1, rate});
                    if (seekFrame >= first + n) {
                        samples.clear();
                    } else {
                        offset = static_cast<size_t>(std::max<int64_t>(0, seekFrame - first)) * 2;
                        seeking = false;
                    }
                }
                av_frame_unref(frame);
                if (flush && n == 0) { ended = true; return MA_AT_END; }
                if (offset < samples.size()) return MA_SUCCESS;
                continue;
            }
            if (r == AVERROR_INVALIDDATA) {
                // The decoder consumed corrupt input. Drain any remaining
                // output, then feed the next packet without flushing history.
                av_frame_unref(frame);
                warnCorruptPacket();
                continue;
            }
            if (r != AVERROR(EAGAIN)) return MA_ERROR;
            if (!pending && !demuxEnd) {
                for (;;) {
                    r = av_read_frame(format, packet);
                    if (r < 0) {
                        if (r != AVERROR_EOF) return MA_IO_ERROR;
                        demuxEnd = true;
                        break;
                    }
                    if (packet->size < 0 || !budget.admit(size_t(packet->size), samples.capacity() * sizeof(float))) {
                        av_packet_unref(packet);
                        return MA_OUT_OF_MEMORY;
                    }
                    if (packet->stream_index == stream) { pending = true; break; }
                    av_packet_unref(packet);
                }
            }
            if (demuxEnd && draining) return MA_ERROR;
            r = avcodec_send_packet(codec, pending ? packet : nullptr);
            if (r == AVERROR(EAGAIN)) continue; // retain packet while draining
            if (r == AVERROR_INVALIDDATA && pending) {
                av_packet_unref(packet);
                pending = false;
                warnCorruptPacket();
                continue;
            }
            if (r < 0) return MA_ERROR;
            if (pending) { av_packet_unref(packet); pending = false; }
            else draining = true;
        }
    }

    ma_result seek(ma_uint64 target) {
        // Bound (abi): file seek targets must fit FFmpeg signed timestamp width. Overflow returns MA_OUT_OF_RANGE.
        if (target > static_cast<ma_uint64>(INT64_MAX)) return MA_OUT_OF_RANGE;
        AVStream* st = format->streams[stream];
        const int64_t start = st->start_time == AV_NOPTS_VALUE ? 0 : st->start_time;
        int64_t timestamp = av_rescale_q(static_cast<int64_t>(target), AVRational{1, rate}, st->time_base);
        // Bound (abi): file start offsets plus seek timestamps must fit FFmpeg signed timestamp width. Overflow returns MA_OUT_OF_RANGE.
        if (timestamp > INT64_MAX - std::max<int64_t>(0, start)) return MA_OUT_OF_RANGE;
        if (timestamp < 0) return MA_OUT_OF_RANGE;
        // Decode the preceding codec frame as preroll. AAC needs its overlap
        // history even when looping to frame 0 (the first packet can have a
        // negative PTS); seeking directly to 0 loses that history.
        int64_t preroll = std::max(codec->frame_size, st->codecpar->seek_preroll);
        int64_t seekTimestamp = timestamp + start - av_rescale_q(preroll,
            AVRational{1, rate}, st->time_base);
        if (avformat_seek_file(format, stream, INT64_MIN, seekTimestamp,
                timestamp + start, AVSEEK_FLAG_BACKWARD) < 0) return MA_BAD_SEEK;
        avcodec_flush_buffers(codec);
        av_packet_unref(packet);
        av_frame_unref(frame);
        swr_close(resampler);
        if (swr_init(resampler) < 0) return MA_ERROR;
        samples.clear();
        offset = 0;
        pending = demuxEnd = draining = ended = false;
        seeking = true;
        seekFrame = static_cast<int64_t>(target);
        cursor = target;
        return MA_SUCCESS;
    }
};

ma_result ffRead(ma_data_source* source, void* out, ma_uint64 count, ma_uint64* read) {
    auto& s = *static_cast<FFmpegAudio*>(source);
    *read = 0;
    // Container audio duration excludes encoder tail padding (e.g. AAC in
    // MP4). Do not append padding to each loop. Container-wide estimates
    // are only a duration hint, and must not truncate a decoded track.
    if (s.exactLength) {
        if (s.cursor >= s.length) return MA_AT_END;
        count = std::min(count, s.length - s.cursor);
    }
    try {
        while (*read < count) {
            if (s.offset == s.samples.size()) {
                ma_result r = s.fill();
                if (r != MA_SUCCESS) return r;
            }
            size_t n = static_cast<size_t>(std::min<ma_uint64>(count - *read,
                (s.samples.size() - s.offset) / 2));
            if (out) std::memcpy(static_cast<float*>(out) + *read * 2,
                s.samples.data() + s.offset, n * 2 * sizeof(float));
            s.offset += n * 2;
            *read += n;
            s.cursor += n;
        }
        return MA_SUCCESS;
    } catch (const std::bad_alloc&) { return MA_OUT_OF_MEMORY; }
      catch (const std::length_error&) { return MA_OUT_OF_MEMORY; }
}
ma_result ffSeek(ma_data_source* s, ma_uint64 frame) { return static_cast<FFmpegAudio*>(s)->seek(frame); }
ma_result ffFormat(ma_data_source* s, ma_format* fmt, ma_uint32* channels, ma_uint32* rate,
                   ma_channel* map, size_t cap) {
    if (fmt) *fmt = ma_format_f32;
    if (channels) *channels = 2;
    if (rate) *rate = static_cast<FFmpegAudio*>(s)->rate;
    if (map) ma_channel_map_init_standard(ma_standard_channel_map_default, map, cap, 2);
    return MA_SUCCESS;
}
ma_result ffCursor(ma_data_source* s, ma_uint64* value) { *value = static_cast<FFmpegAudio*>(s)->cursor; return MA_SUCCESS; }
ma_result ffLength(ma_data_source* s, ma_uint64* value) { *value = static_cast<FFmpegAudio*>(s)->length; return MA_SUCCESS; }
const ma_data_source_vtable ffSourceVTable = {ffRead, ffSeek, ffFormat, ffCursor, ffLength, nullptr, 0};
ma_result ffInitFile(void*, const char* path, const ma_decoding_backend_config*,
                     const ma_allocation_callbacks* callbacks, ma_data_source** out) {
    *out = nullptr;
    try {
        auto* budget = callbacks && callbacks->onMalloc == AudioDecodeBudget::allocate
            ? static_cast<AudioDecodeBudget*>(callbacks->pUserData) : nullptr;
        auto s = std::make_unique<FFmpegAudio>(budget);
        ma_result r = s->open(path);
        if (r != MA_SUCCESS) return r;
        ma_data_source_config config = ma_data_source_config_init();
        config.vtable = &ffSourceVTable;
        r = ma_data_source_init(&config, &s->base);
        if (r != MA_SUCCESS) return r;
        *out = s.release();
        return MA_SUCCESS;
    } catch (const std::bad_alloc&) { return MA_OUT_OF_MEMORY; }
}
void ffUninit(void*, ma_data_source* s, const ma_allocation_callbacks*) {
    ma_data_source_uninit(s);
    delete static_cast<FFmpegAudio*>(s);
}
ma_decoding_backend_vtable ffBackendVTable = {nullptr, ffInitFile, nullptr, nullptr, ffUninit};
} // namespace
} // namespace trussc::internal
