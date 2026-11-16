#include "../../platform/linux/tcVideoNV12Copy.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <vector>

#ifdef TC_TEST_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}
#endif

namespace {
void require(bool condition, const char* message) {
    if (!condition) { std::cerr << message << '\n'; std::exit(1); }
}

void checkCopy(int width, int height, bool reverseSource, bool reverseDestination) {
    std::array<std::vector<uint8_t>, 2> source, destination;
    const uint8_t* input[4] = {};
    uint8_t* output[4] = {};
    int inputStrides[4] = {}, outputStrides[4] = {};
    for (int plane = 0; plane < 2; ++plane) {
        int active = plane == 0 ? width : (width + 1) / 2 * 2;
        int rows = plane == 0 ? height : (height + 1) / 2;
        // Different odd strides and guard bytes on both sides of each plane.
        int sourceStride = active + (active % 2 == 0 ? 3 : 4);
        int destinationStride = sourceStride + 2;
        source[plane].assign(size_t(sourceStride) * rows + 2, 0xDB);
        destination[plane].assign(size_t(destinationStride) * rows + 2, 0xA5);
        inputStrides[plane] = reverseSource ? -sourceStride : sourceStride;
        outputStrides[plane] = reverseDestination ? -destinationStride : destinationStride;
        auto* start = source[plane].data() + 1 + (reverseSource ? (rows - 1) * sourceStride : 0);
        input[plane] = start;
        output[plane] = destination[plane].data() + 1 + (reverseDestination ? (rows - 1) * destinationStride : 0);
        for (int row = 0; row < rows; ++row)
            for (int column = 0; column < active; ++column)
                start[ptrdiff_t(row) * inputStrides[plane] + column] = uint8_t(1 + (row * 19 + column * 7 + plane * 41) % 254);
    }
    const auto original = source;
    trussc::internal::copyVideoNV12Planes(output, outputStrides, input, inputStrides, width, height);
    require(source == original, "copy changed source/padding");
    for (int plane = 0; plane < 2; ++plane) {
        int active = plane == 0 ? width : (width + 1) / 2 * 2;
        int rows = plane == 0 ? height : (height + 1) / 2;
        int stride = std::abs(outputStrides[plane]);
        require(destination[plane].front() == 0xA5 && destination[plane].back() == 0xA5, "plane guard overwritten");
        for (int row = 0; row < rows; ++row) {
            const auto* in = input[plane] + ptrdiff_t(row) * inputStrides[plane];
            const auto* out = output[plane] + ptrdiff_t(row) * outputStrides[plane];
            require(std::equal(in, in + active, out), "active Y/UV bytes differ");
            require(std::all_of(out, out + active, [](uint8_t value) { return value != 0; }), "zero Y/UV sample");
        }
        for (int row = 0; row < rows; ++row)
            for (int column = active; column < stride; ++column)
                require(destination[plane][1 + row * stride + column] == 0xA5, "destination padding overwritten");
    }
}

#ifdef TC_TEST_FFMPEG
void checkH264() {
    const AVCodec* encoder = avcodec_find_encoder_by_name("libx264");
    if (!encoder) encoder = avcodec_find_encoder_by_name("libopenh264");
    if (!encoder) { std::cout << "SKIP offline H.264: no software encoder installed\n"; return; }
    AVCodecContext* encoding = avcodec_alloc_context3(encoder);
    require(encoding != nullptr, "allocate encoder");
    encoding->width = 32; encoding->height = 18;
    encoding->pix_fmt = AV_PIX_FMT_YUV420P;
    encoding->time_base = AVRational{1, 25};
    encoding->thread_count = 1;
    encoding->max_b_frames = 0;
    if (encoder->id == AV_CODEC_ID_H264) {
        av_opt_set(encoding->priv_data, "preset", "ultrafast", 0);
        av_opt_set(encoding->priv_data, "tune", "zerolatency", 0);
    }
    require(avcodec_open2(encoding, encoder, nullptr) == 0, "open offline H.264 encoder");
    const AVCodec* decoder = avcodec_find_decoder(AV_CODEC_ID_H264);
    require(decoder != nullptr, "find software H.264 decoder");
    AVCodecContext* decoding = avcodec_alloc_context3(decoder);
    require(decoding != nullptr, "allocate decoder");
    decoding->thread_count = 1;
    require(avcodec_open2(decoding, decoder, nullptr) == 0, "open software H.264 decoder");
    AVFrame* frame = av_frame_alloc();
    AVFrame* decoded = av_frame_alloc();
    AVFrame* nv12 = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    require(frame && decoded && nv12 && packet, "allocate frames/packet");
    frame->format = AV_PIX_FMT_YUV420P; frame->width = 32; frame->height = 18;
    nv12->format = AV_PIX_FMT_NV12; nv12->width = 32; nv12->height = 18;
    require(av_frame_get_buffer(frame, 32) == 0 && av_frame_get_buffer(nv12, 32) == 0, "allocate planes");
    int frameCount = 0;
    SwsContext* scaler = nullptr;
    auto receiveFrames = [&] {
        int result;
        while ((result = avcodec_receive_frame(decoding, decoded)) == 0) {
            // Software H.264 emits YUV420P. This is a real format conversion;
            // the resulting NV12 exercises the same plane copy as HW transfer.
            scaler = sws_getCachedContext(scaler, 32, 18, AVPixelFormat(decoded->format),
                32, 18, AV_PIX_FMT_NV12, SWS_BILINEAR, nullptr, nullptr, nullptr);
            require(scaler != nullptr, "allocate real format conversion");
            require(sws_scale(scaler, decoded->data, decoded->linesize, 0, 18, nv12->data, nv12->linesize) == 18, "convert decoded frame to NV12");
            std::vector<uint8_t> y(32 * 18), uv(32 * 9);
            uint8_t* output[4] = {y.data(), uv.data(), nullptr, nullptr};
            const int strides[4] = {32, 32, 0, 0};
            trussc::internal::copyVideoNV12Planes(output, strides, nv12->data, nv12->linesize, 32, 18);
            for (int row = 0; row < 18; ++row)
                require(std::equal(y.data() + row * 32, y.data() + (row + 1) * 32, nv12->data[0] + row * nv12->linesize[0]), "decoded Y mismatch");
            for (int row = 0; row < 9; ++row)
                for (int column = 0; column < 32; ++column) {
                    int value = uv[row * 32 + column];
                    require(value == nv12->data[1][row * nv12->linesize[1] + column], "decoded UV mismatch");
                    require(std::abs(value - (column % 2 == 0 ? 90 : 200)) <= 3, "decoded chroma differs from expected color");
                }
            ++frameCount;
            av_frame_unref(decoded);
        }
        require(result == AVERROR(EAGAIN) || result == AVERROR_EOF, "receive decoded H.264");
    };
    auto receivePackets = [&] {
        int result;
        while ((result = avcodec_receive_packet(encoding, packet)) == 0) {
            require(avcodec_send_packet(decoding, packet) == 0, "send encoded H.264 packet");
            receiveFrames();
            av_packet_unref(packet);
        }
        require(result == AVERROR(EAGAIN) || result == AVERROR_EOF, "receive encoded H.264");
    };
    for (int index = 0; index < 4; ++index) {
        require(av_frame_make_writable(frame) == 0, "writable source frame");
        for (int plane = 0; plane < 3; ++plane)
            for (int row = 0; row < (plane == 0 ? 18 : 9); ++row)
                std::fill_n(frame->data[plane] + row * frame->linesize[plane], plane == 0 ? 32 : 16, plane == 0 ? 100 : (plane == 1 ? 90 : 200));
        frame->pts = index;
        require(avcodec_send_frame(encoding, frame) == 0, "encode offline H.264 frame");
        receivePackets();
    }
    require(avcodec_send_frame(encoding, nullptr) == 0, "flush encoder"); receivePackets();
    require(avcodec_send_packet(decoding, nullptr) == 0, "flush decoder"); receiveFrames();
    require(frameCount == 4, "expected four decoded NV12 frames");
    sws_freeContext(scaler);
    av_packet_free(&packet); av_frame_free(&frame); av_frame_free(&decoded); av_frame_free(&nv12);
    avcodec_free_context(&encoding); avcodec_free_context(&decoding);
    std::cout << "PASS offline H.264 -> software decode -> NV12: four colored frames\n";
}
#endif
} // namespace

int main() {
    for (auto dimensions : {std::array<int, 2>{1, 1}, {5, 3}, {6, 5}, {7, 4}, {32, 18}})
        for (bool reverseSource : {false, true})
            for (bool reverseDestination : {false, true})
                checkCopy(dimensions[0], dimensions[1], reverseSource, reverseDestination);
    std::cout << "PASS NV12 odd dimensions/strides, padding, signed strides\n";
#ifdef TC_TEST_FFMPEG
    checkH264();
#endif
}
