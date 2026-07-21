#pragma once
#include "tc/utils/tcAnnotations.h"

// =============================================================================
// tcVideoRecorder.h - Native video recording (H.264 / HEVC / ProRes -> file)
// =============================================================================
//
// Two layers, each platform-native (no ffmpeg, no extra dependency):
//
//   VideoWriter     - low-level encoder. You feed it frames; it writes a file.
//                       open() -> addFrame()/addFrameAt() ... -> close()
//                     Deterministic, caller-driven (fixed-rate timestamps).
//                     This is what the per-platform backends implement.
//
//   ScreenRecorder  - high-level live capture. Records the window (or an Fbo)
//                       every frame at wall-clock speed until stop().
//                       start(path) / start(fbo, path) -> stop()
//                     Size is taken automatically; built on a VideoWriter.
//                     A fixed length may be requested (settings.duration, or the
//                       start(path, seconds) overload): recording then auto-stops
//                       and finalizes the file at exactly that length — no frame
//                       whose PTS reaches `duration` is written. Default is 0 =
//                       unlimited (record until stop()).
//
//   startRecording()/stopRecording() - global convenience over a singleton
//                     ScreenRecorder ("just record the whole window").
//
// Per-platform encoders:
//   - macOS:   AVFoundation (AVAssetWriter)        H.264 / HEVC / ProRes
//   - Windows: Media Foundation (IMFSinkWriter)    H.264 (+ HW->SW fallback)
//   - Linux:   GStreamer (appsrc -> enc -> mux)    H.264 / HEVC (HW-probed)
//
// This file is included from TrussC.h AFTER Fbo, Pixels, grabScreen, events()
// and sapp_width(), whose definitions the inline helpers below rely on.
// =============================================================================

#include <string>
#include <vector>
#include <filesystem>
#include <functional>
#include <atomic>
#include <mutex>
#include <thread>
#include <chrono>
#include <cstring>

// Audio-track recording taps the AudioEngine's master mix (sound never
// includes video, so this dependency is one-way).
#include "../sound/tcSound.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

// macOS-only: ScreenRecorder captures the swapchain asynchronously to avoid a
// per-frame GPU stall. Elsewhere it falls back to the synchronous grabScreen().
#if defined(__APPLE__) && (!defined(TARGET_OS_IPHONE) || !TARGET_OS_IPHONE)
    #define TC_ASYNC_SCREEN_CAPTURE 1
#else
    #define TC_ASYNC_SCREEN_CAPTURE 0
#endif

namespace trussc {

class Fbo;
class Pixels;

// Opaque per-platform encoder state (AVAssetWriter / IMFSinkWriter / GstPipeline)
namespace internal { struct VideoWriterPlatformData; }

// ---------------------------------------------------------------------------
// Codec / settings
// ---------------------------------------------------------------------------
enum class VideoCodec {
    H264,        // .mp4, universal — the default
    HEVC,        // .mp4, H.265
    ProRes422,   // .mov, near-lossless mastering (macOS only)
    ProRes4444,  // .mov, ProRes with alpha (macOS only)
};

inline const char* videoCodecName(VideoCodec c) {
    switch (c) {
        case VideoCodec::H264:       return "H.264";
        case VideoCodec::HEVC:       return "HEVC";
        case VideoCodec::ProRes422:  return "ProRes 422";
        case VideoCodec::ProRes4444: return "ProRes 4444";
    }
    return "?";
}

struct VideoRecordSettings {
    VideoCodec codec = VideoCodec::H264;
    float fps = 60.0f;          // ScreenRecorder: capture ceiling (real-time PTS,
                                // so a slower app just yields fewer frames at 1x).
                                // VideoWriter: the exact output frame rate.
    int bitrate = 0;            // bits/sec for H.264/HEVC; 0 = auto. Ignored by ProRes.
    int keyframeInterval = 0;   // frames between keyframes; 0 = encoder default.
    float duration = 0.0f;      // ScreenRecorder: auto-stop & finalize after this
                                // many seconds of output; 0 = unlimited (call
                                // stop() manually). Ignored by VideoWriter.

