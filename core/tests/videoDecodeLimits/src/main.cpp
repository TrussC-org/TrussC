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
using Status = tc::internal::VideoDecodeStatus;

void layoutChecks() {
    size_t bytes = 0;
    check("8K frame admitted", Limits::rgba(7680, 4320, bytes) && bytes == 132710400);
    check("large frame layout", Limits::rgba(8192, 4096, bytes) && bytes == 134217728);
    check("frame above previous byte cap", Limits::rgba(8192, 4097, bytes));
    check("wide frame layout", Limits::rgba(8193, 1, bytes));
    check("negative dimension rejected", !Limits::rgba(-1, 4, bytes));
    check("empty frame rejected", !Limits::rgba(0, 4, bytes));
    check("product overflow rejected", !Limits::product((std::numeric_limits<size_t>::max)(), 4, bytes));
    check("padded frame rows admitted", Limits::rows(3, 2, 16, 32));
    check("short stride rejected", !Limits::rows(3, 2, 11, 32));
    check("short buffer rejected", !Limits::rows(3, 2, 16, 31));
    check("stride product overflow rejected", !Limits::rows(3, 2, (std::numeric_limits<size_t>::max)(), 32));

    Status status;
    int warnings = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& log) {
        if (log.level == tc::LogLevel::Warning && log.message.find("Video decode storage") != std::string::npos) ++warnings;
    });
    status.reject(1, 0); status.warn(); status.reject(2, 0); status.warn();
    check("first loss warns once and totals persist", warnings == 1 && status.rejected() == 2 && status.rejected() == 2);

    Status stagingStatus;
    int stagingWarnings = 0;
    auto stagingListener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& log) {
        if (log.level == tc::LogLevel::Warning && log.message.find("Video file staging") != std::string::npos) {
            ++stagingWarnings;
            check("staging warning reports actual store/capacity/needed bytes",
                log.message.find("capacity 67108864 bytes, needed 68157440 bytes; rejected 1") != std::string::npos);
        }
    });
    stagingStatus.reject(68157440, 67108864, "Video file staging"); stagingStatus.warn();
    stagingStatus.reject(68157440, 67108864, "Video file staging"); stagingStatus.warn();
    check("staging warns once and counts each rejection", stagingWarnings == 1 && stagingStatus.rejected() == 2);

    {
        Status diagnostic;
        auto failingLog = tc::getLogger().onLog.listen([](tc::LogEventArgs& log) {
            if (log.message.find("Video allocation diagnostic") != std::string::npos) throw std::bad_alloc();
        });
        diagnostic.reject(1, 0, "Video allocation diagnostic");
        bool escaped = false;
        try { diagnostic.warn(); } catch (const std::bad_alloc&) { escaped = true; }
        check("allocation failure while warning is contained", !escaped && diagnostic.rejected() == 1);
    }

    std::vector<uint8_t> track;
    tc::internal::VideoAudioStorage storage(track, status);
    std::vector<uint8_t> chunk(1024 * 1024, 0x5a);
    for (int i = 0; i < 65; ++i)
        if (!storage.append(chunk.data(), chunk.size())) { check("track append", false); break; }
    check("audio track grows past 64 MiB", track.size() == 65u * 1024 * 1024 && track.back() == 0x5a);

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
        if (deviceMode == "extract-reject") {
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
            finish(); return;
        }
        player.setResyncThreshold(0);
        player.setVolume(0);
        const bool loaded = bool(player.load(deviceFixture));
        check("device fixture loads", loaded);
        if (!loaded) { finish(); return; }
        if (deviceMode == "audio-accept") {
            check("device fixture has an audio track", player.hasAudio());
            auto data = player.getAudioData();
            std::printf("extracted audio bytes: %zu\n", data.size());
            const auto rejected = tc::internal::VideoPlayerPlatformAccess::decodeRejectionsForTests(player);
            auto reference = deviceFixture;
            reference.replace_extension(".aac");
            std::error_code error;
            const auto expectedBytes = tc::fs::file_size(reference, error);
            check("device audio reference is available", !error);
            check("device audio extraction is complete", !error && data.size() == expectedBytes && rejected == 0);
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
        std::printf("Device video fixture: %s (%d failures)\n", failures ? "FAIL" : "PASS", failures);
        std::fflush(stdout);
        tc::exitApp();
    }
};

