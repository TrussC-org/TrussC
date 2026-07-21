#pragma once

// =============================================================================
// tcAudioRecorder.h - Record the engine's master output to a WAV file
//
// Taps AudioEngine::audioOut at priority::Monitor, so it runs AFTER every
// generator / effect listener and captures the same mix the speakers get.
// The audio-thread listener only copies samples into a lock-free ring
// buffer; a background thread drains the ring, applies the channel map and
// writes the file — no allocation, locking or IO ever happens on the audio
// thread.
//
//   AudioRecorder rec;
//   rec.start("take.wav");           // record the master mix
//   ...
//   rec.stop();                      // finalize (patches the WAV header)
//
// The engine keeps playing as usual; recording is a pure observer.
// =============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <thread>
#include <vector>

#include "tcSound.h"
#include "../utils/tcLog.h"
#include "../utils/tcUtils.h"

namespace trussc {

// -----------------------------------------------------------------------------
// AudioRecordSettings
// -----------------------------------------------------------------------------
struct AudioRecordSettings {
    enum class SampleFormat {
        S16,    // 16-bit PCM (default; smallest, plays everywhere)
        F32,    // 32-bit IEEE float (headroom survives; no clipping in the file)
    };
    SampleFormat format = SampleFormat::S16;

    // Channel routing, same structure and semantics as Sound::setChannelMap():
    // outer index = OUTPUT (file) channel, inner list = ENGINE channels summed
    // into it — out[c] = sum of mix[s] for s in channelMap[c]. Sums are NOT
    // normalized (clipping is the caller's choice). Source indices outside the
    // engine's channel count contribute silence.
    //
    // Empty (default) = automatic: 1ch engine → mono file, 2ch → stereo,
    // 3ch+ → mono file with an averaged (sum / N) downmix.
    std::vector<std::vector<int>> channelMap;
};

// -----------------------------------------------------------------------------
// AudioRecorder
// -----------------------------------------------------------------------------
class AudioRecorder {
public:
    AudioRecorder() = default;
    ~AudioRecorder() { stop(); }

    AudioRecorder(const AudioRecorder&) = delete;
    AudioRecorder& operator=(const AudioRecorder&) = delete;

    // Start recording the master mix into `path` (WAV). The audio engine must
    // already be initialized (playing a Sound or calling AudioEngine::init()
    // does that); returns false otherwise, or when the file can't be opened.
    bool start(const fs::path& path, const AudioRecordSettings& settings = {}) {
        if (isRecording()) {
            logWarning("AudioRecorder") << "start: already recording";
            return false;
        }
        auto& engine = AudioEngine::getInstance();
        if (!engine.isInitialized()) {
            logWarning("AudioRecorder")
                << "start: AudioEngine is not initialized - nothing to record";
            return false;
        }

        sampleRate_ = engine.getSampleRate();
        srcChannels_ = engine.getChannels();
        settings_ = settings;
        outChannels_ = !settings_.channelMap.empty() ? (int)settings_.channelMap.size()
                     : (srcChannels_ == 2 ? 2 : 1);
        if (outChannels_ <= 0) {
            logWarning("AudioRecorder") << "start: empty channel map";
            return false;
        }

        fs::path resolved = getDataPath(path);
        if (resolved.has_parent_path()) {   // same convenience as VideoWriter
            std::error_code ec;
            fs::create_directories(resolved.parent_path(), ec);
        }
        file_.open(resolved, std::ios::binary | std::ios::trunc);
        if (!file_) {
            logError("AudioRecorder") << "start: cannot open " << resolved.string();
            return false;
        }
        path_ = resolved;
        writeHeader();   // sizes are patched in stop()

        // Ring sized for ~4 seconds of engine output: overflow only happens if
        // the writer thread stalls that long, in which case frames are counted
        // into droppedFrames_ instead of blocking the audio thread.
        ringCap_ = (size_t)1 << (size_t)std::ceil(
            std::log2((double)sampleRate_ * srcChannels_ * 4.0));
        ring_.assign(ringCap_, 0.0f);
        head_.store(0, std::memory_order_relaxed);
        tail_.store(0, std::memory_order_relaxed);
        droppedFrames_.store(0, std::memory_order_relaxed);
        framesWritten_.store(0, std::memory_order_relaxed);

        running_.store(true, std::memory_order_release);
        writer_ = std::thread([this] { writerLoop(); });

        // Monitor priority: runs after every generator/effect listener, so the
        // ring receives the final mix (identical to the device output).
        listener_ = engine.audioOut.listen(
            [this](AudioOutBuffer& b) { capture(b); }, audio::priority::Monitor);

        logNotice("AudioRecorder") << "recording -> " << path_.string()
            << " (" << sampleRate_ << " Hz, " << outChannels_ << "ch, "
            << (settings_.format == AudioRecordSettings::SampleFormat::S16 ? "s16" : "f32")
            << ")";
        return true;
    }