    // Record the engine's master mix into the file as an AAC track (macOS and
    // Windows; other platforms warn and record video-only). ScreenRecorder also
    // switches the video PTS clock from wall time to the AUDIO DEVICE clock, so
    // A/V stay in sync no matter how unstable the frame rate is.
    bool audio = false;
    int  audioBitrate = 192000; // AAC bits/sec
    // Sample rate / channel count of the audio track. ScreenRecorder fills
    // these from the running AudioEngine; set them manually only when feeding
    // writeAudio() yourself on a bare VideoWriter.
    int  audioSampleRate = 0;
    int  audioChannels = 0;
};

// ---------------------------------------------------------------------------
// VideoWriter - low-level encoder you feed frames to.
// ---------------------------------------------------------------------------
class TC_PLATFORMS("macos,windows,linux,android,ios") VideoWriter {
public:
    VideoWriter() = default;
    ~VideoWriter() { close(); }
    VideoWriter(const VideoWriter&) = delete;
    VideoWriter& operator=(const VideoWriter&) = delete;

    // Open the encoder. `path` is resolved via getDataPath() when relative; the
    // parent directory is created if missing. Returns false if the encoder (or
    // the requested codec on this platform) could not be created.
    bool open(const fs::path& path, int width, int height,
              const VideoRecordSettings& settings = {}) {
        close();
        if (width <= 0 || height <= 0) {
            logError("VideoWriter") << "invalid size " << width << "x" << height;
            return false;
        }
        if ((width & 1) || (height & 1)) {
            logWarning("VideoWriter")
                << "size " << width << "x" << height
                << " is odd; most codecs need even dimensions";
        }
        path_ = getDataPath(path);
        {   // native encoders won't create intermediate directories
            std::error_code ec;
            fs::path parent = path_.parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent, ec);
        }
        width_ = width;
        height_ = height;
        settings_ = settings;
        fps_ = (settings_.fps > 0.0f) ? settings_.fps : 60.0f;
        frameCount_ = 0;
        scratch_.resize((size_t)width_ * height_ * 4);

        // Platform encoders take UTF-8 (converted to native inside)
        if (!openPlatform(internal::pathToUtf8(path_), width_, height_, fps_, settings_)) {
            logError("VideoWriter")
                << "failed to open " << videoCodecName(settings_.codec)
                << " encoder for " << path_;
            return false;
        }
        open_ = true;
        logNotice("VideoWriter")
            << "writing " << width_ << "x" << height_ << " @ " << fps_ << "fps "
            << videoCodecName(settings_.codec) << " -> " << path_;
        return true;
    }

    // Finalize and flush the file. Safe to call multiple times.
    void close() {
        if (open_) {
            closePlatform();
            logNotice("VideoWriter")
                << "saved " << frameCount_ << " frames -> " << path_;
        }
        open_ = false;
    }

    bool isOpen() const { return open_; }
    int  getFrameCount() const { return frameCount_; }
    int  getWidth() const { return width_; }
    int  getHeight() const { return height_; }
    float getFps() const { return fps_; }
    fs::path getPath() const { return path_; }
    const VideoRecordSettings& getSettings() const { return settings_; }

    // Append one frame at the fixed-rate clock (frameIndex / fps) — deterministic
    // offline render. Source size must match the writer's size.
    bool addFrame(const Fbo& fbo)      { return addFrameAt(fbo, frameCount_ / (double)fps_); }
    bool addFrame(const Pixels& pixels){ return addFrameAt(pixels, frameCount_ / (double)fps_); }