#if defined(__linux__) && !defined(__ANDROID__)
void hardwareTransferChecks() {
    auto status = std::make_shared<Status>();
    size_t bytes = 0;
    check("allocated surface layout includes padding", tc::internal::videoTransferLayout(1920, 1088, AV_PIX_FMT_YUV420P, bytes, *status)
          && bytes > 1920 * 1088 * 3 / 2);
    check("wide surface layout", tc::internal::videoTransferLayout(8193, 16, AV_PIX_FMT_YUV420P, bytes, *status));
    check("negative surface rejected", !tc::internal::videoTransferLayout(32, -1, AV_PIX_FMT_YUV420P, bytes, *status));
    check("large surface layout", tc::internal::videoTransferLayout(8192, 8192, AV_PIX_FMT_RGBA, bytes, *status));
    check("invalid transfer format rejected", !tc::internal::videoTransferLayout(32, 16, AV_PIX_FMT_NONE, bytes, *status));
    AVFrame* packed = av_frame_alloc();
    packed->format = AV_PIX_FMT_RGB24; packed->width = 33; packed->height = 65;
    check("packed transfer rows sized explicitly", tc::internal::videoTransferLayout(33, 65, AV_PIX_FMT_RGB24,
        bytes, *status, packed->linesize) && packed->linesize[0] == 128);
    check("packed transfer preallocation fits admitted layout", av_frame_get_buffer(packed, 64) >= 0 &&
        packed->buf[0]->size <= bytes && packed->linesize[0] == 128);
    av_frame_free(&packed);

    AVBufferRef* device = nullptr;
    const int deviceResult = av_hwdevice_ctx_create(&device, AV_HWDEVICE_TYPE_VULKAN, nullptr, nullptr, 0);
    if (deviceResult < 0) {
        char error[AV_ERROR_MAX_STRING_SIZE] = {};
        av_strerror(deviceResult, error, sizeof(error));
        std::printf("Vulkan transfer: SKIP (device unavailable: %s); synthetic surface admission exercised\n", error);
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
        const size_t displayCharge = av_image_get_buffer_size(AV_PIX_FMT_YUV420P, 64, 64, 64) + AV_INPUT_BUFFER_PADDING_SIZE;
        {
            AVFrame* reference = av_frame_alloc();
            const bool referenceTransferred = reference && av_hwframe_transfer_data(reference, hardware, 0) >= 0;
            check("FFmpeg reference surface transfer succeeds", referenceTransferred);
            tc::internal::VideoCPUTransfer transfer(status);
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
            check("transfer preserves full surface and display rectangle", cpu && cpu->data[0] &&
                cpu->width == 32 && cpu->height == 16 && cpu->linesize[0] >= 1920 &&
                actual > displayCharge && displayMatches);
            av_frame_free(&reference);
        }
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
    auto status = std::make_shared<Status>();
    AVCodecContext* codec = avcodec_alloc_context3(avcodec_find_decoder(AV_CODEC_ID_FFV1));
    if (codec) tc::internal::configureVideoCodec(codec);
    check("FFmpeg decode thread count is auto", codec && codec->thread_count == 0);
    avcodec_free_context(&codec);
    check("wide fixture generated", makeVideo(dir / "wide.mkv", 10240, 1080, 1));
    playHeadless(dir / "wide.mkv", "10240x1080 CPU frame decodes");
    {
        tc::VideoPlayer headless;
        headless.setUseHwAccel(false);
        headless.setAutoPoster(false);
        check("public headless load needs no GPU texture", headless.load(dir / "wide.mkv") &&
            headless.isLoaded() && !headless.getPath().empty() && !headless.getTexture().isAllocated());
        headless.close();
    }
    std::error_code fixtureError;
    const auto fixtureBytes = tc::fs::file_size(dir / "wide.mkv", fixtureError);
    if (!fixtureError) std::printf("10240x1080 fixture size: %llu bytes\n", static_cast<unsigned long long>(fixtureBytes));
    tc::Pixels wide;
    check("wide thumbnail decodes on CPU", tc::VideoPlayer::extractFrame(dir / "wide.mkv", wide, 0) &&
        wide.getWidth() == 10240 && wide.getHeight() == 1080 && wide.getData()[0] != 0);
    AVFrame* frame = av_frame_alloc();
    frame->width = 32; frame->height = 16; frame->format = AV_PIX_FMT_YUV420P;
    check("FFmpeg frame allocated", av_frame_get_buffer(frame, 64) == 0);
    check("decoded row layout", tc::internal::validVideoFrame(frame, *status));
    frame->linesize[0] = 1;
    check("malformed decoded stride rejected", !tc::internal::validVideoFrame(frame, *status));
    av_frame_free(&frame);
    check("first size segment generated", makeVideo(dir / "first.m2v", 32, 16, 2, "mpeg2video", AV_CODEC_ID_MPEG2VIDEO));
    check("changed size segment generated", makeVideo(dir / "second.m2v", 64, 32, 2, "mpeg2video", AV_CODEC_ID_MPEG2VIDEO));
    check("changed size timeline muxed", changedSizeVideo(dir));
    playHeadless(dir / "changed.mkv", "mid-stream size change plays safely");
    check("small fixture generated", makeVideo(dir / "small.mkv", 64, 32, 8));
    playHeadless(dir / "small.mkv", "small video loads");
    tc::Pixels image;
    check("thumbnail decodes", tc::VideoPlayer::extractFrame(dir / "small.mkv", image, 0) && image.getWidth() == 64 && image.getData()[0] != 0);
    const auto* previousPixels = image.getData();
    av_max_alloc(1);
    const bool exhausted = tc::VideoPlayer::extractFrame(dir / "small.mkv", image, 0);
    av_max_alloc((std::numeric_limits<size_t>::max)());
    check("FFmpeg allocation failure returns false and preserves output", !exhausted && image.getData() == previousPixels);
    check("long fixture generated", makeVideo(dir / "long.mkv", 64, 32, 4000));
    playHeadless(dir / "long.mkv", "long video streams without duration cap");
    if (large) {
        check("large fixture generated", makeVideo(dir / "limit.mkv", 8192, 4096, 3));
        playHeadless(dir / "limit.mkv", "large frame plays");
        check("8K fixture generated", makeVideo(dir / "8k.mkv", 7680, 4320, 3));
        playHeadless(dir / "8k.mkv", "8K frame plays");
    }
}
class TextureAllocationCheck : public tc::App {
public:
    void setup() override {
        const auto path = tc::fs::temp_directory_path() / ("tc-video-texture-" + std::to_string(getpid()) + ".mkv");
        check("texture test fixture generated", makeVideo(path, 32, 16, 1));
        tc::VideoPlayer player;
        player.setUseHwAccel(false);
        player.setAutoPoster(false);
        check("GPU rejects dimensions above its device limit", !tc::internal::checkImageSize(sg_query_limits().max_image_size_2d + 1, 1));
        const auto invalidPath = path.string() + ".invalid";
        { std::ofstream invalid(invalidPath); invalid << "invalid video bytes"; }
        check("error retention fixture loads", bool(player.load(path)));
        int events = 0;
        auto listener = player.onError.listen([&](tc::VideoErrorEventArgs&) { ++events; });
        tc::internal::VideoPlayerPlatformAccess::reportError(player, {"original playback failure", -42});
        player.update();
        check("playback error delivered", events == 1 && player.hasError());
        check("existing invalid reload retains delivered error", !player.load(invalidPath) &&
            player.hasError() && player.getErrorMessage() == "original playback failure" &&
            !player.isLoaded() && player.getPath().empty() && events == 1);
        player.close();
        check("explicit close clears delivered error", !player.hasError());
        tc::fs::remove(invalidPath);
        const auto exhaust = [&](auto allocate, auto release, int count, const char* pool) {
            std::printf("Exhausting GPU %s pool\n", pool);
            // Both output overloads must clear a previously populated Image on upload failure.
            tc::Image extracted[2];
            for (auto& output : extracted) {
                output.allocate(3, 2, 4);
                if (output.getPixelsData()) std::memset(output.getPixelsData(), 0x5a, 24);
                check("extraction output starts populated", output.getPixelsData() && output.getWidth() == 3 &&
                    output.getTexture().isAllocated());
            }
            const uint8_t block[8] = {};
            const bool bc1 = sg_query_pixelformat(SG_PIXELFORMAT_BC1_RGBA).sample;
            tc::Texture replacement;
            if (bc1) {
                replacement.allocateCompressed(4, 4, SG_PIXELFORMAT_BC1_RGBA, block, sizeof(block));
                check("compressed replacement starts allocated", replacement.isAllocated());
            }
            std::vector<decltype(allocate())> reserved;
            for (int i = 0; i < count; ++i) {
                auto handle = allocate();
                if (handle.id) reserved.push_back(handle);
            }
            const auto result = player.load(path);
            check("GPU allocation failure fails load and clears path", !result && player.getPath().empty() &&
                !player.isLoaded() && !player.getTexture().isAllocated());
            for (int i = 0; i < 2; ++i) {
                const bool ok = i ? tc::VideoPlayer::extractKeyFrame(path, extracted[i], 0)
                                  : tc::VideoPlayer::extractFrame(path, extracted[i], 0);
                check("frame image allocation failure clears populated output", !ok &&
                    !extracted[i].getPixelsData() && extracted[i].getWidth() == 0 &&
                    extracted[i].getHeight() == 0 && !extracted[i].getTexture().isAllocated());
            }
            if (bc1) {
                replacement.updateCompressed(block, sizeof(block));
                check("failed compressed replacement clears allocation state", !replacement.isAllocated());
                tc::Texture compressed;
                compressed.allocateCompressed(4, 4, SG_PIXELFORMAT_BC1_RGBA, block, sizeof(block));
                check("compressed GPU allocation failure stays unallocated", !compressed.isAllocated());
            }
            for (auto handle : reserved) release(handle);
            check("load succeeds after GPU storage is available", bool(player.load(path)) && !player.getPath().empty());
            const auto& texture = player.getTexture();
            check("loaded texture image view and sampler are valid", texture.isAllocated() &&
                sg_query_image_state(texture.getImage()) == SG_RESOURCESTATE_VALID &&
                sg_query_view_state(texture.getView()) == SG_RESOURCESTATE_VALID &&
                sg_query_sampler_state(texture.getSampler()) == SG_RESOURCESTATE_VALID);
            player.close();
        };
        const auto desc = sg_query_desc();
        exhaust(sg_alloc_image, sg_dealloc_image, desc.image_pool_size, "image");
        exhaust(sg_alloc_view, sg_dealloc_view, desc.view_pool_size, "view");
        exhaust(sg_alloc_sampler, sg_dealloc_sampler, desc.sampler_pool_size, "sampler");
        tc::fs::remove(path);
        tc::exitApp();
    }
};

#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    layoutChecks();
    if (argc > 2 && std::string(argv[1]) == "--device-fixture") {
        deviceFixture = tc::fs::absolute(argv[2]);
        deviceMode = argc > 3 ? argv[3] : "play";
        if (deviceMode != "play" && deviceMode != "resize" &&
            deviceMode != "extract-reject" && deviceMode != "audio-accept") {
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
    if (argc > 1 && std::string(argv[1]) == "--texture-check") {
        tc::WindowSettings settings;
        settings.setSize(64, 64);
        return tcCoreTest::runDisplayTest<TextureAllocationCheck>(settings, failures, [] {});
    }
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
