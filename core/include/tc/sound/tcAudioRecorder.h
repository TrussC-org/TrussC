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
//
// Every file carries a 36-byte JUNK chunk right after "WAVE" (the samples
// start at byte 80 for S16, 92 for F32; the data chunk header is at 72 / 84).
// A take whose RIFF size passes 32 bits (about 4 GiB of samples) is
// finalized as RF64 (EBU Tech 3306): the JUNK chunk becomes the ds64 chunk
// that holds the 64-bit sizes. Shorter takes stay plain RIFF; readers skip
// the JUNK chunk.
// =============================================================================

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <ostream>
#include <thread>
#include <vector>

#include "tcSound.h"
#include "../utils/tcLog.h"
#include "../utils/tcUtils.h"

namespace trussc {

namespace internal {

// -----------------------------------------------------------------------------
// AudioRecorder's WAV header (#336), as free functions on a stream so the
// 4 GiB switch can be checked on a header alone, without a 4 GiB take.
//
// Layout: RIFF/WAVE, JUNK (36 bytes), fmt, fact (float only), data. The JUNK
// chunk reserves the room of a ds64 chunk: when the RIFF size would not fit
// 32 bits, patchWavHeader() rewrites "RIFF" to "RF64", turns JUNK into ds64
// with the 64-bit RIFF size, data size and sample count, and sets the 32-bit
// RIFF, data and fact fields to 0xFFFFFFFF. Sizes and offsets are 64-bit
// throughout.
// -----------------------------------------------------------------------------

// Where writeWavHeader() put the fields that are patched on stop.
struct WavHeaderLayout {
    uint64_t junkPos = 0;       // id of the JUNK chunk (ds64 after an RF64 patch)
    uint64_t factPos = 0;       // fact sample count (float only; 0 = no fact chunk)
    uint64_t dataSizePos = 0;   // data chunk size
    uint64_t dataStart = 0;     // first sample byte = header size
};

// The size fields of a finished take.
struct WavSizeFields {
    bool     rf64 = false;         // the RIFF size needs 64 bits: write RF64 + ds64
    uint64_t riffSize = 0;         // file size - 8
    uint64_t dataSize = 0;         // bytes of sample data
    uint64_t sampleCount = 0;      // frames
    uint32_t riffSize32 = 0;       // the 32-bit fields as written
    uint32_t dataSize32 = 0;       //   (0xFFFFFFFF each when rf64)
    uint32_t factCount32 = 0;
};

inline constexpr uint32_t kWavJunkBodyBytes = 28;   // = the ds64 body with an empty table

// Pure: the header fields for `frames` frames of `channels` x `bytesPerSample`
// behind a header of `headerBytes` bytes.
inline WavSizeFields wavSizeFields(uint64_t frames, int channels, int bytesPerSample,
                                   uint64_t headerBytes) {
    WavSizeFields f;
    f.sampleCount = frames;
    f.dataSize = frames * (uint64_t)channels * (uint64_t)bytesPerSample;
    f.riffSize = headerBytes - 8 + f.dataSize;
    f.rf64 = f.riffSize > 0xFFFFFFFFull;
    if (f.rf64) {
        f.riffSize32 = f.dataSize32 = f.factCount32 = 0xFFFFFFFFu;
    } else {   // everything fits: riffSize >= dataSize, and frames <= dataSize
        f.riffSize32 = (uint32_t)f.riffSize;
        f.dataSize32 = (uint32_t)f.dataSize;
        f.factCount32 = (uint32_t)f.sampleCount;
    }
    return f;
}

inline uint64_t wavStreamPos(std::ostream& out) {
    return (uint64_t)(std::streamoff)out.tellp();
}

inline void wavSeek(std::ostream& out, uint64_t pos) {
    out.seekp(std::streampos((std::streamoff)pos));
}

// Write a header with zero sizes at the stream's position (the file start).
// Float files use format tag 3 (IEEE float) and carry a fact chunk.
inline WavHeaderLayout writeWavHeader(std::ostream& out, int sampleRate, int channels,
                                      bool isFloat) {
    auto u32 = [&](uint32_t v) { out.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { out.write((const char*)&v, 2); };
    const int bytesPerSample = isFloat ? 4 : 2;
    const uint16_t tag = isFloat ? 3 : 1;
    const uint32_t byteRate = (uint32_t)(sampleRate * channels * bytesPerSample);
    WavHeaderLayout l;
    out.write("RIFF", 4); u32(0); out.write("WAVE", 4);
    l.junkPos = wavStreamPos(out);
    out.write("JUNK", 4); u32(kWavJunkBodyBytes);
    const char zeros[kWavJunkBodyBytes] = {};
    out.write(zeros, kWavJunkBodyBytes);
    out.write("fmt ", 4); u32(16);
    u16(tag); u16((uint16_t)channels); u32((uint32_t)sampleRate);
    u32(byteRate); u16((uint16_t)(channels * bytesPerSample)); u16((uint16_t)(bytesPerSample * 8));
    if (isFloat) { out.write("fact", 4); u32(4); l.factPos = wavStreamPos(out); u32(0); }
    out.write("data", 4); l.dataSizePos = wavStreamPos(out); u32(0);
    l.dataStart = wavStreamPos(out);
    return l;
}

// Patch the sizes of a take of `frames` frames into a header written by
// writeWavHeader(), as RF64 when it needs 64 bits. Leaves the stream at its
// end. Returns the fields written.
inline WavSizeFields patchWavHeader(std::ostream& out, const WavHeaderLayout& l,
                                    uint64_t frames, int channels, bool isFloat) {
    const WavSizeFields f = wavSizeFields(frames, channels, isFloat ? 4 : 2, l.dataStart);
    auto u32 = [&](uint32_t v) { out.write((const char*)&v, 4); };
    auto u64 = [&](uint64_t v) { out.write((const char*)&v, 8); };
    if (f.rf64) {
        wavSeek(out, 0); out.write("RF64", 4);
        wavSeek(out, l.junkPos);
        out.write("ds64", 4); u32(kWavJunkBodyBytes);
        u64(f.riffSize); u64(f.dataSize); u64(f.sampleCount);
        u32(0);   // no table entries
    }
    wavSeek(out, 4); u32(f.riffSize32);
    wavSeek(out, l.dataSizePos); u32(f.dataSize32);
    if (l.factPos != 0) { wavSeek(out, l.factPos); u32(f.factCount32); }
    out.seekp(0, std::ios::end);
    return f;
}

} // namespace internal

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
            logError("AudioRecorder") << "start: cannot open " << internal::pathToDisplayUtf8(resolved);
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
        writing_.store(true, std::memory_order_release);
        writer_ = std::thread([this] { writerLoop(); });