    // Append one frame at an explicit presentation time (seconds). Used by
    // ScreenRecorder for wall-clock live capture, or for custom timing.
    bool addFrameAt(const Fbo& fbo, double timeSec) {
        if (!open_) return false;
        if (fbo.getWidth() != width_ || fbo.getHeight() != height_) {
            logError("VideoWriter")
                << "fbo size " << fbo.getWidth() << "x" << fbo.getHeight()
                << " != writer " << width_ << "x" << height_;
            return false;
        }
        if (!fbo.readPixels(scratch_.data())) {
            logError("VideoWriter") << "fbo readPixels failed";
            return false;
        }
        return appendRGBA(scratch_.data(), timeSec);
    }
    bool addFrameAt(const Pixels& pixels, double timeSec) {
        if (!open_) return false;
        if (pixels.getWidth() != width_ || pixels.getHeight() != height_) {
            logError("VideoWriter")
                << "pixels size " << pixels.getWidth() << "x" << pixels.getHeight()
                << " != writer " << width_ << "x" << height_;
            return false;
        }
        if (pixels.getChannels() == 4) return appendRGBA(pixels.getData(), timeSec);
        // Expand to RGBA into the scratch buffer.
        const unsigned char* src = pixels.getData();
        const int ch = pixels.getChannels();
        const size_t n = (size_t)width_ * height_;
        for (size_t i = 0; i < n; ++i) {
            unsigned char r = src[i * ch + 0];
            scratch_[i * 4 + 0] = r;
            scratch_[i * 4 + 1] = (ch > 1) ? src[i * ch + 1] : r;
            scratch_[i * 4 + 2] = (ch > 2) ? src[i * ch + 2] : r;
            scratch_[i * 4 + 3] = 255;
        }
        return appendRGBA(scratch_.data(), timeSec);
    }

    // Append a tightly-packed RGBA8 buffer directly (no Pixels wrapper). Used by
    // ScreenRecorder's async capture, where pixels arrive in a raw GPU readback
    // buffer on a background thread.
    bool addFrameAt(const unsigned char* rgba, int w, int h, double timeSec) {
        if (!open_) return false;
        if (w != width_ || h != height_) {
            logError("VideoWriter")
                << "frame size " << w << "x" << h
                << " != writer " << width_ << "x" << height_;
            return false;
        }
        return appendRGBA(rgba, timeSec);
    }

#if TC_ASYNC_SCREEN_CAPTURE
    // Zero-intermediate path (macOS): lock the encoder's next pixel buffer for
    // direct readback (returns its base address + row stride), then submit it at
    // a PTS. The screen recorder reads the GPU capture straight into this buffer
    // instead of into a temporary Pixels/RGBA buffer the encoder then re-copies.
    TC_PLATFORMS("macos") unsigned char* lockFrame(int& strideOut) {
        if (!open_) return nullptr;
        return lockFramePlatform(strideOut);
    }
    TC_PLATFORMS("macos") bool submitFrame(double timeSec) {
        if (!open_) return false;
        if (!submitFramePlatform(timeSec)) {
            logError("VideoWriter") << "encoder rejected frame " << frameCount_;
            return false;
        }
        ++frameCount_;
        return true;
    }
#endif

    // Append interleaved float samples to the audio track at an explicit PTS
    // (seconds, same timeline as addFrameAt). Only meaningful when the writer
    // was opened with settings.audio = true (and audioSampleRate/Channels set);
    // no-op false otherwise. Channel count / rate must match the settings.
    bool writeAudio(const float* interleaved, int frames, double timeSec) {
        if (!open_ || !interleaved || frames <= 0) return false;
        if (!settings_.audio) return false;
        return appendAudioPlatform(interleaved, frames, timeSec);
    }

private:
    bool appendRGBA(const unsigned char* rgba, double timeSec) {
        if (!open_ || !rgba) return false;
        if (!appendPlatform(rgba, timeSec)) {
            logError("VideoWriter") << "encoder rejected frame " << frameCount_;
            return false;
        }
        ++frameCount_;
        return true;
    }

