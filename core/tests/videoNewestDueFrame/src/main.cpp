#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#if defined(__linux__) && !defined(__ANDROID__)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
#include <unistd.h>
namespace trussc::internal {
    double videoPauseClockDriftForTests(VideoPlayer& player, double pausedSeconds);
}

#endif

namespace {
#if defined(__linux__) && !defined(__ANDROID__)
using Access = tc::internal::VideoPlayerPlatformAccess;
int failures = 0;
void check(const char* name, bool ok) {
    if (!ok) { std::printf("FAIL: %s\n", name); ++failures; }
}
bool nearlyEqual(double a, double b) { return std::abs(a - b) < 0.0001; }
size_t openFdCount() {
    return std::distance(tc::fs::directory_iterator("/proc/self/fd"), tc::fs::directory_iterator{});
}
bool videoFixture(const tc::fs::path& path, const char* container = "avi") {
    AVFormatContext* f = nullptr;
    if (avformat_alloc_output_context2(&f, nullptr, container, path.c_str()) < 0) return false;
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_FFV1);
    AVCodecContext* c = encoder ? avcodec_alloc_context3(encoder) : nullptr;
    if (!c) { avformat_free_context(f); return false; }
    c->width = c->height = 16;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->time_base = AVRational{1, 30};
    c->gop_size = 6;
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    bool ok = avcodec_open2(c, encoder, nullptr) >= 0;
    AVStream* video = avformat_new_stream(f, nullptr);
    video->time_base = c->time_base;
    video->avg_frame_rate = AVRational{30, 1};
    ok = ok && avcodec_parameters_from_context(video->codecpar, c) >= 0;
    ok = ok && avio_open(&f->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0;
    ok = ok && avformat_write_header(f, nullptr) >= 0;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    frame->format = c->pix_fmt;
    frame->width = frame->height = 16;
    ok = ok && av_frame_get_buffer(frame, 0) >= 0;
    auto drain = [&] {
        while (avcodec_receive_packet(c, packet) == 0) {
            av_packet_rescale_ts(packet, c->time_base, video->time_base);
            packet->stream_index = video->index;
            packet->duration = av_rescale_q(1, c->time_base, video->time_base);
            if (av_interleaved_write_frame(f, packet) < 0) ok = false;
            av_packet_unref(packet);
        }
    };
    for (int i = 0; i < 900 && ok; ++i) {
        av_frame_make_writable(frame);
        for (int plane = 0; plane < 3; ++plane)
            for (int y = 0; y < (plane ? 8 : 16); ++y)
                std::memset(frame->data[plane] + y * frame->linesize[plane],
                            plane ? 128 : 32 + i % 180, plane ? 8 : 16);
        frame->pts = i;
        ok = avcodec_send_frame(c, frame) >= 0;
        drain();
    }
    if (ok) { ok = avcodec_send_frame(c, nullptr) >= 0; drain(); }
    if (ok) ok = av_write_trailer(f) >= 0;
    av_packet_free(&packet);
    av_frame_free(&frame);
    avcodec_free_context(&c);
    if (f->pb) avio_closep(&f->pb);
    avformat_free_context(f);
    return ok;
}


#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (argc == 5 && std::string(argv[1]) == "--hw-device") {
        double loadAverage = 0;
        if (getloadavg(&loadAverage, 1) != 1 || loadAverage >= 2.0) {
            std::fprintf(stderr, "Device check requires load1 < 2\n");
            return 2;
        }
        const int count = std::stoi(argv[3]);
        const int threads = std::stoi(argv[4]);
        if (count < 1 || count > 4 || threads < 0) return 2;
        std::array<tc::VideoPlayer, 4> players;
        for (int i = 0; i < count; ++i) {
            players[i].setDecodeThreads(threads);
            if (!Access::loadHeadlessForTests(players[i], tc::fs::absolute(argv[2]), true)) return 1;
        }
        for (int i = 0; i < count; ++i) {
            Access::stepPlaybackForTests(players[i], 0.0, 8);
            std::printf("player %d: isUsingHwAccel=%d getHwAccelName=%s\n", i,
                players[i].isUsingHwAccel(), players[i].getHwAccelName().c_str());
        }
        return 0;
    }
    const auto path = tc::fs::temp_directory_path() /
        ("tc-newest-frame-" + std::to_string(getpid()) + ".avi");
    if (!videoFixture(path)) return 1;
    auto load = [&](tc::VideoPlayer& player) {
        player.setDecodeThreads(1);
        check("headless load", Access::loadHeadlessForTests(player, path));
    };
    {
        tc::VideoPlayer player;
        const auto baselineFds = openFdCount();
        auto close = [&] {
            player.close();
            check("close releases decoder file descriptors", openFdCount() == baselineFds);
        };
        load(player);
        int warnings = 0;
        auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
            if (event.level == tc::LogLevel::Warning && event.message ==
                "[VideoPlayer] cuda was requested but FFmpeg decodes in software; reporting software") ++warnings;
        });
        Access::decodedBackendForTests(player, AV_PIX_FMT_YUV420P, false, true);
        check("software first frame reports fallback", !player.isUsingHwAccel() && player.getHwAccelName() == "software" && warnings == 1);
        Access::decodedBackendForTests(player, AV_PIX_FMT_YUV420P, false, false);
        check("fallback warning once per load", warnings == 1);
        close();
        load(player);
        Access::decodedBackendForTests(player, AV_PIX_FMT_CUDA, true, true);
        check("hardware frame retains CUDA reporting", player.isUsingHwAccel() && player.getHwAccelName() == "cuda" && warnings == 1);
        Access::decodedBackendForTests(player, AV_PIX_FMT_YUV420P, false, false);
        check("only first frame checked", player.isUsingHwAccel() && warnings == 1);
        Access::decodedBackendForTests(player, AV_PIX_FMT_CUDA, false, true);
        check("hardware pixel format sufficient", player.isUsingHwAccel() && warnings == 1);
        Access::decodedBackendForTests(player, AV_PIX_FMT_YUV420P, true, true);
        check("hardware frames context sufficient", player.isUsingHwAccel() && warnings == 1);
        player.setPosition(0.0f);
        Access::stepPlaybackForTests(player, 0.0, 2);
        check("seek naturally rechecks the next decoded frame", !player.isUsingHwAccel() && player.getHwAccelName() == "software" && warnings == 2);
        player.setPosition(0.0f);
        Access::stepPlaybackForTests(player, 0.0, 2);
        check("seek does not repeat a fallback warning", warnings == 2);
        close();
        load(player);
        Access::decodedBackendForTests(player, AV_PIX_FMT_YUV420P, false, true);
        check("reload allows one new warning", !player.isUsingHwAccel() && player.getHwAccelName() == "software" && warnings == 3);
        close();
    }
    {
        tc::VideoPlayer player;
        load(player);
        auto state = Access::stepPlaybackForTests(player, 0.11, 4);
        check("newest due frame, older frames dropped", state.presented && nearlyEqual(state.presentedPts, 0.1) && state.queued == 0);
        state = Access::stepPlaybackForTests(player, 0.11, 1);
        check("future frame retained", !state.presented && state.queued == 1);
        state = Access::stepPlaybackForTests(player, 0.14, 0);
        check("future frame presented when due", state.presented && nearlyEqual(state.presentedPts, 4.0/30));
    }
    {
        tc::VideoPlayer player;
        load(player);
        for (int i = 0; i < 300; ++i) {
            auto state = Access::stepPlaybackForTests(player, i / 30.0 + 0.002, 1);
            check("normal playback presents every frame in order without seeks", state.presented && std::abs(state.presentedPts - i / 30.0) < 0.001 && state.seeks == 0);
        }
    }
    {
        tc::VideoPlayer player;
        load(player);
        // FFV1 fixture: keyframes every 0.2 s. Threshold is independently
        // exercised; no destination at/behind decoding or at/after the clock.
        player.setResyncThreshold(0.01f);
        auto state = Access::stepPlaybackForTests(player, 0.2, 1);
        check("keyframe exactly at clock does not seek", state.seeks == 0);
        state = Access::stepPlaybackForTests(player, 0.201, 1);
        check("keyframe ahead and before clock seeks", state.seeks == 1 && nearlyEqual(state.decodedPts, 0.2));
        state = Access::stepPlaybackForTests(player, 0.35, 1);
        check("no forward keyframe keeps decoding", state.seeks == 1 && state.decodedPts > 0.2);
        state = Access::stepPlaybackForTests(player, 0.0, 1);
        check("clock behind never seeks backward", state.seeks == 1 && state.decodedPts > 0.23);
        player.setResyncThreshold(0);
        state = Access::stepPlaybackForTests(player, 5.0, 1);
        check("disabled threshold continues decoding", state.seeks == 1 && state.decodedPts < 1.0);
    }
    {
        tc::VideoPlayer player;
        load(player);
        player.setResyncThreshold(0.01f);
        auto state = Access::stepPlaybackForTests(player, 0.4, 1);
        check("keyframe at clock still allows an earlier forward keyframe",
              state.seeks == 1 && nearlyEqual(state.decodedPts, 0.2));
    }
    {
        std::array<tc::VideoPlayer, 10> players;
        std::array<int, 10> presented{};
        std::array<double, 10> lastPresented{};
        for (auto& player : players) load(player);
        for (int step = 1; step < 100; ++step) {
            double clock = step * 0.2 + 0.01;
            for (size_t i = 0; i < players.size(); ++i) {
                auto state = Access::stepPlaybackForTests(players[i], clock, 1);
                if (state.presented) {
                    ++presented[i];
                    check("overloaded playback never reorders frames", state.presentedPts >= lastPresented[i]);
                    lastPresented[i] = state.presentedPts;
                }
                check("sustained overload bounded lag", clock - state.presentedPts <= 0.71);
            }
        }
        for (int count : presented) check("each overloaded player keeps presenting", count > 30);
    }
    {
        tc::VideoPlayer player;
        load(player);
        auto state = Access::stepPlaybackForTests(player, 2.01, 1);
        const double shown = state.presentedPts;
        player.setPosition(0.01f);
        state = Access::stepPlaybackForTests(player, 0.3, 1);
        check("manual backward seek does not report its target as shown", state.seeks == 2 && state.presentedPts == shown);
        state = Access::stepPlaybackForTests(player, 0.3, 1);
        check("manual seek decodes preceding keyframe", state.presented && state.presentedPts < shown);
        state = Access::stepPlaybackForTests(player, 0.302, 4);
        check("setPosition presents requested frame after preroll",
              state.presented && nearlyEqual(state.presentedPts, 0.3));
        player.setFrame(15);
        state = Access::stepPlaybackForTests(player, 0.5, 1);
        check("setFrame requests correct target", nearlyEqual(state.seekTarget, 0.5));
        state = Access::stepPlaybackForTests(player, 0.502, 4);
        check("setFrame presents requested frame", state.presented && nearlyEqual(state.presentedPts, 0.5));
        player.nextFrame();
        state = Access::stepPlaybackForTests(player, 0.5, 1);
        check("nextFrame target", nearlyEqual(state.seekTarget, state.presentedPts + 1.0/30));
        state = Access::stepPlaybackForTests(player, 0.535, 5);
        check("nextFrame presents next frame", state.presented && nearlyEqual(state.presentedPts, 16.0/30));
        player.previousFrame();
        state = Access::stepPlaybackForTests(player, 0.5, 1);
        check("previousFrame target", nearlyEqual(state.seekTarget, state.presentedPts - 1.0/30));
        state = Access::stepPlaybackForTests(player, 0.501, 4);
        check("previousFrame presents previous frame", state.presented && nearlyEqual(state.presentedPts, 0.5));
        player.setLoop(true);
        player.setPosition(0.99f);
        Access::stepPlaybackForTests(player, 29.999, 1);
        for (int i = 0; i < 20 && !state.seekPending; ++i)
            state = Access::stepPlaybackForTests(player, 29.999, 1);
        check("EOF loop requests beginning", state.seekPending && state.seekTarget == 0);
        state = Access::stepPlaybackForTests(player, 0.0, 1);
        state = Access::stepPlaybackForTests(player, 0.0, 1);
        check("loop resumes at beginning", state.presented && state.presentedPts == 0 && !state.finished);
    }
    {
        tc::VideoPlayer player;
        load(player);
        player.setLoop(true);
        auto state = Access::stepPlaybackForTests(player, 31.41, 1);
        check("clock past end wraps once to preceding keyframe",
              state.seeks == 1 && nearlyEqual(state.decodedPts, 1.4) &&
              nearlyEqual(state.clockPts, 1.41) && nearlyEqual(state.startTimeShift, 30.0));
        check("clock loop immediately presents in new cycle",
              state.presented && nearlyEqual(state.presentedPts, 1.4) && !state.finished);
        state = Access::stepPlaybackForTests(player, 1.45, 1);
        check("next step continues without a second loop",
              state.seeks == 1 && nearlyEqual(state.startTimeShift, 0.0) &&
              state.presented && state.presentedPts > 1.4 && !state.finished);
    }
    {
        tc::VideoPlayer player;
        load(player);
        auto state = Access::stepPlaybackForTests(player, 30.1, 1);
        check("clock past non-loop end presents actual final frame then finishes",
              state.presented && nearlyEqual(state.presentedPts, 899.0 / 30.0) &&
              state.finished && state.queued == 0);
    }
    {
        tc::VideoPlayer player;
        load(player);
        player.setPosition(0.5f);
        auto state = Access::stepPlaybackForTests(player, 15.0, 1, true);
        check("seek before play preserves requested clock without a fake image",
              nearlyEqual(state.clockPts, 15.0) && state.presentedPts == 0);
        // The seek is now acknowledged, but no frame has been shown yet.
        state = Access::stepPlaybackForTests(player, 15.0, 1, true);
        check("play after seek acknowledgment keeps requested clock",
              nearlyEqual(state.clockPts, 15.0) && state.presented && nearlyEqual(state.presentedPts, 15.0));
        player.setPosition(0.99f);
        state = Access::stepPlaybackForTests(player, 30.0, 1);
        for (int i = 0; i < 20 && !state.finished; ++i)
            state = Access::stepPlaybackForTests(player, 30.0, 1);
        check("non-looping playback finishes", state.finished);
        const double lastShown = state.presentedPts;
        state = Access::stepPlaybackForTests(player, 0.0, 0, true);
        check("replay restarts clock and seek while retaining actual shown PTS",
              state.clockPts == 0 && state.seekPending && state.seekTarget == 0 && state.presentedPts == lastShown);
        Access::stepPlaybackForTests(player, 0.0, 1);
        state = Access::stepPlaybackForTests(player, 0.0, 1);
        check("replay presents frame zero", state.presented && state.presentedPts == 0);
    }
    {
        tc::VideoPlayer player;
        load(player);
        Access::stepPlaybackForTests(player, 0.0, 0, true);
        const double drift = tc::internal::videoPauseClockDriftForTests(player, 30.0);
        auto state = Access::stepPlaybackForTests(player, 0.01 + drift, 1);
        check("resume excludes paused time without seeking forward",
              nearlyEqual(drift, 0.0) && state.seeks == 0 && state.presented && state.presentedPts == 0);
    }
    {
        tc::VideoPlayer player;
        load(player);
        player.setPosition(1.0f / 30.0f);
        Access::stepPlaybackForTests(player, 1.0, 1);
        const auto beforeStop = Access::stepPlaybackForTests(player, 1.01, 1);
        player.setSpeed(0.0f);
        Access::updateHeadlessForTests(player);
        auto state = Access::stepPlaybackForTests(player, 5.0, 1);
        check("zero speed holds a finite selected clock",
              nearlyEqual(beforeStop.clockPts, 1.01) && nearlyEqual(state.clockPts, beforeStop.clockPts));
        player.setSpeed(1.0f);
        Access::updateHeadlessForTests(player);
        state = Access::stepPlaybackForTests(player, 1.01, 0);
        check("positive speed resumes a finite clock without rewinding",
              std::isfinite(state.clockPts) && state.clockPts >= 1.0);
    }
    const auto unindexed = path.string() + ".mkv";
    check("unindexed fixture", videoFixture(unindexed, "matroska"));
    {
        tc::VideoPlayer player;
        player.setDecodeThreads(1);
        check("unindexed load", Access::loadHeadlessForTests(player, unindexed));
        auto state = Access::stepPlaybackForTests(player, 20.0, 2);
        check("missing forward index keeps decoding and presenting",
              state.seeks == 0 && state.presented && state.decodedPts > 0 && state.decodedPts < 0.1);
    }
    tc::fs::remove(unindexed);
    tc::fs::remove(path);
    std::printf("videoNewestDueFrame: %d failures\n", failures);
    return failures ? 1 : 0;
#else
    (void)argc;
    (void)argv;
    return 0;
#endif
}