        // Monitor priority: runs after every generator/effect listener, so the
        // ring receives the final mix (identical to the device output).
        listener_ = engine.audioOut.listen(
            [this](AudioOutBuffer& b) { capture(b); }, audio::priority::Monitor);

        logNotice("AudioRecorder") << "recording -> " << internal::pathToDisplayUtf8(path_)
            << " (" << sampleRate_ << " Hz, " << outChannels_ << "ch, "
            << (settings_.format == AudioRecordSettings::SampleFormat::S16 ? "s16" : "f32")
            << ")";
        return true;
    }

    // Stop and finalize the file. Safe to call when not recording.
    // A take past 4 GiB of samples is finalized as RF64 (logged as a notice).
    // If writing the file failed on the way (disk full, a file size limit),
    // it logs an error instead: the file is incomplete.
    // It waits on AudioEngine::waitForAudioCallbacks(), the engine-wide barrier:
    // for every audioOut / audioIn listener running at that moment, not only
    // this recorder's capture (usually well under one buffer). So don't call
    // it while holding a lock that an audioOut / audioIn listener takes: the
    // listener would block on it, stop() would wait up to one second for it,
    // and the audio drops out meanwhile.
    void stop() {
        if (!running_.exchange(false, std::memory_order_acq_rel)) return;
        listener_ = EventListener();   // unsubscribe (audio thread stops feeding)
        // Unsubscribing does not wait for a capture() already running on the
        // audio thread: one that passed its running_ check before the exchange
        // above may still be copying into the ring (#256). Wait for it, and
        // only then let the writer finish, so its final drain includes that
        // buffer. The wait also comes before the ring can be refilled by
        // start() or freed.
        AudioEngine::getInstance().waitForAudioCallbacks();
        writing_.store(false, std::memory_order_release);
        if (writer_.joinable()) writer_.join();
        const bool rf64 = patchHeader();
        file_.close();
        // A write that failed (disk full, a file size limit such as FAT32's
        // 4 GiB) leaves the stream failed: later writes, the header patch and
        // the close's flush did nothing, so the file is cut short and its
        // header was not finalized. framesWritten_ counts what was handed to
        // the stream, not what reached the file, so the RF64 decision above
        // says nothing about the file then.
        const bool writeFailed = file_.fail();
        uint64_t dropped = droppedFrames_.load(std::memory_order_relaxed);
        if (dropped > 0) {
            logWarning("AudioRecorder") << "stopped, " << dropped
                << " frames dropped (writer thread fell behind)";
        }
        if (writeFailed) {
            logError("AudioRecorder") << "writing " << internal::pathToDisplayUtf8(path_)
                << " failed (disk full or a file size limit?): the file is incomplete"
                   " and its header is not finalized";
            return;
        }
        if (rf64) {
            logNotice("AudioRecorder") << "the take passed 4 GiB: "
                << internal::pathToDisplayUtf8(path_)
                << " is written as RF64 (EBU Tech 3306); readers without RF64 support can't open it";
        }
        logNotice("AudioRecorder") << "stopped: " << internal::pathToDisplayUtf8(path_)
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
        internal::runAudioRecorderCaptureHookForTests(b.frameCount);
        head_.store(head + n, std::memory_order_release);
    }

    // --- writer thread side --------------------------------------------------
    void writerLoop() {
        std::vector<float> chunk;      // interleaved engine-format samples
        std::vector<float> mapped;     // interleaved file-format samples
        std::vector<int16_t> s16;
        // writing_, not running_: stop() clears it only after the captures
        // in flight have finished, so the final sweep sees their samples.
        while (writing_.load(std::memory_order_acquire) || pending() > 0) {
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

    void writeHeader() {
        header_ = internal::writeWavHeader(file_, sampleRate_, outChannels_, isFloat());
    }

    // Returns true when the file was finalized as RF64.
    bool patchHeader() {
        return internal::patchWavHeader(file_, header_,
                                        framesWritten_.load(std::memory_order_relaxed),
                                        outChannels_, isFloat()).rf64;
    }

    // --- state ---------------------------------------------------------------
    AudioRecordSettings settings_;
    fs::path      path_;
    std::ofstream file_;
    internal::WavHeaderLayout header_;
    int sampleRate_  = 0;
    int srcChannels_ = 0;
    int outChannels_ = 0;

    std::vector<float> ring_;
    size_t ringCap_ = 0;
    std::atomic<uint64_t> head_{0}, tail_{0};      // in floats
    std::atomic<uint64_t> droppedFrames_{0};       // in frames
    std::atomic<uint64_t> framesWritten_{0};       // in frames
    std::atomic<bool>     running_{false};   // capture() takes buffers
    std::atomic<bool>     writing_{false};   // the writer keeps draining (see stop())

    std::thread   writer_;
    EventListener listener_;
};

} // namespace trussc