    // Platform hooks (defined in platform/<os>/tcVideoRecorder_<os>.*)
    bool openPlatform(const std::string& fullPath, int w, int h, float fps,
                      const VideoRecordSettings& settings);
    bool appendPlatform(const unsigned char* rgba, double timeSec);
    bool appendAudioPlatform(const float* interleaved, int frames, double timeSec);
    void closePlatform();
#if TC_ASYNC_SCREEN_CAPTURE
    // Direct-buffer hooks for the zero-intermediate capture path (macOS).
    unsigned char* lockFramePlatform(int& strideOut);   // lock & return encoder buffer
    bool submitFramePlatform(double timeSec);            // append the locked buffer
#endif

    internal::VideoWriterPlatformData* platform_ = nullptr;
    fs::path path_;
    std::vector<unsigned char> scratch_;
    int   width_  = 0;
    int   height_ = 0;
    float fps_    = 30.0f;
    VideoRecordSettings settings_;
    int   frameCount_ = 0;
    bool  open_ = false;
};

// ---------------------------------------------------------------------------
// ScreenRecorder - live capture of the window (or an Fbo) at wall-clock speed.
// ---------------------------------------------------------------------------
class TC_PLATFORMS("macos,windows,linux,android,ios") ScreenRecorder {
public:
    ScreenRecorder() = default;
    ~ScreenRecorder() { stop(); }
    ScreenRecorder(const ScreenRecorder&) = delete;
    ScreenRecorder& operator=(const ScreenRecorder&) = delete;

    // Record the whole window (swapchain) — the same fully-composited image
    // tc_get_screenshot returns. Size is taken from the framebuffer.
    bool start(const fs::path& path, const VideoRecordSettings& settings = {}) {
        return startSource(Source::Swapchain, nullptr,
                           sapp_width(), sapp_height(), path, settings);
    }

    // Record an offscreen Fbo every frame (clean output, GUI-free).
    bool start(const Fbo& fbo, const fs::path& path,
               const VideoRecordSettings& settings = {}) {
        return startSource(Source::Fbo, &fbo,
                           fbo.getWidth(), fbo.getHeight(), path, settings);
    }

    // Fixed-duration convenience: record for `durationSec` seconds, then auto-stop
    // and finalize the file at exactly that length. Same as filling
    // settings.duration. (durationSec <= 0 records until stop(), like the default.)
    bool start(const fs::path& path, float durationSec) {
        VideoRecordSettings s;
        s.duration = durationSec;
        return start(path, s);
    }
    bool start(const Fbo& fbo, const fs::path& path, float durationSec) {
        VideoRecordSettings s;
        s.duration = durationSec;
        return start(fbo, path, s);
    }

    // Stop capturing and finalize the file. Safe to call multiple times.
    void stop() {
        afterFrameListener_ = EventListener{};   // no new captures get queued
        exitListener_ = EventListener{};
        audioListener_ = EventListener{};        // audio thread stops feeding
#if TC_ASYNC_SCREEN_CAPTURE
        // Drain async captures still encoding on background threads before we
        // close the writer (their completion handlers append into it).
        for (int spins = 0;
             inFlight_.load(std::memory_order_acquire) > 0 && spins < 2000;
             ++spins) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
        source_ = Source::None;
        fbo_ = nullptr;
        {
            std::lock_guard<std::mutex> lk(writerMutex_);
            if (audioActive_) drainAudio();      // flush the tail of the ring
            audioActive_ = false;
            writer_.close();
        }
    }

    bool isRecording() const { return writer_.isOpen(); }
    int  getFrameCount() const { return writer_.getFrameCount(); }
    fs::path getPath() const { return writer_.getPath(); }
    VideoWriter& writer() { return writer_; }   // for advanced introspection

private:
    enum class Source { None, Swapchain, Fbo };