    // Stop and finalize the file. Safe to call when not recording.
    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        listener_ = EventListener();   // unsubscribe (audio thread stops feeding)
        if (writer_.joinable()) writer_.join();
        patchHeader();
        file_.close();
        uint64_t dropped = droppedFrames_.load(std::memory_order_relaxed);
        if (dropped > 0) {
            logWarning("AudioRecorder") << "stopped, " << dropped
                << " frames dropped (writer thread fell behind)";
        }
        logNotice("AudioRecorder") << "stopped: " << path_.string()
            << " (" << getRecordedSeconds() << " s)";
    }

    bool isRecording() const { return running_.load(std::memory_order_acquire); }

    // Seconds actually written to the file so far.
    double getRecordedSeconds() const {
        return sampleRate_ > 0
            ? (double)framesWritten_.load(std::memory_order_relaxed) / sampleRate_
            : 0.0;
    }

    // Frames lost to ring-buffer overflow (0 in normal operation).
    uint64_t getDroppedFrames() const {
        return droppedFrames_.load(std::memory_order_relaxed);
    }

    fs::path getPath() const { return path_; }

private:
    // --- audio thread side ---------------------------------------------------
    void capture(const AudioOutBuffer& b) {
        if (!running_.load(std::memory_order_acquire)) return;
        // Guard against a device change mid-recording (rate/channel switch
        // would corrupt the file): drop and count instead.
        if (b.sampleRate != sampleRate_ || b.channels != srcChannels_) {
            droppedFrames_.fetch_add((uint64_t)b.frameCount, std::memory_order_relaxed);
            return;
        }
        const size_t n = (size_t)b.frameCount * b.channels;
        const uint64_t head = head_.load(std::memory_order_relaxed);
        const uint64_t tail = tail_.load(std::memory_order_acquire);
        if (ringCap_ - (size_t)(head - tail) < n) {
            droppedFrames_.fetch_add((uint64_t)b.frameCount, std::memory_order_relaxed);
            return;
        }
        const size_t at = (size_t)(head & (ringCap_ - 1));
        const size_t first = std::min(n, ringCap_ - at);
        std::memcpy(ring_.data() + at, b.data, first * sizeof(float));
        if (n > first) std::memcpy(ring_.data(), b.data + first, (n - first) * sizeof(float));
        head_.store(head + n, std::memory_order_release);
    }

