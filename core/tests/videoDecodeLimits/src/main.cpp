#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "tc/video/internal/tcVideoDecodeLimits.h"
#include <cstdio>
#if defined(__linux__) && !defined(__ANDROID__)
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/error.h>
#include <libavutil/opt.h>
}
#include "../../../platform/linux/tcVideoDecodeFFmpeg.h"
#endif

namespace {
int failures = 0;
void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}
using Limits = tc::internal::VideoDecodeLimits;
using Budget = tc::internal::VideoDecodeBudget;

void layoutChecks() {
    size_t bytes = 0;
    check("8K frame admitted", Limits::rgba(7680, 4320, bytes) && bytes == 132710400);
    check("exact frame limit admitted", Limits::rgba(8192, 4096, bytes) && bytes == Limits::frameBytes);
    check("frame limit overflow rejected", !Limits::rgba(8192, 4097, bytes));
    check("axis overflow rejected", !Limits::rgba(8193, 1, bytes));
    check("negative dimension rejected", !Limits::rgba(-1, 4, bytes));
    check("empty frame rejected", !Limits::rgba(0, 4, bytes));
    check("product overflow rejected", !Limits::product((std::numeric_limits<size_t>::max)(), 4, bytes));
    check("padded frame rows admitted", Limits::rows(3, 2, 16, 32));
    check("short stride rejected", !Limits::rows(3, 2, 11, 32));
    check("short buffer rejected", !Limits::rows(3, 2, 16, 31));
    check("stride product overflow rejected", !Limits::rows(3, 2, (std::numeric_limits<size_t>::max)(), 32));

    Budget budget;
    check("exact live budget admitted", budget.acquire(Limits::workingBytes));
    check("simultaneous allocation rejected", !budget.acquire(1));
    budget.release(Limits::workingBytes);
    check("released bytes are reusable", budget.acquire(4096));
    budget.release(4096);
    check("peak counts simultaneous storage", budget.peak() == Limits::workingBytes && budget.used() == 0);
    auto posterBudget = std::make_shared<Budget>();
    check("unused poster has no reservation", posterBudget->used() == 0);
    {
        tc::internal::VideoDecodeLease poster(posterBudget, Limits::frameBytes);
        check("poster reserves before decoding", poster.acquired() && posterBudget->used() == Limits::frameBytes);
        poster.shrink(4096);
        check("retained poster charges actual output", posterBudget->used() == 4096);
    }
    check("poster release returns credit", posterBudget->used() == 0);
    int warnings = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& log) {
        if (log.level == tc::LogLevel::Warning && log.message.find("Video decode storage") != std::string::npos) ++warnings;
    });
    budget.reject(1, 0); budget.warn(); budget.reject(2, 0); budget.warn();
    check("first loss warns once and totals persist", warnings == 1 && budget.rejected() == 2 && budget.rejected() == 2);

    Budget stagingBudget;
    int stagingWarnings = 0;
    auto stagingListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& log) {
        if (log.level == tc::LogLevel::Warning && log.message.find("Video file staging") != std::string::npos) {
            ++stagingWarnings;
            check("staging warning reports actual store/capacity/needed bytes",
                log.message.find("capacity 67108864 bytes, needed 68157440 bytes; rejected 1") != std::string::npos);
        }
    });
    stagingBudget.reject(68157440, 67108864, "Video file staging"); stagingBudget.warn();
    stagingBudget.reject(68157440, 67108864, "Video file staging"); stagingBudget.warn();
    check("staging warns once and counts each rejection", stagingWarnings == 1 && stagingBudget.rejected() == 2);

    std::vector<uint8_t> track;
    tc::internal::VideoAudioStorage storage(track, budget);
    std::vector<uint8_t> chunk(1024 * 1024, 0x5a);
    for (int i = 0; i < 64; ++i)
        if (!storage.append(chunk.data(), chunk.size())) { check("track append", false); break; }
    check("complete exact-limit audio track", track.size() == Limits::inputBytes && track.back() == 0x5a && track.capacity() <= Limits::inputBytes);
    check("audio limit rejects next whole unit", !storage.append(chunk.data(), 1) && track.size() == Limits::inputBytes);
}