    bool startSource(Source src, const Fbo* fbo, int w, int h,
                     const fs::path& path,
                     const VideoRecordSettings& settings) {
        stop();
        VideoRecordSettings s = settings;
        if (s.audio) {
            auto& engine = AudioEngine::getInstance();
            if (!engine.isInitialized()) {
                logWarning("ScreenRecorder")
                    << "audio requested but AudioEngine is not initialized - "
                       "recording video only";
                s.audio = false;
            } else {
                if (s.audioSampleRate <= 0) s.audioSampleRate = engine.getSampleRate();
                if (s.audioChannels   <= 0) s.audioChannels   = engine.getChannels();
            }
        }
        if (!writer_.open(path, w, h, s)) return false;
        source_ = src;
        fbo_ = fbo;
        fboAlive_ = fbo ? fbo->lifetimeToken() : std::shared_ptr<void>{};
        duration_ = s.duration;
        startElapsed_ = getElapsedTimef();
        nextCaptureTime_ = -1.0f;
        lastFrameTime_ = -1.0f;
        if (s.audio) startAudioTap(s.audioSampleRate, s.audioChannels);
        afterFrameListener_ = events().afterFrame.listen([this]() { onAfterFrame(); });
        exitListener_ = events().exit.listen([this]() { stop(); });
        return true;
    }

    void onAfterFrame() {
        if (!writer_.isOpen()) return;
        // Recorded Fbo was destroyed: finalize instead of reading a dangling
        // pointer. The file stays valid up to the last captured frame.
        if (source_ == Source::Fbo && fboAlive_.expired()) {
            logWarning("ScreenRecorder")
                << "recorded Fbo was destroyed; stopping and finalizing";
            stop();
            return;
        }
        float now = getElapsedTimef();

        // Feed buffered audio to the encoder once per frame (main thread; the
        // audio thread only ever touches the lock-free ring).
        if (audioActive_) {
            std::lock_guard<std::mutex> lk(writerMutex_);
            drainAudio();
        }

        // Fixed-duration cutoff. `t` is the wall-clock PTS this frame would carry;
        // once it reaches the requested length we finalize and append nothing more,
        // so the output length == duration (to within one frame slot). The cut is
        // PTS-based, so any async capture already in flight — its PTS was sampled
        // below when it was issued, always < duration — can't push the file past
        // the cutoff; stop() just drains those before closing the writer.
        //
        // stop() clears afterFrameListener_ from inside this callback (self-removal
        // mid-dispatch). That is safe here: Event::notify() iterates an RCU
        // snapshot of the listener list held alive for the whole loop, so removing
        // ourselves only swaps in a new list for the NEXT fire; the in-progress
        // dispatch is unaffected, and removeListener() takes a recursive_mutex that
        // notify() does not hold (no deadlock). No deferral needed.
        if (duration_ > 0.0f && (double)(now - startElapsed_) >= (double)duration_) {
            stop();
            return;
        }

        float interval = 1.0f / writer_.getFps();

        // Decimate the (often higher-rate) frame stream to the target fps by
        // capturing the frame NEAREST each target slot. nextCaptureTime_ advances
        // by exactly one interval (drift-free); we capture once the slot falls
        // within half a frame of now, so a frame landing a hair before its slot
        // (e.g. every-other frame on a 120Hz display, where 16.67ms sat right on
        // the old threshold) still counts instead of aliasing the rate down. The
        // half-frame tolerance is the source frame time dt, not a fudge constant,
        // so it stays correct for any source:target ratio (120->60, 120->30,
        // 144->60, or a source slower than the target).
        float dt = (lastFrameTime_ >= 0.0f) ? (now - lastFrameTime_) : interval;
        lastFrameTime_ = now;
        if (nextCaptureTime_ >= 0.0f && (now + 0.5f * dt) < nextCaptureTime_) {
            return;   // this frame isn't the closest one to the next slot yet
        }
        nextCaptureTime_ = (nextCaptureTime_ < 0.0f ? now : nextCaptureTime_) + interval;
        if (nextCaptureTime_ <= now) {
            nextCaptureTime_ = now + interval;   // source slower than target: resync
        }
        // PTS: wall clock normally; the AUDIO DEVICE clock when an audio track
        // is being recorded, so frames land exactly where they happened on the
        // audio timeline (drift-free A/V sync however unstable the fps is).
        double t = audioActive_ ? audioClockSeconds()
                                : (double)(now - startElapsed_);
        t = std::max(t, lastPts_ + 1e-4);   // writer needs monotonic PTS
        lastPts_ = t;
        if (source_ == Source::Fbo && fbo_) {
            writer_.addFrameAt(*fbo_, t);
            return;
        }
#if TC_ASYNC_SCREEN_CAPTURE
        // Async + zero-intermediate: issue the GPU readback and return — no
        // main-thread stall. When it completes (Metal background thread), read the
        // staging straight into the encoder's pixel buffer (BGRA, no swap) and
        // submit. PTS t is captured now so wall-clock timing is unaffected by
        // encode latency.
        inFlight_.fetch_add(1, std::memory_order_relaxed);
        bool started = internal::captureWindowAsync(
            [this, t](const internal::CaptureReadback& rb) {
                {
                    std::lock_guard<std::mutex> lk(writerMutex_);
                    if (writer_.isOpen()) {
                        int stride = 0;
                        if (unsigned char* dst = writer_.lockFrame(stride)) {
                            rb.readInto(dst, stride, /*wantRGBA=*/false);
                            writer_.submitFrame(t);
                        }
                    }
                }
                inFlight_.fetch_sub(1, std::memory_order_acq_rel);
            });
        if (!started) inFlight_.fetch_sub(1, std::memory_order_acq_rel);
#else
        Pixels px;
        if (grabScreen(px) && px.isAllocated()) writer_.addFrameAt(px, t);
#endif
    }