    // --- writer thread side --------------------------------------------------
    void writerLoop() {
        std::vector<float> chunk;      // interleaved engine-format samples
        std::vector<float> mapped;     // interleaved file-format samples
        std::vector<int16_t> s16;
        while (running_.load(std::memory_order_acquire) || pending() > 0) {
            drain(chunk, mapped, s16);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        drain(chunk, mapped, s16);     // final sweep after the listener detached
    }

    size_t pending() const {
        return (size_t)(head_.load(std::memory_order_acquire)
                        - tail_.load(std::memory_order_relaxed));
    }

    void drain(std::vector<float>& chunk, std::vector<float>& mapped,
               std::vector<int16_t>& s16) {
        size_t n = pending();
        n -= n % (size_t)srcChannels_;   // whole frames only
        if (n == 0) return;
        chunk.resize(n);
        const uint64_t tail = tail_.load(std::memory_order_relaxed);
        const size_t at = (size_t)(tail & (ringCap_ - 1));
        const size_t first = std::min(n, ringCap_ - at);
        std::memcpy(chunk.data(), ring_.data() + at, first * sizeof(float));
        if (n > first) std::memcpy(chunk.data() + first, ring_.data(), (n - first) * sizeof(float));
        tail_.store(tail + n, std::memory_order_release);

        const size_t frames = n / srcChannels_;
        mapChannels(chunk.data(), frames, mapped);

        if (settings_.format == AudioRecordSettings::SampleFormat::S16) {
            s16.resize(mapped.size());
            for (size_t i = 0; i < mapped.size(); i++) {
                float v = std::clamp(mapped[i], -1.0f, 1.0f);
                s16[i] = (int16_t)std::lrintf(v * 32767.0f);
            }
            file_.write((const char*)s16.data(), (std::streamsize)(s16.size() * sizeof(int16_t)));
        } else {
            file_.write((const char*)mapped.data(), (std::streamsize)(mapped.size() * sizeof(float)));
        }
        framesWritten_.fetch_add((uint64_t)frames, std::memory_order_relaxed);
    }

    // Engine-interleaved -> file-interleaved. Explicit map: out[c] = sum of
    // sources (unnormalized, same as Sound::setChannelMap). Auto: 1:1 for
    // mono/stereo engines, averaged downmix to mono for 3ch+.
    void mapChannels(const float* src, size_t frames, std::vector<float>& out) {
        out.assign(frames * outChannels_, 0.0f);
        if (!settings_.channelMap.empty()) {
            for (size_t f = 0; f < frames; f++) {
                const float* in = src + f * srcChannels_;
                float* o = out.data() + f * outChannels_;
                for (int c = 0; c < outChannels_; c++) {
                    float acc = 0.0f;
                    for (int s : settings_.channelMap[(size_t)c]) {
                        if (s >= 0 && s < srcChannels_) acc += in[s];
                    }
                    o[c] = acc;
                }
            }
        } else if (srcChannels_ == outChannels_) {
            std::memcpy(out.data(), src, frames * srcChannels_ * sizeof(float));
        } else {   // auto mono downmix (average keeps levels sane by default)
            const float inv = 1.0f / (float)srcChannels_;
            for (size_t f = 0; f < frames; f++) {
                const float* in = src + f * srcChannels_;
                float acc = 0.0f;
                for (int s = 0; s < srcChannels_; s++) acc += in[s];
                out[f] = acc * inv;
            }
        }
    }

    // --- WAV plumbing --------------------------------------------------------
    bool isFloat() const { return settings_.format == AudioRecordSettings::SampleFormat::F32; }
    int bytesPerSample() const { return isFloat() ? 4 : 2; }

    void writeHeader() {
        // RIFF/WAVE with fmt (+ fact for float) and a data chunk whose size is
        // patched on stop. Float files use format tag 3 (IEEE float).
        auto u32 = [&](uint32_t v) { file_.write((const char*)&v, 4); };
        auto u16 = [&](uint16_t v) { file_.write((const char*)&v, 2); };
        const uint16_t tag = isFloat() ? 3 : 1;
        const uint32_t byteRate = (uint32_t)(sampleRate_ * outChannels_ * bytesPerSample());
        file_.write("RIFF", 4); u32(0); file_.write("WAVE", 4);
        file_.write("fmt ", 4); u32(16);
        u16(tag); u16((uint16_t)outChannels_); u32((uint32_t)sampleRate_);
        u32(byteRate); u16((uint16_t)(outChannels_ * bytesPerSample())); u16((uint16_t)(bytesPerSample() * 8));
        if (isFloat()) { file_.write("fact", 4); u32(4); factPos_ = file_.tellp(); u32(0); }
        file_.write("data", 4); dataSizePos_ = file_.tellp(); u32(0);
        dataStart_ = file_.tellp();
    }

    void patchHeader() {
        const uint64_t frames = framesWritten_.load(std::memory_order_relaxed);
        const uint32_t dataBytes = (uint32_t)(frames * outChannels_ * bytesPerSample());
        auto patch32 = [&](std::streampos pos, uint32_t v) {
            file_.seekp(pos); file_.write((const char*)&v, 4);
        };
        patch32(dataSizePos_, dataBytes);
        if (isFloat()) patch32(factPos_, (uint32_t)frames);
        patch32(std::streampos(4), (uint32_t)((uint64_t)dataStart_ - 8 + dataBytes));
        file_.seekp(0, std::ios::end);
    }

    // --- state ---------------------------------------------------------------
    AudioRecordSettings settings_;
    fs::path      path_;
    std::ofstream file_;
    std::streampos dataSizePos_{}, factPos_{}, dataStart_{};
    int sampleRate_  = 0;
    int srcChannels_ = 0;
    int outChannels_ = 0;

    std::vector<float> ring_;
    size_t ringCap_ = 0;
    std::atomic<uint64_t> head_{0}, tail_{0};      // in floats
    std::atomic<uint64_t> droppedFrames_{0};       // in frames
    std::atomic<uint64_t> framesWritten_{0};       // in frames
    std::atomic<bool>     running_{false};

    std::thread   writer_;
    EventListener listener_;
};

} // namespace trussc