// Opt-in real-device check. One fixture per process also supports the macOS
// app loop, which terminates the process when the window closes.
tc::fs::path deviceFixture;
std::string deviceMode;
bool deviceCompleted = false;
class DeviceVideoCheck : public tc::App {
public:
    void setup() override {
        started = std::chrono::steady_clock::now();
        if (deviceMode == "reject" || deviceMode == "extract-reject") {
            for (bool keyframe : {false, true}) {
                tc::Pixels output;
                output.allocate(3, 2, 4);
                std::memset(output.getData(), 0x5a, 24);
                const bool extracted = keyframe ? tc::VideoPlayer::extractKeyFrame(deviceFixture, output, 0) :
                                                 tc::VideoPlayer::extractFrame(deviceFixture, output, 0);
                check("device extraction rejects whole fixture", !extracted);
                check("device rejected extraction preserves pixels", output.getWidth() == 3 && output.getHeight() == 2 &&
                    std::all_of(output.getData(), output.getData() + 24, [](uint8_t b) { return b == 0x5a; }));
            }
            if (deviceMode == "extract-reject") { finish(); return; }
        }
        player.setResyncThreshold(0);
        player.setVolume(0);
        const bool loaded = bool(player.load(deviceFixture));
        if (deviceMode == "reject") {
            check("device metadata rejected before output", !loaded &&
                tc::internal::VideoPlayerPlatformAccess::decodeRejectionsForTests(player) > 0);
            finish(); return;
        }
        check("device fixture loads", loaded);
        if (!loaded) { finish(); return; }
        if (deviceMode == "audio-accept" || deviceMode == "audio-reject") {
            check("device fixture has an audio track", player.hasAudio());
            auto data = player.getAudioData();
            std::printf("extracted audio bytes: %zu\n", data.size());
            const auto rejected = tc::internal::VideoPlayerPlatformAccess::decodeRejectionsForTests(player);
            auto reference = deviceFixture;
            reference.replace_extension(".aac");
            std::error_code error;
            const auto expectedBytes = tc::fs::file_size(reference, error);
            check("device audio reference is available", !error);
            check("device audio extraction is whole or rejected", !error && (deviceMode == "audio-reject" ?
                (expectedBytes > Limits::inputBytes && data.empty() && rejected > 0) :
                (data.size() == expectedBytes && data.size() <= Limits::inputBytes && rejected == 0)));
            finish(); return;
        }
        for (bool keyframe : {false, true}) {
            tc::Pixels output;
            check("device thumbnail/keyframe extraction succeeds", keyframe ?
                tc::VideoPlayer::extractKeyFrame(deviceFixture, output, 0) :
                tc::VideoPlayer::extractFrame(deviceFixture, output, 0));
        }
        player.play();
    }
    void update() override {
        if (finished) return;
        player.update();
        sawFrame |= player.isFrameNew();
        if (player.isDone() || player.hasError()) {
            check("device playback reaches EOF with output", player.isDone() && !player.hasError() && sawFrame);
            check("device playback stays admitted", tc::internal::VideoPlayerPlatformAccess::decodeRejectionsForTests(player) == 0);
            if (deviceMode == "resize") {
                check("device size-change presentation remains valid",
                    (player.getWidth() == 32 && player.getHeight() == 16) ||
                    (player.getWidth() == 64 && player.getHeight() == 32));
                std::printf("size-change final presentation: %dx%d\n", int(player.getWidth()), int(player.getHeight()));
            }
            finish();
        } else if (std::chrono::steady_clock::now() - started > std::chrono::seconds(180)) {
            check("device playback completes", false); finish();
        }
    }
    void draw() override { if (!finished) player.draw(0, 0, 64, 64); }
    void cleanup() override { player.close(); }
private:
    tc::VideoPlayer player;
    std::chrono::steady_clock::time_point started;
    bool sawFrame = false, finished = false;
    void finish() {
        finished = true;
        deviceCompleted = true;
        std::printf("device peak admitted bytes: %zu\n", tc::internal::VideoPlayerPlatformAccess::decodePeakBytesForTests(player));
        check("device working credits bounded", tc::internal::VideoPlayerPlatformAccess::decodePeakBytesForTests(player) <= Limits::workingBytes);
        std::printf("Device video fixture: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
        std::fflush(stdout);
        tc::exitApp();
    }
};

#if defined(__linux__) && !defined(__ANDROID__)
void hardwareTransferChecks() {
    auto budget = std::make_shared<Budget>();
    size_t bytes = 0;
    check("allocated surface layout includes padding", tc::internal::videoTransferLayout(1920, 1088, AV_PIX_FMT_YUV420P, bytes, *budget)
          && bytes > 1920 * 1088 * 3 / 2);
    check("surface axis overflow rejected", !tc::internal::videoTransferLayout(8193, 16, AV_PIX_FMT_YUV420P, bytes, *budget));
    check("negative surface rejected", !tc::internal::videoTransferLayout(32, -1, AV_PIX_FMT_YUV420P, bytes, *budget));
    check("surface byte overflow rejected", !tc::internal::videoTransferLayout(8192, 8192, AV_PIX_FMT_RGBA, bytes, *budget));
    check("invalid transfer format rejected", !tc::internal::videoTransferLayout(32, 16, AV_PIX_FMT_NONE, bytes, *budget));
    AVFrame* packed = av_frame_alloc();
    packed->format = AV_PIX_FMT_RGB24; packed->width = 33; packed->height = 65;
    check("packed transfer rows sized explicitly", tc::internal::videoTransferLayout(33, 65, AV_PIX_FMT_RGB24,
        bytes, *budget, packed->linesize) && packed->linesize[0] == 128);
    check("packed transfer preallocation fits admitted layout", av_frame_get_buffer(packed, 64) >= 0 &&
        packed->buf[0]->size <= bytes && packed->linesize[0] == 128);
    av_frame_free(&packed);

    AVBufferRef* device = nullptr;
    if (av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0) < 0) {
        std::puts("Vulkan transfer: SKIP (device unavailable); synthetic surface admission exercised");
        return;
    }
    AVBufferRef* frames = av_hwframe_ctx_alloc(device);
    check("Vulkan frames context allocated", frames != nullptr);
    if (!frames) { av_buffer_unref(&device); return; }
    auto* context = reinterpret_cast<AVHWFramesContext*>(frames->data);
    context->format = AV_PIX_FMT_VULKAN;
    context->sw_format = AV_PIX_FMT_YUV420P;
    context->width = 1920; context->height = 1088;
    int result = av_hwframe_ctx_init(frames);
    if (result < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(result, error, sizeof(error));
        std::printf("Vulkan transfer: SKIP (frames context unavailable: %s); synthetic surface admission exercised\n", error);
        std::puts("Vulkan surface allocated: SKIP (frames context unavailable)");
        av_buffer_unref(&frames);
        av_buffer_unref(&device);
        return;
    }
    check("Vulkan frames context initialized", true);
    AVFrame* hardware = av_frame_alloc();
    result = av_hwframe_get_buffer(frames, hardware, 0);
    check("Vulkan surface allocated", result >= 0);
    if (result >= 0) {
        AVFrame* upload = av_frame_alloc();
        upload->format = AV_PIX_FMT_YUV420P; upload->width = 1920; upload->height = 1088;
        result = av_frame_get_buffer(upload, 64);
        if (result >= 0) {
            for (int plane = 0; plane < 3; ++plane)
                for (int row = 0; row < (plane ? 544 : 1088); ++row)
                    std::memset(upload->data[plane] + row * upload->linesize[plane], 23, plane ? 960 : 1920);
            result = av_hwframe_transfer_data(hardware, upload, 0);
        }
        check("Vulkan fixture uploaded", result >= 0);
        av_frame_free(&upload);
        hardware->width = 32; hardware->height = 16;
        // Leave enough credits for the previous display-sized reservation,
        // but too few for the actual allocated surface.
        const size_t displayCharge = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 64, 64, 64) + AV_INPUT_BUFFER_PADDING_SIZE;
        auto limited = std::make_shared<Budget>();
        limited->acquire(Limits::workingBytes - displayCharge);
        {
            tc::internal::VideoCPUTransfer transfer(limited);
            check("small display cannot bypass full surface admission", transfer.transfer(hardware) < 0 &&
                !transfer.frame() && limited->used() == Limits::workingBytes - displayCharge && limited->rejected() == 1);
        }
        limited->release(Limits::workingBytes - displayCharge);
        {
            AVFrame* reference = av_frame_alloc();
            const bool referenceTransferred = reference && av_hwframe_transfer_data(reference, hardware, 0) >= 0;
            check("FFmpeg reference surface transfer succeeds", referenceTransferred);
            tc::internal::VideoCPUTransfer transfer(budget);
            check("full surface transfer admitted", transfer.transfer(hardware) >= 0);
            auto* cpu = transfer.frame();
            size_t actual = 0;
            if (cpu) for (auto* buffer : cpu->buf) if (buffer) actual += buffer->size;
            bool displayMatches = referenceTransferred && cpu && cpu->format == AV_PIX_FMT_YUV420P &&
                reference->format == AV_PIX_FMT_YUV420P && cpu->width == 32 && cpu->height == 16 &&
                reference->width >= 32 && reference->height >= 16;
            bool fixtureDiffers = false;
            if (displayMatches) {
                for (int plane = 0; plane < 3; ++plane) {
                    const int width = plane ? 16 : 32;
                    const int height = plane ? 8 : 16;
                    if (!cpu->data[plane] || !reference->data[plane] ||
                        cpu->linesize[plane] < width || reference->linesize[plane] < width) {
                        displayMatches = false;
                        continue;
                    }
                    for (int row = 0; row < height; ++row) {
                        const auto* expected = reference->data[plane] + row * reference->linesize[plane];
                        displayMatches &= std::memcmp(cpu->data[plane] + row * cpu->linesize[plane], expected, width) == 0;
                        fixtureDiffers |= std::any_of(expected, expected + width, [](uint8_t value) { return value != 23; });
                    }
                }
            }
            if (fixtureDiffers) std::puts("Vulkan fixture: upload/readback differs on this device (informational)");
            check("transfer charges actual allocation and preserves display rectangle", cpu && cpu->data[0] &&
                cpu->width == 32 && cpu->height == 16 && cpu->linesize[0] >= 1920 &&
                actual > displayCharge && actual <= budget->used() && displayMatches);
            std::printf("Vulkan transfer: display 32x16, surface 1920x1088, allocated %zu, admitted %zu bytes\n", actual, budget->used());
            av_frame_free(&reference);
        }
        check("transfer frame freed before credit returned", budget->used() == 0);
    }
    av_frame_free(&hardware); av_buffer_unref(&frames); av_buffer_unref(&device);
}