    // --- audio tap (settings.audio) ------------------------------------------
    // Same tap pattern as AudioRecorder: a Monitor-priority audioOut listener
    // copies the master mix into an SPSC ring; the main thread drains it into
    // the writer. The tap also anchors the AUDIO DEVICE clock: each callback
    // publishes (framePosition, wall time), and audioClockSeconds() maps "now"
    // onto the audio timeline by extrapolating from the latest pair. Overflow
    // (writer stalled >4 s) is repaid as silence so A/V alignment survives.

    static double nowWallSec() {
        using namespace std::chrono;
        return duration_cast<duration<double>>(
                   steady_clock::now().time_since_epoch()).count();
    }

    void startAudioTap(int rate, int channels) {
        audioRate_ = rate;
        audioCh_   = channels;
        size_t want = (size_t)rate * channels * 4;   // ~4 seconds
        size_t cap = 1; while (cap < want) cap <<= 1;
        audioRing_.assign(cap, 0.0f);
        audioHead_.store(0, std::memory_order_relaxed);
        audioTail_.store(0, std::memory_order_relaxed);
        silenceDebt_.store(0, std::memory_order_relaxed);
        consumedFrames_ = 0;
        lastPts_ = -1.0;
        anchored_.store(false, std::memory_order_relaxed);
        recStartWall_ = nowWallSec();
        audioActive_ = true;
        audioListener_ = AudioEngine::getInstance().audioOut.listen(
            [this](AudioOutBuffer& b) { onAudioBuffer(b); },
            audio::priority::Monitor);
    }

