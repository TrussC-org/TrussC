#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#if defined(__linux__) && !defined(__ANDROID__)
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}
#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <vector>
#include <unistd.h>
#endif

namespace {
#if defined(__linux__) && !defined(__ANDROID__)
constexpr int kRate = 48000;
constexpr int kFrames = 4800;
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
float sample(int frame) { return 0.25f + 0.25f * frame / kFrames; }

// Runtime Matroska fixture: PCM needs no encoder. Each packet has an exact
// millisecond timestamp; the ramp exposes missing/duplicated boundary samples.
bool fixture(const tc::fs::path& path, bool audio) {
    AVFormatContext* f = nullptr;
    if (avformat_alloc_output_context2(&f, nullptr, "matroska", path.c_str()) < 0) return false;
    AVStream* st = avformat_new_stream(f, nullptr);
    st->time_base = AVRational{1, kRate};
    st->codecpar->codec_type = audio ? AVMEDIA_TYPE_AUDIO : AVMEDIA_TYPE_VIDEO;
    st->codecpar->codec_id = audio ? AV_CODEC_ID_PCM_F32LE : AV_CODEC_ID_FFV1;
    if (audio) {
        st->codecpar->sample_rate = kRate;
        av_channel_layout_default(&st->codecpar->ch_layout, 2);
        st->codecpar->bits_per_coded_sample = 32;
        st->codecpar->block_align = 8;
    } else {
        st->codecpar->width = st->codecpar->height = 16;
    }
    bool ok = avio_open(&f->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0;
    if (ok) ok = avformat_write_header(f, nullptr) >= 0;
    AVPacket* pkt = av_packet_alloc();
    if (ok && audio) {
        for (int first = 0; first < kFrames && ok; first += 480) {
            av_new_packet(pkt, 480 * 2 * sizeof(float));
            float* p = reinterpret_cast<float*>(pkt->data);
            for (int i = 0; i < 480; ++i) p[2*i] = p[2*i+1] = sample(first+i);
            pkt->stream_index = st->index;
            pkt->pts = pkt->dts = av_rescale_q(first, AVRational{1, kRate}, st->time_base);
            pkt->duration = av_rescale_q(480, AVRational{1, kRate}, st->time_base);
            ok = av_interleaved_write_frame(f, pkt) >= 0;
            av_packet_unref(pkt);
        }
    } else if (ok) {
        av_new_packet(pkt, 1);
        pkt->data[0] = 0;
        pkt->pts = pkt->dts = 0;
        pkt->duration = 100;
        ok = av_interleaved_write_frame(f, pkt) >= 0;
    }
    if (ok) ok = av_write_trailer(f) >= 0;
    av_packet_free(&pkt);
    if (f->pb) avio_closep(&f->pb);
    avformat_free_context(f);
    return ok;
}
// AAC/MP4 exercises codec priming and tail padding, which PCM lacks.
bool aacFixture(const tc::fs::path& path) {
    AVFormatContext* f = nullptr;
    if (avformat_alloc_output_context2(&f, nullptr, "mp4", path.c_str()) < 0) return false;
    const AVCodec* encoder = avcodec_find_encoder(AV_CODEC_ID_AAC);
    AVCodecContext* c = encoder ? avcodec_alloc_context3(encoder) : nullptr;
    if (!c) { avformat_free_context(f); return false; }
    c->sample_rate = kRate;
    c->sample_fmt = AV_SAMPLE_FMT_FLTP;
    c->time_base = AVRational{1, kRate};
    c->bit_rate = 128000;
    av_channel_layout_default(&c->ch_layout, 2);
    c->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    // Disable AAC's stochastic noise substitution so decoder output can be
    // compared exactly across seeks; the loop test measures gaps and padding.
    av_opt_set(c->priv_data, "aac_pns", "0", 0);
    bool ok = avcodec_open2(c, encoder, nullptr) >= 0;
    AVStream* st = avformat_new_stream(f, nullptr);
    st->time_base = c->time_base;
    ok = ok && avcodec_parameters_from_context(st->codecpar, c) >= 0;
    ok = ok && avio_open(&f->pb, path.c_str(), AVIO_FLAG_WRITE) >= 0;
    ok = ok && avformat_write_header(f, nullptr) >= 0;
    AVFrame* frame = av_frame_alloc();
    AVPacket* packet = av_packet_alloc();
    frame->format = c->sample_fmt;
    frame->sample_rate = kRate;
    av_channel_layout_copy(&frame->ch_layout, &c->ch_layout);
    frame->nb_samples = c->frame_size;
    ok = ok && av_frame_get_buffer(frame, 0) >= 0;
    auto drain = [&] {
        while (avcodec_receive_packet(c, packet) == 0) {
            av_packet_rescale_ts(packet, c->time_base, st->time_base);
            packet->stream_index = st->index;
            if (av_interleaved_write_frame(f, packet) < 0) ok = false;
            av_packet_unref(packet);
        }
    };
    for (int first = 0; first < kFrames && ok; first += c->frame_size) {
        av_frame_make_writable(frame);
        frame->nb_samples = std::min(c->frame_size, kFrames-first);
        frame->pts = first;
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < frame->nb_samples; ++i)
                reinterpret_cast<float*>(frame->data[ch])[i] = sample(first+i);
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

TC_CORE_TEST_MAIN() {
#if defined(__linux__) && !defined(__ANDROID__)
    using namespace tc;
    internal::setNullAudioBackendForTests(true);
    auto& engine = AudioEngine::getInstance();
    AudioSettings settings;
    settings.sampleRate = kRate;
    settings.bufferSize = 256;
    check("null audio backend", engine.init(settings));
    fs::path dir = fs::temp_directory_path() / ("tc-video-audio-" + std::to_string(getpid()));
    fs::create_directories(dir);
    auto path = dir / "ramp.mkv";
    check("PCM Matroska fixture", fixture(path, true));
    check("video-only fixture", fixture(dir / "silent.mkv", false));
    Sound silent;
    check("no audio stream fails", !internal::loadFFmpegAudioStream(dir / "silent.mkv", silent));
    Sound sound;
    check("FFmpeg stream opens", (bool)internal::loadFFmpegAudioStream(path, sound));
    Sound protocol;
    check("internal open bypasses filesystem gate", (bool)internal::loadFFmpegAudioStream(
        fs::path("file:" + path.string()), protocol));
    check("duration from container", std::abs(sound.getDuration() - 0.1f) < 0.00001f);
    VideoPlayer player;
    check("streaming preference defaults true", player.isAudioStreaming());
    player.setAudioStreaming(false);
    VideoPlayer moved(std::move(player));
    check("preload preference survives move", !moved.isAudioStreaming());

    std::mutex mutex;
    std::condition_variable cv;
    std::vector<float> heard;
    bool started = false;
    auto listener = engine.audioOut.listen([&](AudioOutBuffer& buffer) {
        std::lock_guard<std::mutex> lock(mutex);
        for (int i = 0; i < buffer.frameCount && heard.size() < kFrames * 4; ++i) {
            float value = buffer.data[i * buffer.channels];
            if (value > 0.1f) started = true;
            if (started) heard.push_back(value);
        }
        cv.notify_one();
    });
    sound.setLoop(true);
    check("stream plays", sound.play());
    bool listed = false;
    for (const auto& voice : engine.getPlayingSounds()) listed |= voice.streaming;
    check("voice listed as streaming", listed);
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool complete = cv.wait_for(lock, std::chrono::seconds(5), [&] { return heard.size() >= kFrames * 3; });
        bool exact = complete;
        for (size_t i = 0; i < heard.size() && exact; ++i)
            exact = std::abs(heard[i] - sample(i % kFrames)) < 0.00001f;
        check("three loops preserve every sample without silence", exact);
    }
    sound.pause();
    sound.setPosition(0.05f);
    check("explicit seek reports target", std::abs(sound.getPosition() - 0.05f) < 0.00001f);
    {
        std::lock_guard<std::mutex> lock(mutex);
        heard.clear();
        started = false;
    }
    sound.resume();
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool complete = cv.wait_for(lock, std::chrono::seconds(5), [&] { return heard.size() >= kFrames; });
        // Locate the sought sample and verify the whole tail and next loop.
        // A seek may refill with silence; only the post-seek PCM is compared.
        bool found = false;
        for (size_t start = 0; start + kFrames/2 + 100 < heard.size() && !found; ++start) {
            if (std::abs(heard[start] - sample(kFrames/2)) > 0.00001f) continue;
            found = true;
            for (size_t i = 0; i < kFrames/2 + 100 && found; ++i)
                found = std::abs(heard[start+i] - sample((kFrames/2+i) % kFrames)) < 0.00001f;
        }
        check("decoder seek plays correct tail and wraps", complete && found);
    }
    sound.pause();
    float before = sound.getPosition();
    settings.sampleRate = 96000;
    check("custom decoder reopens at engine re-init", engine.init(settings));
    check("re-init retains stream position", std::abs(sound.getPosition() - before) < 0.0001f);
    sound.stop();
    // Restore the original rate: compare complete AAC loops sample by sample,
    // including encoder delay and padding, independently of callback timing.
    settings.sampleRate = kRate;
    check("restore 48 kHz", engine.init(settings));
    check("AAC fixture", aacFixture(dir / "ramp.m4a"));
    Sound aac;
    check("AAC stream opens", (bool)internal::loadFFmpegAudioStream(dir / "ramp.m4a", aac));
    check("AAC duration excludes encoder padding", std::abs(aac.getDuration() - 0.1f) < 0.00001f);
    {
        std::lock_guard<std::mutex> lock(mutex);
        heard.clear();
        started = false;
    }
    aac.setLoop(true);
    check("AAC plays", aac.play());
    {
        std::unique_lock<std::mutex> lock(mutex);
        bool complete = cv.wait_for(lock, std::chrono::seconds(5), [&] { return heard.size() >= kFrames * 3; });
        bool exact = complete;
        for (size_t i = kFrames; i < heard.size() && exact; ++i) {
            exact = std::abs(heard[i] - heard[i % kFrames]) < 0.00001f;
            if (!exact) std::printf("AAC mismatch at %zu: %.8f vs %.8f\n", i,
                heard[i], heard[i % kFrames]);
        }
        check("AAC loops repeat every sample without padding or gaps", exact);
    }
    aac.stop();
    listener.disconnect();
    engine.shutdown();
    std::error_code ec;
    fs::remove_all(dir, ec);
    return failures ? 1 : 0;
#else
    std::puts("SKIP: FFmpeg video audio streaming is Linux-only");
    return 0;
#endif
}
} // namespace