// Real mux/encode/decode fixtures need no CLI tools, network or display.
bool makeVideo(const tc::fs::path& path, int width, int height, int frames, const char* container = "matroska",
               AVCodecID codecId = AV_CODEC_ID_FFV1, int trailingPacketBytes = 0) {
    AVFormatContext* format = nullptr;
    if (avformat_alloc_output_context2(&format, nullptr, container, path.c_str()) < 0) return false;
    const AVCodec* encoder = avcodec_find_encoder(codecId);
    AVCodecContext* codec = encoder ? avcodec_alloc_context3(encoder) : nullptr;
    if (!codec) { avformat_free_context(format); return false; }
    codec->width = width; codec->height = height;
    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->time_base = AVRational{1, 1000};
    codec->framerate = AVRational{1000, 1};
    if (codecId == AV_CODEC_ID_MPEG2VIDEO) { codec->framerate = AVRational{25, 1}; codec->time_base = AVRational{1, 25}; }
    codec->thread_count = 1;
    codec->gop_size = 1;
    if (format->oformat->flags & AVFMT_GLOBALHEADER) codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    bool ok = avcodec_open2(codec, encoder, nullptr) >= 0;
    AVStream* stream = avformat_new_stream(format, nullptr);
    stream->time_base = codec->time_base;
    ok = ok && avcodec_parameters_from_context(stream->codecpar, codec) >= 0;
    ok = ok && avio_open(&format->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0;
    ok = ok && avformat_write_header(format, nullptr) >= 0;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    frame->format = codec->pix_fmt; frame->width = width; frame->height = height;
    ok = ok && av_frame_get_buffer(frame, 32) >= 0;
    auto drain = [&] {
        while (avcodec_receive_packet(codec, packet) == 0) {
            av_packet_rescale_ts(packet, codec->time_base, stream->time_base);
            packet->stream_index = stream->index;
            packet->duration = av_rescale_q(1, codec->time_base, stream->time_base);
            if (av_interleaved_write_frame(format, packet) < 0) ok = false;
            av_packet_unref(packet);
        }
    };
    for (int i = 0; i < frames && ok; ++i) {
        ok = av_frame_make_writable(frame) >= 0;
        for (int plane = 0; plane < 3; ++plane)
            for (int row = 0; row < (plane ? (height + 1) / 2 : height); ++row)
                std::memset(frame->data[plane] + size_t(row) * frame->linesize[plane], plane ? 128 : 64,
                            plane ? (width + 1) / 2 : width);
        frame->pts = i;
        ok = ok && avcodec_send_frame(codec, frame) >= 0;
        drain();
    }
    if (ok) { ok = avcodec_send_frame(codec, nullptr) >= 0; drain(); }
    if (ok && trailingPacketBytes) {
        ok = av_new_packet(packet, trailingPacketBytes) >= 0;
        if (ok) {
            std::memset(packet->data, 0, packet->size);
            packet->stream_index = stream->index;
            packet->pts = packet->dts = av_rescale_q(frames, codec->time_base, stream->time_base);
            packet->duration = av_rescale_q(1, codec->time_base, stream->time_base);
            ok = av_interleaved_write_frame(format, packet) >= 0;
        }
    }
    if (ok) ok = av_write_trailer(format) >= 0;
    av_frame_free(&frame); av_packet_free(&packet); avcodec_free_context(&codec);
    if (format->pb) avio_closep(&format->pb);
    avformat_free_context(format);
    return ok;
}

bool changedSizeVideo(const tc::fs::path& dir) {
    AVFormatContext* output = nullptr;
    if (avformat_alloc_output_context2(&output, nullptr, "matroska", (dir / "changed.mkv").c_str()) < 0) return false;
    AVPacket* packet = av_packet_alloc();
    bool ok = packet != nullptr;
    AVStream* stream = nullptr;
    int index = 0;
    for (const char* name : {"first.m2v", "second.m2v"}) {
        if (!ok) break;
        AVFormatContext* input = nullptr;
        ok = avformat_open_input(&input, (dir / name).c_str(), nullptr, nullptr) >= 0;
        if (ok) ok = avformat_find_stream_info(input, nullptr) >= 0;
        if (ok && !stream) {
            stream = avformat_new_stream(output, nullptr);
            ok = stream && avcodec_parameters_copy(stream->codecpar, input->streams[0]->codecpar) >= 0;
            if (ok) {
                stream->time_base = AVRational{1, 1000};
                stream->avg_frame_rate = AVRational{25, 1};
                ok = avio_open(&output->pb, (dir / "changed.mkv").c_str(), AVIO_FLAG_WRITE) >= 0;
            }
            if (ok) ok = avformat_write_header(output, nullptr) >= 0;
        }
        while (ok && av_read_frame(input, packet) >= 0) {
            packet->stream_index = stream->index;
            packet->pts = packet->dts = av_rescale_q(index++, AVRational{1, 25}, stream->time_base);
            packet->duration = av_rescale_q(1, AVRational{1, 25}, stream->time_base);
            ok = av_interleaved_write_frame(output, packet) >= 0;
            av_packet_unref(packet);
        }
        avformat_close_input(&input);
    }
    if (ok) ok = av_write_trailer(output) >= 0;
    av_packet_free(&packet);
    if (output->pb) avio_closep(&output->pb);
    avformat_free_context(output);
    return ok;
}

void playHeadless(const tc::fs::path& path, const char* label) {
    using Access = tc::internal::VideoPlayerPlatformAccess;
    tc::VideoPlayer player;
    player.setResyncThreshold(0);
    bool loaded = Access::loadHeadlessForTests(player, path);
    check(label, loaded);
    if (!loaded) return;
    Access::playHeadlessForTests(player);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    bool gotFrame = false;
    do {
        Access::updateHeadlessForTests(player);
        gotFrame |= player.getPixels() && player.getPixels()[0] != 0;
        if (player.isDone() || player.hasError()) break;
        std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    check("whole video reaches EOF with decoded output", player.isDone() && !player.hasError() && gotFrame);
    std::printf("peak admitted bytes: %zu\n", Access::decodePeakBytesForTests(player));
    check("live retained storage stays bounded", Access::decodePeakBytesForTests(player) <= Limits::workingBytes);
    check("admitted video has no storage rejections", Access::decodeRejectionsForTests(player) == 0);
    player.close();
}

void extractionRejectionChecks(const tc::fs::path& dir) {
    const auto buffered = dir / "buffered-picture.mkv";
    check("single delayed MPEG-2 picture generated", makeVideo(buffered, 32, 16, 1, "matroska", AV_CODEC_ID_MPEG2VIDEO));
    tc::Pixels delayed;
    check("ordinary EOF still flushes a delayed thumbnail", tc::VideoPlayer::extractFrame(buffered, delayed, 0) && delayed.getWidth() == 32);
    check("ordinary EOF still flushes a delayed keyframe", tc::VideoPlayer::extractKeyFrame(buffered, delayed, 0) && delayed.getWidth() == 32);
    const auto path = dir / "oversized-packet.mkv";
    check("buffered MPEG-2 picture plus oversized packet generated",
        makeVideo(path, 32, 16, 1, "matroska", AV_CODEC_ID_MPEG2VIDEO, 65 * 1024 * 1024));
    for (bool keyframe : {false, true}) {
        tc::Pixels image;
        image.allocate(3, 2, 4);
        std::memset(image.getData(), 0x5a, 24);
        auto* previous = image.getData();
        int warnings = 0;
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& log) {
            if (log.level == tc::LogLevel::Warning && log.message.find(
                "capacity 67108864 bytes, needed 68157440 bytes; rejected 1") != std::string::npos) ++warnings;
        });
        const bool result = keyframe ? tc::VideoPlayer::extractKeyFrame(path, image, 0) :
                                      tc::VideoPlayer::extractFrame(path, image, 0);
        check(keyframe ? "keyframe rejects whole extraction after oversized packet" :
                         "thumbnail rejects whole extraction after oversized packet", !result);
        check("packet rejection is counted with actual bytes", warnings == 1);
        check("failed extraction preserves previous output", image.getData() == previous &&
            image.getWidth() == 3 && image.getHeight() == 2 &&
            std::all_of(image.getData(), image.getData() + 24, [](uint8_t b) { return b == 0x5a; }));
    }
}