    // Audio thread. Push the mixed buffer (prepending any silence debt) and
    // publish the clock anchor.
    void onAudioBuffer(const AudioOutBuffer& b) {
        if (b.sampleRate != audioRate_ || b.channels != audioCh_) return;
        const double noww = nowWallSec();
        if (!anchored_.load(std::memory_order_relaxed)) {
            // First buffer: its samples STARTED one buffer-duration ago; that
            // instant, expressed on the recording timeline, is the audio
            // track's PTS origin.
            fp0_ = b.framePosition;
            offset0_ = std::max(
                0.0, (noww - (double)b.frameCount / audioRate_) - recStartWall_);
            anchored_.store(true, std::memory_order_release);
        }
        fpLast_.store(b.framePosition + (uint64_t)b.frameCount, std::memory_order_relaxed);
        wallLast_.store(noww, std::memory_order_relaxed);

        const size_t cap = audioRing_.size();
        auto space = [&] {
            return cap - (size_t)(audioHead_.load(std::memory_order_relaxed)
                                  - audioTail_.load(std::memory_order_acquire));
        };
        auto push = [&](const float* src, size_t nFloats) {   // src null = silence
            const uint64_t head = audioHead_.load(std::memory_order_relaxed);
            const size_t at = (size_t)(head & (cap - 1));
            const size_t first = std::min(nFloats, cap - at);
            if (src) {
                std::memcpy(audioRing_.data() + at, src, first * sizeof(float));
                if (nFloats > first)
                    std::memcpy(audioRing_.data(), src + first, (nFloats - first) * sizeof(float));
            } else {
                std::memset(audioRing_.data() + at, 0, first * sizeof(float));
                if (nFloats > first)
                    std::memset(audioRing_.data(), 0, (nFloats - first) * sizeof(float));
            }
            audioHead_.store(head + nFloats, std::memory_order_release);
        };

        // Repay silence debt (frames lost to a previous overflow) first, so
        // the ring's content stays contiguous on the audio timeline.
        uint64_t debt = silenceDebt_.load(std::memory_order_relaxed);
        if (debt > 0) {
            uint64_t repay = std::min(debt, (uint64_t)(space() / audioCh_));
            if (repay > 0) {
                push(nullptr, (size_t)repay * audioCh_);
                silenceDebt_.store(debt - repay, std::memory_order_relaxed);
            }
        }
        const size_t n = (size_t)b.frameCount * audioCh_;
        if (space() < n || silenceDebt_.load(std::memory_order_relaxed) > 0) {
            silenceDebt_.fetch_add((uint64_t)b.frameCount, std::memory_order_relaxed);
            return;
        }
        push(b.data, n);
    }

    // Main thread (writerMutex_ held). Hand every complete frame in the ring
    // to the writer at its sample-accurate PTS.
    void drainAudio() {
        if (!writer_.isOpen() || !anchored_.load(std::memory_order_acquire)) return;
        const size_t cap = audioRing_.size();
        size_t n = (size_t)(audioHead_.load(std::memory_order_acquire)
                            - audioTail_.load(std::memory_order_relaxed));
        n -= n % (size_t)audioCh_;
        if (n == 0) return;
        audioScratch_.resize(n);
        const uint64_t tail = audioTail_.load(std::memory_order_relaxed);
        const size_t at = (size_t)(tail & (cap - 1));
        const size_t first = std::min(n, cap - at);
        std::memcpy(audioScratch_.data(), audioRing_.data() + at, first * sizeof(float));
        if (n > first)
            std::memcpy(audioScratch_.data() + first, audioRing_.data(), (n - first) * sizeof(float));
        audioTail_.store(tail + n, std::memory_order_release);

        const int frames = (int)(n / (size_t)audioCh_);
        const double pts = offset0_ + (double)consumedFrames_ / audioRate_;
        consumedFrames_ += (uint64_t)frames;
        // Respect the fixed-duration cutoff (the video side stops there too).
        if (duration_ > 0.0f && pts >= (double)duration_) return;
        writer_.writeAudio(audioScratch_.data(), frames, pts);
    }

    // "Now" on the audio timeline: extrapolate from the latest (framePosition,
    // wall time) pair the audio thread published. Before the first callback,
    // fall back to the wall clock (sub-buffer-length window at start).
    double audioClockSeconds() {
        const double noww = nowWallSec();
        if (!anchored_.load(std::memory_order_acquire))
            return noww - recStartWall_;
        const double atLast = offset0_
            + (double)(fpLast_.load(std::memory_order_relaxed) - fp0_) / audioRate_;
        return atLast + (noww - wallLast_.load(std::memory_order_relaxed));
    }

