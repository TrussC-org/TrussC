#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <chrono>
#include <thread>
#if defined(__linux__) && !defined(__ANDROID__)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#include <unistd.h>
namespace trussc::internal {
    double videoPauseClockDriftForTests(VideoPlayer& player, double pausedSeconds);
    double videoPausedSeekClockDriftForTests(VideoPlayer& player, int frame);
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
bool videoFixture(const tc::fs::path& path, const char* container = "avi", bool longGop = false, bool audio = false, int audioSamples = 1600) {
    AVFormatContext* f = nullptr;
    if (avformat_alloc_output_context2(&f, nullptr, container, path.c_str()) < 0) return false;
    const AVCodec* encoder = longGop ? avcodec_find_encoder_by_name("libx264") : avcodec_find_encoder(AV_CODEC_ID_FFV1);
    AVCodecContext* c = encoder ? avcodec_alloc_context3(encoder) : nullptr;
    if (!c) { avformat_free_context(f); return false; }
    c->width = c->height = 16;
    c->pix_fmt = AV_PIX_FMT_YUV420P;
    c->time_base = AVRational{1, 30};
    c->thread_count = 1;
    c->gop_size = longGop ? 150 : 6;
    c->max_b_frames = longGop ? 2 : 0;
    if (longGop) av_opt_set(c->priv_data, "x264-params", "scenecut=0:keyint=150:min-keyint=150", 0);
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    bool ok = avcodec_open2(c, encoder, nullptr) >= 0;
    AVStream* video = avformat_new_stream(f, nullptr);
    video->time_base = c->time_base;
    video->avg_frame_rate = AVRational{30, 1};
    ok = ok && avcodec_parameters_from_context(video->codecpar, c) >= 0;
    AVStream* sound = nullptr;
    if (audio) {
        sound = avformat_new_stream(f, nullptr);
        sound->time_base = AVRational{1, 48000};
        sound->codecpar->codec_type = AVMEDIA_TYPE_AUDIO;
        sound->codecpar->codec_id = AV_CODEC_ID_PCM_S16LE;
        sound->codecpar->sample_rate = 48000;
        sound->codecpar->bits_per_coded_sample = 16;
        sound->codecpar->block_align = 2;
        av_channel_layout_default(&sound->codecpar->ch_layout, 1);
    }
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
    for (int i = 0; i < (longGop ? 300 : 900) && ok; ++i) {
        av_frame_make_writable(frame);
        for (int plane = 0; plane < 3; ++plane)
            for (int y = 0; y < (plane ? 8 : 16); ++y)
                std::memset(frame->data[plane] + y * frame->linesize[plane],
                            plane ? 128 : 32 + i % 180, plane ? 8 : 16);
        frame->pts = i;
        ok = avcodec_send_frame(c, frame) >= 0;
        drain();
        if (sound) {
            av_new_packet(packet, audioSamples * 2);
            std::memset(packet->data, 0, packet->size);
            packet->flags |= AV_PKT_FLAG_KEY;
            packet->stream_index = sound->index;
            packet->pts = packet->dts = av_rescale_q(i * audioSamples, AVRational{1, 48000}, sound->time_base);
            packet->duration = av_rescale_q(audioSamples, AVRational{1, 48000}, sound->time_base);
            ok = ok && av_interleaved_write_frame(f, packet) >= 0;
            av_packet_unref(packet);
        }
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


using Clock = std::chrono::steady_clock;
bool atFrame(const tc::VideoPlayer& player, int frame) {
    return std::abs(player.getCurrentTime() - frame / 30.0) < 0.001;
}
template<class Predicate>
bool waitFor(tc::VideoPlayer& player, Predicate ready) {
    const auto deadline = Clock::now() + std::chrono::seconds(5);
    do {
        Access::updateHeadlessForTests(player);
        if (ready()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (Clock::now() < deadline);
    return false;
}
void workerSeekChecks(const tc::fs::path& path, bool audio = false) {
    for (float speed : {0.5f, 1.0f, 2.0f}) {
        tc::VideoPlayer player;
        player.setDecodeThreads(1);
        player.setAudioStreaming(speed != 2.0f);
        check("long GOP worker load", Access::loadHeadlessForTests(player, path));
        player.setSpeed(speed);
        Access::playHeadlessForTests(player);
        player.setPaused(true);
        player.setFrame(30);
        check("worker initial paused seek", waitFor(player, [&] { return atFrame(player, 30); }));
        for (int target : {106, 46}) {
            player.setPaused(true);
            const auto pixel = player.getPixels().getData()[0];
            const auto shown = player.getCurrentTime();
            if (target == 106) player.setPosition(target / 300.0f);
            else player.setFrame(target);
            check("seek getters retain displayed PTS until update", player.getCurrentTime() == shown);
            bool preroll = false;
            check("worker presents paused seek target", waitFor(player, [&] {
                const auto pts = player.getCurrentTime();
                preroll |= pts != shown && !atFrame(player, target);
                return atFrame(player, target);
            }));
            check("paused seek changes pixels without preroll", !preroll && player.getPixels().getData()[0] != pixel);
            for (int i = 0; i < 20; ++i) {
                Access::updateHeadlessForTests(player);
                check("seek stays paused at target", player.isPaused() && atFrame(player, target));
            }
            if (audio) {
                const auto voices = tc::AudioEngine::getInstance().getPlayingSounds();
                check("paused seek retains paused audio voice", voices.size() == 1 && voices[0].paused);
                if (voices.size() == 1 && !voices[0].streaming)
                    check("paused preload audio seeks with video", std::abs(voices[0].position - target / 30.0) < 0.001);
            }
            player.nextFrame();
            check("worker nextFrame while paused", waitFor(player, [&] { return atFrame(player, target + 1); }));
            player.previousFrame();
            check("worker previousFrame while paused", waitFor(player, [&] { return atFrame(player, target); }));
            player.setPaused(false);
            bool rewound = false;
            check("resume advances from target", waitFor(player, [&] {
                rewound |= player.getCurrentTime() + 0.001 < target / 30.0;
                return player.getCurrentTime() >= (target + 3) / 30.0;
            }));
            check("resume never presents preroll", !rewound);
            if (audio) {
                const auto voices = tc::AudioEngine::getInstance().getPlayingSounds();
                check("resumed video follows audio clock", voices.size() == 1 && !voices[0].paused &&
                      std::abs(voices[0].position - player.getCurrentTime()) <= 2.0 / 30);
            }
        }
        // The same seek operation must suppress preroll while playing.
        player.setFrame(20);
        bool preroll = false;
        check("playing backward seek reaches target", waitFor(player, [&] {
            preroll |= player.getCurrentTime() + 0.001 < 20 / 30.0;
            return atFrame(player, 20);
        }));
        check("playing backward seek never shows preroll", !preroll);
        player.setPaused(true);
        player.setPosition(1.0f);
        check("paused end seek presents final frame", waitFor(player, [&] { return atFrame(player, 299); }));
    }
}
tc::fs::path displayClip;
bool displayFinished = false;
class PausedSeekDisplay : public tc::App {
    tc::VideoPlayer player;
    tc::Fbo fbo;
    Clock::time_point deadline;
    int phase = 0;
    int firstPixel = 0;
public:
    void setup() override {
        player.setUseHwAccel(false);
        player.setDecodeThreads(1);
        if (!player.load(displayClip)) { check("display load", false); tc::exitApp(1); return; }
        fbo.allocate(16, 16);
        player.play();
        player.setPaused(true);
        player.setFrame(30);
        deadline = Clock::now() + std::chrono::seconds(5);
    }
    void update() override {
        player.update();
        if (Clock::now() > deadline) { check("paused seek display deadline", false); tc::exitApp(1); return; }
        const int target = phase == 0 ? 30 : 106;
        if (!atFrame(player, target)) return;
        check("paused seek reports isFrameNew", player.isFrameNew() && player.isPaused());
        fbo.begin();
        tc::clear(0);
        tc::setColor(1);
        player.draw(0, 0, 16, 16);
        fbo.end();
        tc::Pixels pixels;
        check("read uploaded paused seek texture", fbo.readPixels(pixels));
        if (!pixels.isAllocated()) { tc::exitApp(1); return; }
        const int pixel = pixels.getData()[0];
        check("GPU texture matches decoded seek pixels", std::abs(pixel - int(player.getPixels().getData()[0])) <= 2);
        if (phase++ == 0) {
            firstPixel = pixel;
            player.setFrame(106);
            deadline = Clock::now() + std::chrono::seconds(5);
        } else {
            check("texture changes while paused", pixel != firstPixel);
            player.update();
            check("paused seek reports new only once", !player.isFrameNew());
            displayFinished = true;
            tc::exitApp();
        }
    }
    void cleanup() override {
        player.close();
        tc::fs::remove(displayClip);
    }
};

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
    const auto longGop = tc::fs::temp_directory_path() /
        ("tc-paused-seek-" + std::to_string(getpid()) + ".mp4");
    if (!videoFixture(longGop, "mp4", true)) return 1;
    if (argc == 2 && std::string(argv[1]) == "--display") {
        displayClip = longGop;
        tc::WindowSettings settings;
        settings.setSize(160, 120);
        return tcCoreTest::runDisplayTest<PausedSeekDisplay>(settings, failures, [] {
            check("display seek completed", displayFinished);
        });
    }
    workerSeekChecks(longGop);
    for (float speed : {0.5f, 1.0f, 2.0f}) {
        tc::VideoPlayer player;
        player.setDecodeThreads(1);
        check("controlled long GOP load", Access::loadHeadlessForTests(player, longGop));
        player.setSpeed(speed);
        for (int frame : {106, 46}) {
            check("seek resume rebases wall and worker clocks at selected speed",
                  tc::internal::videoPausedSeekClockDriftForTests(player, frame) < 0.0001);
            const auto state = Access::stepPlaybackForTests(player, frame / 30.0, 1);
            check("long GOP seek queues target without a preroll burst",
                  state.presented && nearlyEqual(state.presentedPts, frame / 30.0) && state.queued == 0);
        }
    }
    tc::fs::remove(longGop);
    auto& engine = tc::AudioEngine::getInstance();
    tc::AudioSettings audioSettings;
    audioSettings.backend = tc::AudioBackend::Null;
    check("null audio backend", engine.init(audioSettings));
    const auto withAudio = longGop.string() + ".mkv";
    check("long GOP PCM fixture", videoFixture(withAudio, "matroska", true, true));
    workerSeekChecks(withAudio, true);
    tc::fs::remove(withAudio);
    check("audio tail fixture", videoFixture(withAudio, "matroska", true, true, 1605));
    {
        tc::VideoPlayer player;
        player.setDecodeThreads(1);
        check("audio tail load", Access::loadHeadlessForTests(player, withAudio));
        check("container includes audio tail", player.getDuration() > 10.02f);
        Access::playHeadlessForTests(player);
        player.setPaused(true);
        player.setFrame(30);
        check("audio tail initial paused seek", waitFor(player, [&] { return atFrame(player, 30); }));
        player.setPosition(1.0f);
        check("audio tail endpoint presents final picture", waitFor(player, [&] { return atFrame(player, 299); }));
        for (int i = 0; i < 20; ++i) {
            Access::updateHeadlessForTests(player);
            check("audio tail endpoint stays paused", player.isPaused() && atFrame(player, 299));
        }
    }
    tc::fs::remove(withAudio);
    engine.close();
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
        check("manual backward seek presents target without preroll",
              state.seeks == 2 && state.presented && nearlyEqual(state.presentedPts, 0.3) && state.presentedPts < shown);
        player.setFrame(15);
        state = Access::stepPlaybackForTests(player, 0.5, 1);
        check("setFrame presents requested frame", state.presented && nearlyEqual(state.presentedPts, 0.5));
        player.nextFrame();
        state = Access::stepPlaybackForTests(player, 0.535, 1);
        check("nextFrame presents next frame", state.presented && nearlyEqual(state.presentedPts, 16.0/30));
        player.previousFrame();
        state = Access::stepPlaybackForTests(player, 0.501, 1);
        check("previousFrame presents previous frame", state.presented && nearlyEqual(state.presentedPts, 0.5));
        player.setLoop(true);
        player.setPosition(0.99f);
        Access::stepPlaybackForTests(player, 29.999, 1);
        for (int i = 0; i < 20 && !state.seekPending; ++i)
            state = Access::stepPlaybackForTests(player, 29.999, 1);
        check("EOF loop requests beginning", state.seekPending && state.seekTarget == 0);
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
        check("seek before play preserves clock and decodes actual requested image",
              nearlyEqual(state.clockPts, 15.0) && state.presented && nearlyEqual(state.presentedPts, 15.0));
        state = Access::stepPlaybackForTests(player, 15.0, 0, true);
        check("play after completed seek keeps requested clock",
              nearlyEqual(state.clockPts, 15.0) && nearlyEqual(state.presentedPts, 15.0));
        player.setPosition(0.99f);
        state = Access::stepPlaybackForTests(player, 30.0, 1);
        for (int i = 0; i < 20 && !state.finished; ++i)
            state = Access::stepPlaybackForTests(player, 30.0, 1);
        check("non-looping playback finishes", state.finished);
        const double lastShown = state.presentedPts;
        state = Access::stepPlaybackForTests(player, 0.0, 0, true);
        check("replay restarts clock and seek while retaining actual shown PTS",
              state.clockPts == 0 && state.seekPending && state.seekTarget == 0 && state.presentedPts == lastShown);
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