void ffmpegChecks(const tc::fs::path& dir, bool large) {
    extractionRejectionChecks(dir);
    // Directly exercise the real codec allocator, including format changes,
    // negative image-size errors and simultaneous output reservations.
    auto budget = std::make_shared<Budget>();
    AVCodecContext* codec = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_FFV1));
    codec->pix_fmt = AV_PIX_FMT_YUV420P;
    codec->width = 32; codec->height = 16;
    tc::internal::configureVideoCodec(codec, budget);
    check("codec limits configured before open", codec->max_pixels == int64_t(Limits::frameBytes / 4) && codec->thread_count == 1);
    check("FFV1 decoder opened", avcodec_open2(codec, avcodec_find_decoder(AV_CODEC_ID_FFV1), nullptr) >= 0);
    AVFrame* frame = av_frame_alloc();
    frame->width = 32; frame->height = 16; frame->format = AV_PIX_FMT_YUV420P;
    check("real decoded allocation admitted", tc::internal::boundedVideoBuffer(codec, frame, 0) == 0 && budget->used() > 0);
    check("actual decoded row validation", tc::internal::validVideoFrame(frame, *budget));
    frame->linesize[0] = 1;
    check("malformed decoded stride rejected", !tc::internal::validVideoFrame(frame, *budget));
    av_frame_unref(frame);
    check("codec buffer release returns credit", budget->used() == 0);
    frame->width = 32; frame->height = 16; frame->format = -1;
    check("negative image size rejected", tc::internal::boundedVideoBuffer(codec, frame, 0) < 0 && budget->used() == 0);
    frame->format = AV_PIX_FMT_YUV420P; frame->width = 8193;
    check("changed invalid size rejected before allocation", tc::internal::boundedVideoBuffer(codec, frame, 0) < 0 && budget->used() == 0);
    frame->width = 32;
    check("reserve all working storage", budget->acquire(Limits::workingBytes));
    check("codec allocation rejects before exceeding live budget",
          tc::internal::boundedVideoBuffer(codec, frame, 0) < 0 && budget->used() == Limits::workingBytes);
    codec->hw_device_ctx = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_VAAPI);
    check("attached device context allocated", codec->hw_device_ctx != nullptr);
    const auto rejections = budget->rejected();
    check("FFV1 software frame with attached device still obeys exhausted budget",
          tc::internal::boundedVideoBuffer(codec, frame, 0) < 0 && !frame->buf[0] &&
          budget->used() == Limits::workingBytes && budget->rejected() == rejections + 1);
    av_frame_unref(frame);
    av_buffer_unref(&codec->hw_device_ctx);
    budget->release(Limits::workingBytes);
    frame->format = AV_PIX_FMT_NV12; frame->width = 64; frame->height = 32;
    codec->pix_fmt = AV_PIX_FMT_NV12; codec->width = 64; codec->height = 32;
    check("changed admitted pixel format gets valid storage",
          tc::internal::boundedVideoBuffer(codec, frame, 0) == 0 && tc::internal::validVideoFrame(frame, *budget));
    av_frame_free(&frame); avcodec_free_context(&codec);
    check("wide metadata fixture generated", makeVideo(dir / "wide.mkv", 8194, 2, 1));
    tc::VideoPlayer refused;
    check("wide metadata rejected before decoder/output allocation",
          !tc::internal::VideoPlayerPlatformAccess::loadHeadlessForTests(refused, dir / "wide.mkv") &&
          tc::internal::VideoPlayerPlatformAccess::decodePeakBytesForTests(refused) == 0 &&
          tc::internal::VideoPlayerPlatformAccess::decodeRejectionsForTests(refused) == 1);
    check("first size segment generated", makeVideo(dir / "first.m2v", 32, 16, 2, "mpeg2video", AV_CODEC_ID_MPEG2VIDEO));
    check("changed size segment generated", makeVideo(dir / "second.m2v", 64, 32, 2, "mpeg2video", AV_CODEC_ID_MPEG2VIDEO));
    check("changed size timeline muxed", changedSizeVideo(dir));
    playHeadless(dir / "changed.mkv", "mid-stream size change plays safely");
    check("small fixture generated", makeVideo(dir / "small.mkv", 64, 32, 8));
    playHeadless(dir / "small.mkv", "small video loads");
    tc::Pixels image;
    check("thumbnail uses bounded decoder", tc::VideoPlayer::extractFrame(dir / "small.mkv", image, 0) && image.getWidth() == 64 && image.getData()[0] != 0);
    check("long fixture generated", makeVideo(dir / "long.mkv", 64, 32, 4000));
    playHeadless(dir / "long.mkv", "long video streams without duration cap");
    if (large) {
        check("exact-limit fixture generated", makeVideo(dir / "limit.mkv", 8192, 4096, 3));
        playHeadless(dir / "limit.mkv", "exact-limit frame plays");
        check("8K fixture generated", makeVideo(dir / "8k.mkv", 7680, 4320, 3));
        playHeadless(dir / "8k.mkv", "8K frame plays");
    }
}
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    layoutChecks();
    if (argc > 2 && std::string(argv[1]) == "--device-fixture") {
        deviceFixture = tc::fs::absolute(argv[2]);
        deviceMode = argc > 3 ? argv[3] : "play";
        if (deviceMode != "play" && deviceMode != "resize" && deviceMode != "reject" &&
            deviceMode != "extract-reject" && deviceMode != "audio-accept" && deviceMode != "audio-reject") {
            std::fprintf(stderr, "Unknown device fixture mode: %s\n", deviceMode.c_str());
            return 2;
        }
        tc::WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        return tcCoreTest::runDisplayTest<DeviceVideoCheck>(settings, failures, [] {
            check("device fixture check completed", deviceCompleted);
        });
    }
#if defined(__linux__) && !defined(__ANDROID__)
    hardwareTransferChecks();
    const bool large = std::any_of(argv + 1, argv + argc, [](const char* arg) { return std::string(arg) == "--large"; });
    auto dir = tc::fs::temp_directory_path() / ("tc-video-decode-" + std::to_string(getpid()));
    tc::fs::create_directories(dir);
    ffmpegChecks(dir, large);
    if (argc > 2 && std::string(argv[1]) == "--write-fixtures") {
        tc::fs::create_directories(argv[2]);
        tc::fs::copy(dir, tc::fs::path(argv[2]), tc::fs::copy_options::recursive | tc::fs::copy_options::overwrite_existing);
    }
    tc::fs::remove_all(dir);
#else
    (void)argc; (void)argv;
#endif
    return failures ? 1 : 0;
}