    VideoWriter writer_;
    Source source_ = Source::None;
    const Fbo* fbo_ = nullptr;
    std::weak_ptr<void> fboAlive_;  // expires when the recorded Fbo dies
    float startElapsed_ = 0.0f;
    float duration_ = 0.0f;           // fixed-length cutoff in seconds (0 = unlimited)
    float nextCaptureTime_ = -1.0f;   // scheduled time of the next frame to capture
    float lastFrameTime_ = -1.0f;     // previous afterFrame time (for source dt)
    double lastPts_ = -1.0;           // monotonicity clamp for the video PTS
    EventListener afterFrameListener_;
    EventListener exitListener_;
    std::mutex writerMutex_;          // serializes encoder access (completion threads + stop)
    std::atomic<int> inFlight_{0};    // async captures not yet encoded (macOS)

    // Audio tap state (see the comment block above).
    bool audioActive_ = false;
    int  audioRate_ = 0, audioCh_ = 0;
    std::vector<float> audioRing_, audioScratch_;
    std::atomic<uint64_t> audioHead_{0}, audioTail_{0};   // in floats
    std::atomic<uint64_t> silenceDebt_{0};                // in frames
    uint64_t consumedFrames_ = 0;                         // consumer-only
    std::atomic<bool>     anchored_{false};
    uint64_t fp0_ = 0;                // framePosition of the first buffer
    double   offset0_ = 0.0;          // audio-track PTS origin on the rec timeline
    double   recStartWall_ = 0.0;
    std::atomic<uint64_t> fpLast_{0};
    std::atomic<double>   wallLast_{0.0};
    EventListener audioListener_;
};

// ---------------------------------------------------------------------------
// Convenience: one global ScreenRecorder for "just record the whole window".
// ---------------------------------------------------------------------------
namespace internal {
    inline ScreenRecorder& globalScreenRecorder() {
        static ScreenRecorder rec;
        return rec;
    }
}

// Start recording the whole window to a video file. `path` is required;
// relative paths resolve via getDataPath(). Auto-finalizes on app exit.
TC_PLATFORMS("macos,windows,linux,android,ios") inline bool startRecording(const fs::path& path,
                           const VideoRecordSettings& settings = {}) {
    return internal::globalScreenRecorder().start(path, settings);
}

// Fixed-duration convenience: record the whole window for `durationSec` seconds,
// then auto-stop and finalize at exactly that length. (durationSec <= 0 behaves
// like the unlimited overload above — record until stopRecording().)
TC_PLATFORMS("macos,windows,linux,android,ios") inline bool startRecording(const fs::path& path,
                           float durationSec) {
    return internal::globalScreenRecorder().start(path, durationSec);
}

// Record an offscreen Fbo instead of the window (clean, GUI-free output).
// The Fbo must stay alive while recording — if it is destroyed mid-recording,
// the recording stops and finalizes automatically.
TC_PLATFORMS("macos,windows,linux,android,ios") inline bool startRecording(const Fbo& fbo,
                           const fs::path& path,
                           const VideoRecordSettings& settings = {}) {
    return internal::globalScreenRecorder().start(fbo, path, settings);
}

// Fixed-duration Fbo recording (see above).
TC_PLATFORMS("macos,windows,linux,android,ios") inline bool startRecording(const Fbo& fbo,
                           const fs::path& path,
                           float durationSec) {
    return internal::globalScreenRecorder().start(fbo, path, durationSec);
}

inline void stopRecording() { internal::globalScreenRecorder().stop(); }
inline bool isRecording()   { return internal::globalScreenRecorder().isRecording(); }
inline int  recordingFrameCount() { return internal::globalScreenRecorder().getFrameCount(); }
inline fs::path recordingPath() { return internal::globalScreenRecorder().getPath(); }

} // namespace trussc
