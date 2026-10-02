// =============================================================================
// tcAudio implementation
// Sound playback, microphone input, and file decoding via miniaudio
//
// Why miniaudio instead of sokol_audio:
// - sokol_audio is playback-only (no microphone/capture support)
// - miniaudio configures AAudio properly on Android (usage, content type),
//   while sokol_audio's minimal AAudio init can fail to produce audible output
// - miniaudio provides device enumeration and format conversion
//
// Decoder configuration:
// - MA_NO_DECODING is intentionally NOT set: ma_decoder (WAV/MP3/FLAC) is used
//   by tcSound_impl.cpp to decode static asset files
// - MA_NO_ENCODING: we don't write audio files
// - MA_NO_GENERATION: TrussC has its own generators (sine/square/noise/etc)
//
// AAC remains platform-specific (AudioToolbox / GStreamer / MediaCodec) for
// best platform-native quality; OGG Vorbis stays on stb_vorbis because
// miniaudio does not bundle a Vorbis decoder.
// =============================================================================

#define MA_NO_ENCODING
#define MA_NO_GENERATION
#define MINIAUDIO_IMPLEMENTATION
#include "tc/sound/tcAudioDeviceInternal.h"

#include "tc/sound/tcSound.h"
#include "tc/utils/tcFile.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace trussc {

namespace {
// Open a ma_decoder from a path. Wide entry point on Windows so non-ASCII
// paths survive (same helper as tcSound_impl.cpp).
ma_result maDecoderInitPathA(const fs::path& path,
                             const ma_decoder_config* cfg, ma_decoder* dec) {
#ifdef _WIN32
    return ma_decoder_init_file_w(path.c_str(), cfg, dec);
#else
    return ma_decoder_init_file(path.c_str(), cfg, dec);
#endif
}

// Set by internal::setNullAudioBackendForTests(): the engine, device
// enumeration and MicInput open miniaudio's null backend only.
std::atomic<bool> g_nullBackendForTests{false};

// Whether the engine's persistent context was opened with the null backend
// on request (the test hook above), so landing on it is not a fallback.
// Main thread only: written and read in AudioEngine::init().
bool g_engineNullBackendRequested = false;

// Set by internal::setAudioRecorderCaptureHookForTests(); nullptr normally.
std::atomic<void (*)(int)> g_recorderCaptureHook{nullptr};

// Set by internal::setStreamFaultForTests(): an internal::StreamFaultForTests
// value; None normally.
std::atomic<int> g_streamFault{0};

// StreamFaultForTests::MixerLags: the mixer has read a ring's write position
// with a seek pending, which releases the worker.
std::atomic<bool> g_mixerLagReleased{false};

// Read by internal::lastStreamSeekPointsForTests(): the seek points of the
// stream decoder opened last.
std::atomic<uint32_t> g_lastStreamSeekPoints{0};

// Read by internal::streamWorkerPassesForTests(): the StreamWorker's passes.
std::atomic<uint64_t> g_streamWorkerPasses{0};

const ma_backend kNullBackend = ma_backend_null;

// ma_context_init with miniaudio's default backend order for the platform,
// or only the null backend under the test hook.
ma_result initContext(ma_context* ctx) {
    if (g_nullBackendForTests.load(std::memory_order_relaxed)) {
        return ma_context_init(&kNullBackend, 1, NULL, ctx);
    }
    return ma_context_init(NULL, 0, NULL, ctx);
}
} // namespace

namespace internal {
bool openedDeviceIsDefault(const ma_device_id* selectedID,
                           const ma_device_info* infos, ma_uint32 count) {
    // Some backends leave playback.id zeroed when opening the default.
    if (!selectedID) return true;
    for (ma_uint32 i = 0; i < count; ++i) {
        if (std::memcmp(&infos[i].id, selectedID, sizeof(ma_device_id)) == 0) {
            return infos[i].isDefault != 0;
        }
    }
    return false;
}

void setNullAudioBackendForTests(bool on) {
    g_nullBackendForTests.store(on, std::memory_order_relaxed);
}

void setAudioRecorderCaptureHookForTests(void (*hook)(int frames)) {
    g_recorderCaptureHook.store(hook, std::memory_order_release);
}

void runAudioRecorderCaptureHookForTests(int frames) {
    // Audio thread, once per captured buffer: one load when unset.
    if (auto hook = g_recorderCaptureHook.load(std::memory_order_acquire)) hook(frames);
}

void setStreamFaultForTests(StreamFaultForTests fault) {
    g_mixerLagReleased.store(false, std::memory_order_relaxed);
    g_streamFault.store((int)fault, std::memory_order_relaxed);
}

uint32_t lastStreamSeekPointsForTests() {
    return g_lastStreamSeekPoints.load(std::memory_order_relaxed);
}

uint64_t streamWorkerPassesForTests() {
    return g_streamWorkerPasses.load(std::memory_order_relaxed);
}
} // namespace internal

// =============================================================================
// Engine diagnostics
//
// Counters and meters behind AudioEngine::getStats() / tc_get_audio_state.
// The audio thread only touches atomics and the accumulators it owns; it
// never logs, locks or allocates here. Plays dropped off the main thread,
// and repeats inside the rate limit, reach the log through
// reportDiagnostics(), which the app loop runs on the main thread every
// frame (pumpAudioDiagnostics()). Log lines about one drop reason are at
// least kReportInterval apart; the ones in between are summed into the next
// line, so a sound that keeps hitting the voice limit cannot flood a log
// file.
// =============================================================================

namespace internal {

struct AudioDiagnostics {
    static constexpr int kDropReasons = 4;   // AudioEngine::DropReason values
    static constexpr std::chrono::seconds kReportInterval{2};

    // --- any thread (play() can be called off the main thread) ---
    std::atomic<uint64_t> dropped[kDropReasons]{};
    std::atomic<uint64_t> unreportedDrops[kDropReasons]{};  // counted, not logged yet

    // --- written by the audio thread, read anywhere ---
    std::atomic<uint64_t> clippedSamples{0};
    std::atomic<float>    peak{0.0f};
    std::atomic<float>    rms{0.0f};
    std::atomic<float>    cpuUsage{0.0f};
    std::atomic<float>    cpuUsagePeak{0.0f};

    // --- audio thread only (reset while no device runs): meter / load windows ---
    float    winPeak = 0.0f;
    double   winSumSq = 0.0;
    uint64_t winSamples = 0;     // frames * channels in the meter window
    uint64_t winFrames = 0;
    double   loadBusy = 0.0;     // seconds spent mixing in the load window
    double   loadAudio = 0.0;    // seconds of audio produced in the load window
    float    loadWinMax = 0.0f;

    // --- main thread only: report state ---
    std::chrono::steady_clock::time_point lastDropLog[kDropReasons]{};
    bool     deviceIsDefault = false;   // set by init()

    AudioDiagnostics() {
        // "Long ago", so the first report of each reason goes out at once
        // (steady_clock's epoch is not guaranteed to be far in the past).
        const auto longAgo = std::chrono::steady_clock::now() - kReportInterval;
        for (auto& t : lastDropLog) t = longAgo;
    }
};

} // namespace internal

using internal::AudioDiagnostics;

namespace {

// Module name and summary text per AudioEngine::DropReason, by its index
// (PolyphonyLimit, StreamLimit, DecoderError, NotRunning).
const char* dropModule(int reason) {
    return (reason == 1 || reason == 2) ? "SoundStream" : "AudioEngine";
}

const char* dropReasonText(int reason) {
    switch (reason) {
        case 0: return "every playback slot busy (raise AudioSettings::maxPolyphony)";
        case 1: return "a SoundStream reached its maxPolyphony (copies of a streamed Sound share it)";
        case 2: return "a stream's file could not be reopened for a new playback";
        case 3: return "no output device running";
    }
    return "unknown";
}

// "path/to/file.wav", or a note that the source has no file behind it.
std::string sourceLabel(const SoundSource* source) {
    if (!source) return "(no source)";
    const fs::path p = (source->kind() == SoundSource::Stream)
        ? static_cast<const SoundStream*>(source)->getPath()
        : static_cast<const SoundBuffer*>(source)->getPath();
    return p.empty() ? std::string("a generated / in-memory buffer")
                     : internal::pathToUtf8(p);
}

} // namespace

AudioEngine::AudioEngine()
    : diag_(std::make_unique<AudioDiagnostics>()) {
    playingSounds_.resize(DEFAULT_MAX_PLAYING_SOUNDS);
    analysisBuffer_.resize(ANALYSIS_BUFFER_SIZE, 0.0f);
}

AudioEngine::~AudioEngine() {
    shutdown();
}

void AudioEngine::noteDroppedPlay(DropReason reason, const SoundSource* source, int code) {
    AudioDiagnostics& d = *diag_;
    const int r = (int)reason;
    d.dropped[r].fetch_add(1, std::memory_order_relaxed);

    // Off the main thread (a worker, or an audioOut listener that calls
    // play()): only count. The main thread reports it without this thread
    // ever waiting on the logger.
    if (!isMainThread()) {
        d.unreportedDrops[r].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - d.lastDropLog[r] < AudioDiagnostics::kReportInterval) {
        d.unreportedDrops[r].fetch_add(1, std::memory_order_relaxed);
        return;
    }
    d.lastDropLog[r] = now;
    const uint64_t earlier = d.unreportedDrops[r].exchange(0, std::memory_order_relaxed);

    auto line = logWarning(dropModule(r));
    switch (reason) {
        case DropReason::PolyphonyLimit:
            line << "play dropped: all " << playingSounds_.size() << " playback slots are busy ("
                 << sourceLabel(source) << "). Raise AudioSettings::maxPolyphony or stop "
                    "sounds that no longer need to play";
            break;
        case DropReason::StreamLimit:
            line << "play dropped: maxPolyphony="
                 << static_cast<const SoundStream*>(source)->getMaxPolyphony()
                 << " reached for " << sourceLabel(source)
                 << " (copies of a streamed Sound share its playback slots). Stop a previous "
                    "instance or raise maxPolyphony in loadStream()";
            break;
        case DropReason::DecoderError:
            line << "play dropped: could not reopen " << sourceLabel(source)
                 << " for a new playback (result=" << code << ")";
            break;
        case DropReason::NotRunning:
            line << "play dropped: no output device is running (" << sourceLabel(source)
                 << "); the engine failed to initialize or was shut down";
            break;
    }
    if (earlier > 0) line << " [+" << earlier << " more since the last report]";
}

void AudioEngine::meterOutput(const float* buffer, int numFrames, int numChannels) {
    AudioDiagnostics& d = *diag_;
    const int n = numFrames * numChannels;
    float    blockPeak = 0.0f;
    double   sumSq = 0.0;
    uint64_t clipped = 0;
    for (int i = 0; i < n; ++i) {
        const float s = buffer[i];
        const float mag = std::fabs(s);
        if (mag > blockPeak) blockPeak = mag;
        if (mag > 1.0f) ++clipped;
        sumSq += (double)s * (double)s;
    }
    if (clipped > 0) d.clippedSamples.fetch_add(clipped, std::memory_order_relaxed);

    // Publish peak / RMS once per ~100 ms of audio.
    if (blockPeak > d.winPeak) d.winPeak = blockPeak;
    d.winSumSq   += sumSq;
    d.winSamples += (uint64_t)n;
    d.winFrames  += (uint64_t)numFrames;
    const uint64_t rate = (uint64_t)(sampleRate_ > 0 ? sampleRate_ : DEFAULT_SAMPLE_RATE);
    if (d.winFrames * 10 >= rate) {
        d.peak.store(d.winPeak, std::memory_order_relaxed);
        d.rms.store(d.winSamples > 0 ? (float)std::sqrt(d.winSumSq / (double)d.winSamples) : 0.0f,
                    std::memory_order_relaxed);
        d.winPeak = 0.0f;
        d.winSumSq = 0.0;
        d.winSamples = 0;
        d.winFrames = 0;
    }
}

void AudioEngine::reportDiagnostics(bool force) {
    AudioDiagnostics& d = *diag_;
    const auto now = std::chrono::steady_clock::now();

    for (int r = 0; r < AudioDiagnostics::kDropReasons; ++r) {
        if (d.unreportedDrops[r].load(std::memory_order_relaxed) == 0) continue;
        if (!force && now - d.lastDropLog[r] < AudioDiagnostics::kReportInterval) continue;
        const uint64_t n = d.unreportedDrops[r].exchange(0, std::memory_order_relaxed);
        if (n == 0) continue;
        d.lastDropLog[r] = now;
        logWarning(dropModule(r)) << n << (n == 1 ? " play" : " plays")
                                  << " dropped since the last report: " << dropReasonText(r);
    }
}

void AudioEngine::resetMeters() {
    AudioDiagnostics& d = *diag_;
    d.peak.store(0.0f, std::memory_order_relaxed);
    d.rms.store(0.0f, std::memory_order_relaxed);
    d.cpuUsage.store(0.0f, std::memory_order_relaxed);
    d.cpuUsagePeak.store(0.0f, std::memory_order_relaxed);
    d.winPeak = 0.0f;
    d.winSumSq = 0.0;
    d.winSamples = 0;
    d.winFrames = 0;
    d.loadBusy = 0.0;
    d.loadAudio = 0.0;
    d.loadWinMax = 0.0f;

    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& v : playingSounds_) {
        if (v) v->level.store(0.0f, std::memory_order_relaxed);
    }
}

AudioStats AudioEngine::getStats() const {
    const AudioDiagnostics& d = *diag_;
    auto get = [](const std::atomic<uint64_t>& a) { return a.load(std::memory_order_relaxed); };
    AudioStats s;
    s.droppedPolyphonyLimit = get(d.dropped[(int)DropReason::PolyphonyLimit]);
    s.droppedStreamLimit    = get(d.dropped[(int)DropReason::StreamLimit]);
    s.droppedDecoderError   = get(d.dropped[(int)DropReason::DecoderError]);
    s.droppedNotRunning     = get(d.dropped[(int)DropReason::NotRunning]);
    s.droppedPlays = s.droppedPolyphonyLimit + s.droppedStreamLimit
                   + s.droppedDecoderError + s.droppedNotRunning;
    s.clippedSamples = get(d.clippedSamples);
    s.peak         = d.peak.load(std::memory_order_relaxed);
    s.rms          = d.rms.load(std::memory_order_relaxed);
    s.cpuUsage     = d.cpuUsage.load(std::memory_order_relaxed);
    s.cpuUsagePeak = d.cpuUsagePeak.load(std::memory_order_relaxed);
    return s;
}

std::vector<PlayingSoundInfo> AudioEngine::getPlayingSounds() const {
    std::vector<PlayingSoundInfo> out;
    std::lock_guard<std::mutex> lock(mutex_);
    for (size_t i = 0; i < playingSounds_.size(); ++i) {
        const auto& v = playingSounds_[i];
        if (!v || !v->buffer || !v->playing) continue;  // paused voices keep playing = true
        const SoundSource& src = *v->buffer;
        PlayingSoundInfo info;
        info.slot      = (int)i;
        info.streaming = (src.kind() == SoundSource::Stream);
        const fs::path p = info.streaming ? static_cast<const SoundStream&>(src).getPath()
                                          : static_cast<const SoundBuffer&>(src).getPath();
        info.path     = p;  // as given, same string as getPath() and the logs
        info.paused   = v->paused;
        info.loop     = v->loop;
        // positionF counts source frames for eager voices and, for streams,
        // frames at the voice's own position rate (the engine rate its
        // decoder outputs at, which a voice the re-init migration did not
        // rebuild keeps from before). Same rate as Sound::positionRate().
        const int rate = info.streaming ? v->positionRateHz_.load(std::memory_order_relaxed)
                                        : src.sampleRate;
        info.position = rate > 0 ? (float)(v->positionF / (double)rate) : 0.0f;
        info.duration = src.getDuration();
        info.volume   = v->volume;
        info.pan      = v->pan;
        info.speed    = v->speed;
        info.level    = info.paused ? 0.0f : v->level.load(std::memory_order_relaxed);
        out.push_back(std::move(info));
    }
    return out;
}

namespace internal {

void pumpAudioDiagnostics() {
    AudioEngine::getInstance().reportDiagnostics();
}

void flushAudioDiagnostics() {
    AudioEngine::getInstance().reportDiagnostics(true);
}

AudioDeviceReport audioDeviceReport(bool enumerate) {
    AudioDeviceReport r;
    AudioEngine& engine = AudioEngine::getInstance();
    ma_context* ctx = static_cast<ma_context*>(engine.context_);
    if (ctx) r.backend = ma_get_backend_name(ctx->backend);

    if (engine.device_) {
        const ma_device* dev = static_cast<const ma_device*>(engine.device_);
        r.outputDevice     = dev->playback.name;
        r.outputIsDefault  = engine.diag_->deviceIsDefault;
        r.periodFrames     = (int)dev->playback.internalPeriodSizeInFrames;
        r.deviceSampleRate = (int)dev->playback.internalSampleRate;
        r.deviceChannels   = (int)dev->playback.internalChannels;
    }

    if (enumerate) {
        // Enumerate through the engine's context when it has one; otherwise
        // through a throwaway context, so asking never starts the engine.
        ma_context temp;
        bool tempInit = false;
        if (!ctx && initContext(&temp) == MA_SUCCESS) {
            ctx = &temp;
            tempInit = true;
            r.backend = ma_get_backend_name(temp.backend);
        }
        ma_device_info* playback = nullptr;
        ma_device_info* capture = nullptr;
        ma_uint32 playbackCount = 0;
        ma_uint32 captureCount = 0;
        if (ctx && ma_context_get_devices(ctx, &playback, &playbackCount,
                                          &capture, &captureCount) == MA_SUCCESS) {
            r.enumerated = true;
            for (ma_uint32 i = 0; i < playbackCount; ++i) {
                r.playbackDevices.push_back({playback[i].name, playback[i].isDefault != 0});
            }
            for (ma_uint32 i = 0; i < captureCount; ++i) {
                r.captureDevices.push_back({capture[i].name, capture[i].isDefault != 0});
            }
        }
        if (tempInit) ma_context_uninit(&temp);
    }
    return r;
}

} // namespace internal


// =============================================================================
// Streaming audio playback
//
// Each play() of a SoundStream allocates a StreamInstance: an ma_decoder
// configured to output at engine sample rate + stereo (so the mixer can
// memcpy + apply vol/pan without resampling), plus a small ring buffer
// fed by a dedicated worker thread.
//
// Ring is SPSC:
//   - producer = StreamWorker thread (writes at writeFrame_)
//   - consumer = audio callback / mixer (reads at readFrame_)
// Both indices are monotonic uint64; modulo RING_FRAMES on access. Power
// of 2 keeps the wrap to a bitwise AND.
//
// Sizing: RING_FRAMES = 16384 stereo frames at 96 kHz = ~170 ms latency
// budget. Worker polls every ~5 ms so underrun is unlikely under normal
// load. Bigger = more headroom + memory; smaller = less RAM but more
// vulnerable to long decode stalls.
//
// Seek (#280) keeps the ring SPSC: only the worker writes writeFrame, only
// the mixer writes readFrame, subFrame and the voice's positionF (besides
// play() and the re-init migration, which set up a voice the mixer does
// not run at that moment).
//   1. internal::seekVoice() stores the target, then bumps seekRequestSeq
//      (release). The request is pending while seekRequestSeq differs from
//      seekAppliedSeq.
//   2. The worker takes the latest request (once the mixer has applied the
//      previous seek), seeks the decoder and publishes where the post-seek
//      data starts: seekBaseFrame = the current writeFrame, the target and
//      the request's seq, then seekEpoch (release). Only then does it write
//      the post-seek data from that writeFrame on. Publishing first matters:
//      the mixer loads writeFrame before seekEpoch, so a writeFrame that
//      already covers post-seek data implies it also sees the new epoch,
//      and it never plays post-seek frames as if they were old ones.
//   3. The mixer, on a new epoch, sets readFrame = seekBaseFrame, subFrame
//      = 0 and positionF = target, then stores seekAppliedEpoch and
//      seekAppliedSeq (release). readFrame never moves backwards: a mixer
//      that had run past writeFrame (it can overshoot by up to ceil(speed)-1
//      frames on an underrun) keeps its readFrame, because the worker bounded
//      its writes by that value. While a request is still pending it plays
//      nothing, so a superseded position is never heard.
// =============================================================================

namespace internal {

struct StreamInstance {
    static constexpr size_t RING_FRAMES = 16384;          // power of 2
    static constexpr size_t RING_MASK   = RING_FRAMES - 1;
    static constexpr int    CHANNELS    = 2;              // stereo, hard-coded
                                                          // because that's what
                                                          // the mixer consumes
    ma_decoder decoder;
    bool decoderInitialized = false;

    // Interleaved stereo float, size = RING_FRAMES * CHANNELS.
    std::vector<float> ring;

    std::atomic<uint64_t> writeFrame{0};
    std::atomic<uint64_t> readFrame{0};
    std::atomic<bool>     endOfStream{false};
    std::atomic<bool>     looping{false};
    std::atomic<bool>     disposed{false};   // set by AudioEngine to retire
                                              // the instance from the worker

    // Seek protocol (see the section comment above).
    // Requester (internal::seekVoice()):
    std::atomic<double>   seekTargetFrame{0.0};   // latest requested target
    std::atomic<uint64_t> seekRequestSeq{0};      // bumped per request (release)
    // Worker -> mixer, stable until the mixer has applied them:
    std::atomic<uint64_t> seekBaseFrame{0};       // ring frame the post-seek data starts at
    std::atomic<double>   seekPublishedTarget{0.0};
    std::atomic<uint64_t> seekPublishedSeq{0};    // request this seek served
    std::atomic<uint64_t> seekEpoch{0};           // bumped after the three above (release)
    // Mixer -> worker / requester:
    std::atomic<uint64_t> seekAppliedEpoch{0};
    std::atomic<uint64_t> seekAppliedSeq{0};      // pending while != seekRequestSeq

    // Worker only.
    uint64_t seekServedSeq = 0;   // last request the worker took
    // Under the worker's mutex: whether the worker has looked at this
    // stream, and the read position it saw then (its poll interval, #550).
    bool workerSeen = false;
    uint64_t workerSeenReadFrame = 0;
    // The stream ended on an error (published with endOfStream, cleared by
    // an explicit seek request). The worker decodes nothing more for it
    // until then. The mixer ends the voice once the ring has drained,
    // including looping voices. Also set by the re-init migration before
    // registration. Atomic because the mixer reads it too (see halt()).
    std::atomic<bool> halted{false};
    // The decoder returned no frames on a non-looping stream: nothing more
    // to read until a seek request, or until the loop flag is set and the
    // worker starts the file over. Unlike endOfStream it is cleared when the
    // worker loops back, so a stream that looped after its end and then
    // stops looping is read on to its real end.
    bool decoderAtEnd = false;

    // Under the engine lock (internal::seekVoice()): a seek on this voice
    // was refused because the length is unknown, and that was logged.
    bool unknownLengthSeekLogged = false;

    uint64_t totalFramesInFile = 0;           // decoder output (engine-rate) frames; 0 = unknown
    std::string pathUtf8;                     // for the worker's log lines

    // Sub-frame position inside the ring for setSpeed-aware linear interp.
    // Only touched by the mixer (audio callback thread) — single-writer, no
    // atomicity needed.
    double subFrame = 0.0;

    StreamInstance() : ring(RING_FRAMES * CHANNELS, 0.0f) {}

    // Open this voice's decoder for `src`, outputting stereo f32 at the
    // engine rate `rate`, and read its length. play() and the re-init
    // migration, on the caller's thread.
    //
    // An MP3 gets a seek table (#280): without one dr_mp3 seeks by decoding
    // from the start (or from the current frame, forward), which on a
    // one-hour file took up to 2.5 s on a desktop CPU and blocks the single
    // StreamWorker, so every other stream underruns meanwhile. With one
    // point per second of audio a seek decodes at most ~1 s (~1 ms). The
    // count is capped at 1024 (24 bytes each, 24 KB per voice): past ~17
    // minutes the points spread out, a one-hour file seeks in ~3 ms and a
    // three-hour one in ~10 ms. Building the table scans the file's frame
    // headers once more, on this thread (~75 ms for a one-hour 192 kbps
    // file in the page cache; reading its length already takes one such
    // scan). WAV and FLAC seek on their own; only the MP3 backend reads
    // seekPointCount.
    ma_result openDecoder(const SoundStream& src, ma_uint32 rate) {
        ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, CHANNELS, rate);
        cfg.encodingFormat = (ma_encoding_format)src.encodingFormatHint_;
        if (cfg.encodingFormat == ma_encoding_format_mp3) {
            cfg.seekPointCount = mp3SeekPointCount(src.duration_);
        }
        const ma_result r = maDecoderInitPathA(src.path_, &cfg, &decoder);
        if (r != MA_SUCCESS) return r;
        decoderInitialized = true;
        uint32_t seekPoints = 0;
        if (decoder.pBackendVTable == &g_ma_decoding_backend_vtable_mp3 && decoder.pBackend) {
            seekPoints = static_cast<const ma_mp3*>(decoder.pBackend)->seekPointCount;
        }
        g_lastStreamSeekPoints.store(seekPoints, std::memory_order_relaxed);
        ma_uint64 total = 0;
        ma_decoder_get_length_in_pcm_frames(&decoder, &total);
        totalFramesInFile = (uint64_t)total;
        pathUtf8 = internal::pathToUtf8(src.path_);
        return MA_SUCCESS;
    }

    // Sound::setPosition() clamps to the float getDuration(), which on a
    // file of ~17 minutes or more can land a few frames past the last one;
    // dr_mp3 fails a seek past the end. Clamp to the last frame when the
    // length is known. The worker and the re-init migration.
    double clampSeekTarget(double target) const {
        if (totalFramesInFile > 0 && target > (double)(totalFramesInFile - 1)) {
            return (double)(totalFramesInFile - 1);
        }
        return target;
    }

    // Seek the decoder, with the test fault applied. The worker, and the
    // re-init migration before it registers the instance.
    ma_result seekDecoder(uint64_t frame) {
        if (g_streamFault.load(std::memory_order_relaxed)
                == (int)internal::StreamFaultForTests::SeekFails) {
            return MA_IO_ERROR;
        }
        return ma_decoder_seek_to_pcm_frame(&decoder, frame);
    }

    // The stream cannot go on: the worker decodes nothing more for it until
    // the next seek request. The mixer drains what the ring holds; then a
    // voice ends, whether looping or not.
    // Logged as an error once per halt: the audio stops for a reason the
    // app cannot see otherwise. The worker, or the re-init migration before
    // it registers the instance. endOfStream publishes halted to the mixer.
    void halt(const std::string& why) {
        halted.store(true, std::memory_order_relaxed);
        endOfStream.store(true, std::memory_order_release);
        logError("SoundStream") << pathUtf8 << ": " << why << "; the stream ends here";
    }

    // One seek point per second of audio, at least 1, at most 1024 (see
    // openDecoder()).
    static ma_uint32 mp3SeekPointCount(float durationSec) {
        constexpr ma_uint32 kMax = 1024;
        if (!(durationSec > 1.0f)) return 1;
        if (durationSec >= (float)kMax) return kMax;
        return (ma_uint32)std::ceil(durationSec);
    }

    ~StreamInstance() {
        if (decoderInitialized) {
            ma_decoder_uninit(&decoder);
            decoderInitialized = false;
        }
    }

    // Frames available for read (monotonic, never overflows in practice
    // — uint64 at 96 kHz lasts ~6 million years).
    // Worker side. Acquire on readFrame: the mixer's reads of the slots it
    // released happen before the worker overwrites them.
    uint64_t available() const {
        return writeFrame.load(std::memory_order_relaxed)
             - readFrame.load(std::memory_order_acquire);
    }
    uint64_t space() const { return RING_FRAMES - available(); }
};

} // namespace internal

#if defined(_WIN32) && !defined(CREATE_WAITABLE_TIMER_HIGH_RESOLUTION)
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002   // Windows SDK 10.0.17134+
#endif

// Implementation detail: the rest of this TU refers to StreamInstance
// unqualified. (This is a .cpp, not a public header.)
using internal::StreamInstance;

// ---------------------------------------------------------------------------
// StreamWorker — single thread + condition variable; refills any registered
// StreamInstance whose ring has room for another chunk, and serves seek
// requests (#280).
//
// It sleeps while there is no such work (#447): its wait ends on stop, when
// some stream needs work (hasWork()), or after the poll interval, and it
// runs a pass at every timeout too. The poll is 5 ms while a stream is live
// (the mixer drained a ring, or a seek was pending, within the last
// kIdleAfter), so a refill is never more than 5 ms late, and 50 ms
// otherwise (#550), so an idle app does not wake the worker 200 times a
// second. A registration, a seek request and stop() notify it. The mixer
// notifies it once per seek it applies (the ring then turns from full of
// old data to empty at once, and the worker can only write the new data
// after that), when a callback takes a ring below half full, and when it
// drains a ring while the worker is on its idle poll (a voice that starts,
// resumes or leaves speed 0); that wait also ends when it sees a ring
// drained. It does not notify at each refill threshold: a ring that is due
// a refill (room for 1024 frames of 16384) still holds ~320 ms at 48 kHz,
// so the timeout picks it up in time. A disposed stream leaves the list at
// the worker's next look.
//
// On Windows the timed wait is a high-resolution waitable timer (Windows 10
// 1803+, as in internal::HeadlessSleeper and the sokol run loop, #488), so
// the 5 ms poll is 5 ms and not the 15.6 ms system timer tick that
// condition_variable::wait_for rounds it to. The worker then waits on the
// timer and an auto-reset event that the notifications set; without the
// timer it falls back to the condition variable.
// ---------------------------------------------------------------------------
class StreamWorker {
public:
    static StreamWorker& getInstance() {
        // Never destroyed, only stopped: the engine is never destroyed
        // either, and a device left running at exit (a headless app) still
        // calls the mixer, which may wake the worker (wakeFromMixer()) after
        // the static destructors ran. The Stopper joins the thread at the
        // point this static was destroyed before.
        static StreamWorker* instance = new StreamWorker();
        static Stopper stopper{*instance};
        return *instance;
    }

    void registerStream(std::weak_ptr<StreamInstance> w) {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_.push_back(std::move(w));
        ensureRunningLocked();
        signal();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cv_.notify_all();
            signalEvent();
        }
        if (thread_.joinable()) thread_.join();
    }

    // A stream has new work (a seek request was posted): end the worker's
    // wait. Taking the lock after the caller's store orders that store
    // before the worker's next look at hasWork(), so the wakeup is not lost
    // between its check and its wait. Do not call it under the engine lock.
    void wake() {
        { std::lock_guard<std::mutex> lock(mutex_); }
        signal();
    }

    // The mixer emptied a stream's ring (it applied a seek) or took it below
    // half full. From the audio callback, so without the lock (the worker
    // can hold it while it closes a stream's file): a wakeup that comes
    // between the worker's check and its wait is lost (not on Windows with
    // the timer: the event keeps it), and the poll covers it.
    void wakeFromMixer() { signal(); }

    // The mixer drained a stream's ring. From the audio callback, without
    // the lock: one atomic load while the worker is on its 5 ms poll. On
    // the idle poll, wake the worker once so that it switches to the 5 ms
    // poll now rather than up to 50 ms later (a lost wakeup is covered by
    // the half-full wake and then the 50 ms poll).
    void noteDrainedFromMixer() {
        if (idlePoll_.load(std::memory_order_relaxed)
            && idlePoll_.exchange(false, std::memory_order_relaxed)) {
            signal();
        }
    }

private:
    StreamWorker() {
#ifdef _WIN32
        // nullptr before Windows 10 1803, which rejects the flag: the
        // worker then waits on cv_ as elsewhere.
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
                                        TIMER_MODIFY_STATE | SYNCHRONIZE);
        if (timer_) {
            event_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);   // auto-reset
            if (!event_) {
                CloseHandle(timer_);
                timer_ = nullptr;
            }
        }
#endif
    }
    ~StreamWorker() = default;   // never called (see getInstance())

    // The poll while a stream is live, and while none is.
    static constexpr std::chrono::milliseconds kLivePoll{5};
    static constexpr std::chrono::milliseconds kIdlePoll{50};
    // A stream stays live this long after the worker last saw its ring
    // drained or a seek pending: longer than the callback period, so a
    // device that calls back every 10-20 ms keeps the 5 ms poll between
    // its callbacks.
    static constexpr std::chrono::milliseconds kIdleAfter{250};

    // End the worker's wait.
    void signal() {
        cv_.notify_one();
        signalEvent();
    }

    void signalEvent() {
#ifdef _WIN32
        if (event_) SetEvent(event_);
#endif
    }

    // Wait until pred() or for `timeout`, like cv_.wait_for(lock, timeout,
    // pred). On Windows on the high-resolution timer (see the class comment).
    template <class Pred>
    void waitFor(std::unique_lock<std::mutex>& lock, std::chrono::milliseconds timeout,
                 Pred pred) {
#ifdef _WIN32
        if (timer_) {
            if (pred()) return;
            LARGE_INTEGER due;
            due.QuadPart = -(LONGLONG)timeout.count() * 10000;   // relative, 100 ns units
            if (SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) {
                HANDLE handles[2] = {event_, timer_};
                while (true) {
                    lock.unlock();
                    const DWORD r = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                    lock.lock();
                    if (r == WAIT_OBJECT_0 + 1) return;   // timed out: run a pass
                    if (r != WAIT_OBJECT_0) break;        // the wait failed: cv_ below
                    if (pred()) return;
                }
            }
        }
#endif
        cv_.wait_for(lock, timeout, pred);
    }

    struct Stopper {
        StreamWorker& worker;
        ~Stopper() { worker.shutdown(); }
    };

    void ensureRunningLocked() {
        if (running_) return;
        stop_ = false;
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    // Decoder calls of the refill, with the test fault applied.
    static ma_result readFrames(StreamInstance& s, float* out, ma_uint64 frames,
                                ma_uint64* read) {
        const int fault = g_streamFault.load(std::memory_order_relaxed);
        if (fault == (int)internal::StreamFaultForTests::ReadFails) {
            *read = 0;
            return MA_IO_ERROR;
        }
        const ma_result r = ma_decoder_read_pcm_frames(&s.decoder, out, frames, read);
        if (fault == (int)internal::StreamFaultForTests::ReadFailsWithFrames && *read > 0) {
            return MA_IO_ERROR;
        }
        return r;
    }

    static ma_result seekDecoder(StreamInstance& s, uint64_t frame) {
        return s.seekDecoder(frame);
    }

    // See StreamInstance::halt().
    static void endOnError(StreamInstance& s, const std::string& why) {
        s.halt(why);
    }

    // Decoding goes in chunks of this many frames; a stream is due a refill
    // once its ring has room for one.
    static constexpr size_t kChunkFrames = 1024;

    // refillOne() and hasWork() share these checks, so that hasWork() is
    // true only when a pass does something for the stream (a true result
    // with nothing to do would spin the worker).

    // Whether the worker may touch the stream at all.
    static bool serviceable(const StreamInstance& s) {
        if (s.disposed.load(std::memory_order_acquire)) return false;
        if (!s.decoderInitialized) return false;
        const int fault = g_streamFault.load(std::memory_order_relaxed);
        if (fault == (int)internal::StreamFaultForTests::Stalls) {
            return false;   // test: a worker that falls behind (slow storage)
        }
        if (fault == (int)internal::StreamFaultForTests::MixerLags
            && !g_mixerLagReleased.load(std::memory_order_acquire)) {
            return false;   // test: held until the mixer has read the write position
        }
        return true;
    }

    // A seek request the worker can take now: a new one, and the mixer has
    // applied the previous seek (the published fields stay put until then).
    static bool seekTakeable(const StreamInstance& s) {
        return s.seekRequestSeq.load(std::memory_order_acquire) != s.seekServedSeq
            && s.seekAppliedEpoch.load(std::memory_order_acquire)
                   == s.seekEpoch.load(std::memory_order_relaxed);
    }

    // Whether the decoder can give more frames (after any seek is taken).
    static bool readable(const StreamInstance& s) {
        if (s.halted.load(std::memory_order_relaxed)) return false;
        return !s.decoderAtEnd || s.looping.load(std::memory_order_acquire);
    }

    // The wait predicate's test for one stream: a seek to take, or a ring
    // with room for a chunk that the decoder can fill. The worker thread,
    // under mutex_ (seekServedSeq and decoderAtEnd are the worker's;
    // the rest are atomics or fixed before registration).
    static bool hasWork(const StreamInstance& s) {
        if (!serviceable(s)) return false;
        if (seekTakeable(s)) return true;
        return readable(s) && s.space() >= kChunkFrames;
    }

    // Refill one stream up to roughly full. Honor seek requests first.
    void refillOne(StreamInstance& s) {
        if (!serviceable(s)) return;

        // True right after a seek to frame 0: a read that then returns no
        // frames means the stream is empty.
        bool atStart = false;

        // Take the latest seek request, once the mixer has applied the
        // previous one.
        if (seekTakeable(s)) {
            const uint64_t req = s.seekRequestSeq.load(std::memory_order_acquire);
            const uint64_t epoch = s.seekEpoch.load(std::memory_order_relaxed);
            const double target =
                s.clampSeekTarget(s.seekTargetFrame.load(std::memory_order_relaxed));
            const uint64_t frame = (uint64_t)target;
            s.seekServedSeq = req;
            s.halted.store(false, std::memory_order_relaxed);
            s.decoderAtEnd = false;
            const ma_result sr = seekDecoder(s, frame);
            // Publish before writing any post-seek data (see the section
            // comment). endOfStream is reset before the epoch, so a mixer
            // that sees the epoch does not see a stale end.
            s.endOfStream.store(false, std::memory_order_release);
            if (sr != MA_SUCCESS) {
                endOnError(s, "seek to frame " + std::to_string(frame) + " failed (result="
                              + std::to_string((int)sr) + ")");
            }
            s.seekBaseFrame.store(s.writeFrame.load(std::memory_order_relaxed),
                                  std::memory_order_relaxed);
            s.seekPublishedTarget.store(target, std::memory_order_relaxed);
            s.seekPublishedSeq.store(req, std::memory_order_relaxed);
            s.seekEpoch.store(epoch + 1, std::memory_order_release);
            atStart = (frame == 0);
        }
        if (!readable(s)) return;

        // Decode in chunks of up to scratchFrames frames at a time.
        constexpr size_t scratchFrames = kChunkFrames;
        float scratch[scratchFrames * StreamInstance::CHANNELS];

        while (!s.disposed.load(std::memory_order_acquire)
               && !stop_.load(std::memory_order_relaxed)
               && s.space() >= scratchFrames) {
            ma_uint64 read = 0;
            const ma_result r = readFrames(s, scratch, scratchFrames, &read);
            const bool readFailed = (r != MA_SUCCESS && r != MA_AT_END);
            if (readFailed && read == 0) {
                endOnError(s, "decoder read failed (result=" + std::to_string((int)r) + ")");
                break;
            }
            if (read == 0) {
                if (atStart) {
                    endOnError(s, "no frames to read from the start of the file");
                    break;
                }
                if (s.looping.load(std::memory_order_acquire)) {
                    const ma_result sr = seekDecoder(s, 0);
                    if (sr != MA_SUCCESS) {
                        endOnError(s, "seek to the start for the loop failed (result="
                                      + std::to_string((int)sr) + ")");
                        break;
                    }
                    atStart = true;
                    s.decoderAtEnd = false;
                    continue;
                }
                s.decoderAtEnd = true;
                s.endOfStream.store(true, std::memory_order_release);
                break;
            }
            atStart = false;

            // Copy `read` frames into the ring, splitting on wrap.
            uint64_t w = s.writeFrame.load(std::memory_order_relaxed);
            size_t   start = (size_t)(w & StreamInstance::RING_MASK);
            size_t   first = std::min<size_t>(read, StreamInstance::RING_FRAMES - start);
            std::memcpy(&s.ring[start * StreamInstance::CHANNELS],
                        scratch,
                        first * StreamInstance::CHANNELS * sizeof(float));
            if (first < read) {
                std::memcpy(&s.ring[0],
                            scratch + first * StreamInstance::CHANNELS,
                            (read - first) * StreamInstance::CHANNELS * sizeof(float));
            }
            s.writeFrame.store(w + read, std::memory_order_release);
            if (readFailed) {
                // End only now that the frames this read returned are
                // published: an underrunning mixer that saw endOfStream first
                // would end the voice without playing them.
                endOnError(s, "decoder read failed (result=" + std::to_string((int)r) + ")");
                break;
            }

            if ((ma_uint64)read < (ma_uint64)scratchFrames) {
                // Short read = end of file. Next iteration's read=0 path
                // handles the EOS / loop bookkeeping; bail out for now.
                if (!s.looping.load(std::memory_order_acquire)) {
                    s.endOfStream.store(true, std::memory_order_release);
                }
                break;
            }
        }
    }

    // The wait predicate (besides stop): drop the entries of streams that
    // are gone or disposed, and say whether any other one has work. Under
    // mutex_.
    bool anyWorkLocked() {
        bool work = false;
        auto it = streams_.begin();
        while (it != streams_.end()) {
            auto sp = it->lock();
            if (!sp || sp->disposed.load(std::memory_order_acquire)) {
                it = streams_.erase(it);
                continue;
            }
            if (!work && hasWork(*sp)) work = true;
            ++it;
        }
        return work;
    }

    // Whether a stream was live since the last look: its ring was drained
    // (its read position moved) or a seek on it is pending. Also true for a
    // stream the worker sees for the first time. Updates what the worker
    // saw. Under mutex_.
    bool anyLiveLocked() {
        bool live = false;
        for (auto& w : streams_) {
            auto sp = w.lock();
            if (!sp) continue;
            StreamInstance& s = *sp;
            const uint64_t r = s.readFrame.load(std::memory_order_acquire);
            if (!s.workerSeen || r != s.workerSeenReadFrame
                || s.seekRequestSeq.load(std::memory_order_acquire)
                       != s.seekAppliedSeq.load(std::memory_order_acquire)) {
                live = true;
            }
            s.workerSeen = true;
            s.workerSeenReadFrame = r;
        }
        return live;
    }

    // The idle wait's extra predicate: a ring was drained since the last
    // look. Under mutex_.
    bool anyDrainedLocked() const {
        for (auto& w : streams_) {
            auto sp = w.lock();
            if (sp && sp->workerSeen
                && sp->readFrame.load(std::memory_order_acquire) != sp->workerSeenReadFrame) {
                return true;
            }
        }
        return false;
    }

    void run() {
        std::vector<std::shared_ptr<StreamInstance>> live;
        std::unique_lock<std::mutex> lock(mutex_);
        auto lastLive = std::chrono::steady_clock::now();
        while (true) {
            // Sleep until a stream has work, or for at most the poll: the
            // pass below then runs anyway (see the class comment).
            const auto now = std::chrono::steady_clock::now();
            if (anyLiveLocked()) lastLive = now;
            if (now - lastLive < kIdleAfter) {
                idlePoll_.store(false, std::memory_order_relaxed);
                waitFor(lock, kLivePoll, [this] { return stop_.load() || anyWorkLocked(); });
            } else {
                // Before the wait's first look, so that a drain after it
                // notifies (see noteDrainedFromMixer()).
                idlePoll_.store(true, std::memory_order_relaxed);
                waitFor(lock, kIdlePoll, [this] {
                    return stop_.load() || anyWorkLocked() || anyDrainedLocked();
                });
            }
            if (stop_.load()) break;
            g_streamWorkerPasses.fetch_add(1, std::memory_order_relaxed);

            // Snapshot strong refs while holding the lock, then drop the
            // lock for the actual decode work (which calls into miniaudio
            // and shouldn't block other registrations).
            live.reserve(streams_.size());
            for (auto& w : streams_) {
                if (auto sp = w.lock()) live.push_back(std::move(sp));
            }
            lock.unlock();

            for (auto& sp : live) {
                refillOne(*sp);
            }
            // Drop the refs before taking the lock again: a stream whose last
            // other owner let it go meanwhile closes its decoder here.
            live.clear();
            lock.lock();
        }
        running_ = false;
    }

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::weak_ptr<StreamInstance>> streams_;
    std::atomic<bool> stop_{false};   // written under mutex_; refillOne() polls it
    bool running_ = false;            // under mutex_
    // The worker is on its idle poll; the mixer clears it when it wakes it.
    std::atomic<bool> idlePoll_{false};
#ifdef _WIN32
    HANDLE timer_ = nullptr;   // high-resolution waitable timer, or nullptr
    HANDLE event_ = nullptr;   // auto-reset; set with timer_
#endif
};

// ---------------------------------------------------------------------------
// SoundStream::loadStream — open the file once to validate format and
// query duration/channels/sampleRate. Per-voice decoders are opened
// lazily by AudioEngine::play().
// ---------------------------------------------------------------------------
LoadResult SoundStream::loadStream(const fs::path& path, int maxPolyphony) {
    if (maxPolyphony < 1) maxPolyphony = 1;

    // Detect format by extension. We don't go through stb_vorbis here —
    // OGG support for streaming would need a separate code path. WAV /
    // MP3 / FLAC are routed through ma_decoder, which handles all three
    // with the same API.
    std::string ext = toLower(getFileExtension(path));

    ma_encoding_format fmt = ma_encoding_format_unknown;
    if (ext == "wav")       fmt = ma_encoding_format_wav;
    else if (ext == "mp3")  fmt = ma_encoding_format_mp3;
    else if (ext == "flac") fmt = ma_encoding_format_flac;
    else {
        // TODO(streaming): OGG Vorbis is reachable via stb_vorbis (already
        // bundled at core/include/stb_vorbis.c and used by SoundBuffer's
        // eager loader). To enable OGG streaming, register stb_vorbis as
        // a custom ma_decoder backend through ma_decoding_backend_vtable
        // (miniaudio provides reference impls in extras/miniaudio_libvorbis.h).
        // Roughly half a day of work; tracked separately from this refactor.
        // AAC is platform-specific (AudioToolbox / MediaFoundation / GStreamer)
        // and streaming would need per-platform plumbing — lower priority.
        logError("SoundStream") << "unsupported extension for streaming '." << ext
                                << "' (use load() for full decode)";
        return LoadResult::fail(LoadError::UnsupportedFormat,
                                "unsupported extension for streaming '." + ext +
                                "' (use load() for full decode)");
    }

    // Classify missing files before handing the path to miniaudio (whose
    // error codes don't distinguish the two cases cheaply).
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        logError("SoundStream") << "file not found: " << internal::pathToUtf8(path);
        return LoadResult::fail(LoadError::FileNotFound,
                                "file not found: " + internal::pathToUtf8(path));
    }

    // Probe decode: open, query, close. Per-voice decoders re-open later.
    // The decoder is configured to output at the engine's runtime sample
    // rate so the mixer can memcpy without resampling.
    ma_decoder probe;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32,
                                                    StreamInstance::CHANNELS,
                                                    AudioEngine::getInstance().getSampleRate());
    cfg.encodingFormat = fmt;
    ma_result r = maDecoderInitPathA(path, &cfg, &probe);
    if (r != MA_SUCCESS) {
        logError("SoundStream") << "failed to open " << internal::pathToUtf8(path)
                                << " (result=" << (int)r << ")";
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to open " + internal::pathToUtf8(path) +
                                " (result=" + std::to_string((int)r) + ")");
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&probe, &totalFrames);
    ma_uint64 probed = totalFrames;
    if (totalFrames == 0) {
        // A length of 0 can also mean "unknown" (a FLAC whose STREAMINFO
        // leaves the total at 0, e.g. one encoded to a pipe): decide on a
        // read. Such a file streams with a duration of 0.
        float frames[16 * StreamInstance::CHANNELS];
        ma_decoder_read_pcm_frames(&probe, frames, 16, &probed);
    }
    if (probed == 0) {
        // Nothing to play (e.g. a WAV with an empty data chunk), like an
        // eager load() that decodes no samples.
        ma_decoder_uninit(&probe);
        logError("SoundStream") << "no audio frames in " << internal::pathToUtf8(path);
        return LoadResult::fail(LoadError::DecodeFailed,
                                "no audio frames in " + internal::pathToUtf8(path));
    }
    channels = (int)probe.outputChannels;
    sampleRate = (int)probe.outputSampleRate;
    duration_ = (sampleRate > 0)
                ? (float)((double)totalFrames / (double)sampleRate)
                : 0.0f;
    ma_decoder_uninit(&probe);

    path_ = path;
    maxPolyphony_ = maxPolyphony;
    encodingFormatHint_ = (int)fmt;

    logVerbose("SoundStream") << "ready " << internal::pathToUtf8(path) << " (" << channels
                              << " ch, " << sampleRate << " Hz, " << duration_
                              << " s, maxPolyphony=" << maxPolyphony << ")";
    return LoadResult::success();
}

// ---------------------------------------------------------------------------
// AudioEngine::play(SoundSource) — unified entry point for both eager
// SoundBuffer and streaming SoundStream sources.
// ---------------------------------------------------------------------------
std::shared_ptr<PlayingSound> AudioEngine::play(std::shared_ptr<SoundSource> source) {
    if (!source) return nullptr;
    if (!initialized_) {
        noteDroppedPlay(DropReason::NotRunning, source.get());
        return nullptr;
    }

    // For streams: also build a StreamInstance up-front so when we hand
    // the slot back the caller can already start consuming frames.
    std::shared_ptr<StreamInstance> stream;
    if (source->kind() == SoundSource::Stream) {
        auto* s = static_cast<SoundStream*>(source.get());

        // Count active stream voices for this same source — refuse to
        // exceed maxPolyphony. Walking the slot list is O(MAX_PLAYING_SOUNDS)
        // but MAX is small (32) so this is fine in practice.
        int active = 0;
        for (auto& slot : playingSounds_) {
            if (slot && slot->playing && slot->buffer.get() == s) ++active;
        }
        if (active >= s->getMaxPolyphony()) {
            // Reject. A single Sound never gets here: Sound::play() stops its
            // own previous voice before calling us. Only copies of a Sound,
            // which share one SoundStream, can exceed its maxPolyphony.
            noteDroppedPlay(DropReason::StreamLimit, s);
            return nullptr;
        }

        // Open a fresh decoder for this voice.
        stream = std::make_shared<StreamInstance>();
        ma_result r = stream->openDecoder(*s, (ma_uint32)sampleRate_);
        if (r != MA_SUCCESS) {
            noteDroppedPlay(DropReason::DecoderError, s, (int)r);
            return nullptr;
        }

        StreamWorker::getInstance().registerStream(stream);
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);

        for (auto& slot : playingSounds_) {
            if (!slot || !slot->playing) {
                slot = std::make_shared<PlayingSound>();
                slot->buffer = source;
                slot->stream = stream;
                slot->positionF = 0.0;
                // A stream's positionF counts frames of its decoder's output,
                // at the engine rate; an eager voice's counts source frames.
                slot->positionRateHz_.store(
                    source->kind() == SoundSource::Stream ? sampleRate_ : source->sampleRate,
                    std::memory_order_relaxed);
                slot->volume = 1.0f;
                slot->pan = 0.0f;
                slot->speed = 1.0f;
                slot->loop = false;
                slot->playing = true;
                slot->paused = false;
                // For streams the decoder already resampled to engine rate, so
                // rateRatio = 1.0 (no pitch adjust). For eager buffers, retain
                // the existing buffer/engine ratio compensation.
                if (source->kind() == SoundSource::Eager) {
                    slot->rateRatio = (source->sampleRate > 0 && sampleRate_ > 0)
                        ? ((float)source->sampleRate / (float)sampleRate_)
                        : 1.0f;
                } else {
                    slot->rateRatio = 1.0f;
                }
                return slot;
            }
        }
    }  // engine lock released: the drop is logged without stalling the mixer

    // Mark the just-opened stream as disposed so the worker drops it.
    if (stream) stream->disposed.store(true, std::memory_order_release);
    noteDroppedPlay(DropReason::PolyphonyLimit, source.get());
    return nullptr;
}

// ---------------------------------------------------------------------------
// mixStreamVoice — consume frames from the per-voice ring buffer.
//
// The mixer is invoked with the global engine lock held (mutex_), so we
// don't need to lock the stream itself — the SPSC ring uses atomic
// indices for synchronization with the worker.
//
// speed support:
//   - [0, 10] is allowed. Sound::setSpeed already clamps negatives to 0
//     because reverse streaming isn't implemented yet (needs direction-
//     aware ring fill).
//   - subFrame (double) tracks the fractional ring position. Per output
//     frame we lerp ring[i] and ring[i+1] by subFrame, then advance
//     subFrame by `speed`. Each whole unit of subFrame consumes one
//     ring frame.
//   - speed = 0 keeps subFrame fixed → same sample emitted = freeze.
//   - speed > 1 consumes the ring N× faster, raising underrun risk on
//     slow hardware. Worker decode is far faster than realtime on
//     modern CPUs for WAV / MP3 / FLAC, so this is usually fine.
// ---------------------------------------------------------------------------
void AudioEngine::mixStreamVoice(PlayingSound& sound, SoundStream& src,
                                 float* buffer, int num_frames, int num_channels) {
    auto& stream = sound.stream;
    if (!stream) return;

    // Honor loop flag changes mid-playback (worker reads `looping` atomically).
    stream->looping.store(sound.loop.load(), std::memory_order_release);

    float vol  = sound.volume;
    float pan  = sound.pan;
    float panL = (pan <= 0.0f) ? 1.0f : (1.0f - pan);
    float panR = (pan >= 0.0f) ? 1.0f : (1.0f + pan);

    // Defensive: setSpeed clamps to [0, 10] for streams, but read again here
    // in case a stale value snuck through.
    double speed = (double)sound.speed.load();
    if (speed < 0.0) speed = 0.0;
    if (speed > 10.0) speed = 10.0;

    // Snapshot routing state for this callback. Stream ring is always 2ch
    // (the per-voice decoder is configured to output stereo), so routing
    // operates on src.channels = StreamInstance::CHANNELS.
    auto map = internal::sharedLoad(sound.channelMap);
    auto gains = internal::sharedLoad(sound.channelGains);
    int mm = sound.mixMode.load(std::memory_order_acquire);
    const int srcCh = StreamInstance::CHANNELS;
    const int mapSize = map ? (int)map->size() : 0;
    const int gainsSize = gains ? (int)gains->size() : 0;

    // writeFrame first, then the seek epoch: a writeFrame that covers
    // post-seek data implies the epoch that precedes it is visible (see the
    // seek protocol in the section comment).
    uint64_t writeFrame = stream->writeFrame.load(std::memory_order_acquire);
    if (g_streamFault.load(std::memory_order_relaxed)
            == (int)internal::StreamFaultForTests::MixerLags
        && stream->seekRequestSeq.load(std::memory_order_acquire)
            != stream->seekAppliedSeq.load(std::memory_order_relaxed)) {
        // Test: the audio thread preempted right here while the worker
        // serves the seek and writes on past the writeFrame just read, up
        // to the stream's end.
        g_mixerLagReleased.store(true, std::memory_order_release);
        for (int i = 0; i < 50; ++i) {
            if (stream->seekEpoch.load(std::memory_order_acquire)
                    != stream->seekAppliedEpoch.load(std::memory_order_relaxed)
                && stream->endOfStream.load(std::memory_order_acquire)) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    const uint64_t epoch = stream->seekEpoch.load(std::memory_order_acquire);
    if (epoch != stream->seekAppliedEpoch.load(std::memory_order_relaxed)) {
        // The worker seeked: skip what is left of the old data and play
        // from where the post-seek data starts. Never backwards: after an
        // underrun at speed > 1 readFrame can be a few frames past the base,
        // and the worker bounded its writes by that readFrame, so moving it
        // back would let the ring hold more than RING_FRAMES.
        const uint64_t base = stream->seekBaseFrame.load(std::memory_order_relaxed);
        const uint64_t current = stream->readFrame.load(std::memory_order_relaxed);
        stream->readFrame.store(std::max(base, current), std::memory_order_release);
        stream->subFrame = 0.0;
        sound.positionF = stream->seekPublishedTarget.load(std::memory_order_relaxed);
        const uint64_t servedSeq = stream->seekPublishedSeq.load(std::memory_order_relaxed);
        stream->seekAppliedEpoch.store(epoch, std::memory_order_release);
        stream->seekAppliedSeq.store(servedSeq, std::memory_order_release);   // not pending
        // The ring is empty from here: the worker can write the new data now.
        StreamWorker::getInstance().wakeFromMixer();
    }
    if (stream->seekRequestSeq.load(std::memory_order_acquire)
            != stream->seekAppliedSeq.load(std::memory_order_relaxed)) {
        // A seek is on its way: play nothing more of the position it
        // replaces (nor end the voice at its end).
        sound.level.store(0.0f, std::memory_order_relaxed);
        return;
    }

    uint64_t readFrame  = stream->readFrame.load(std::memory_order_relaxed);
    const uint64_t readStart = readFrame;
    double   subFrame   = stream->subFrame;

    int produced = 0;
    float level = 0.0f;       // peak of this voice's contribution (diagnostics)
    double posAdvance = 0.0;  // sum of consumed ring frames this callback

    for (int frame = 0; frame < num_frames; ++frame) {
        // Need both readFrame and readFrame+1 for interpolation.
        if (readFrame + 1 >= writeFrame) {
            if (!stream->endOfStream.load(std::memory_order_acquire)
                || (sound.loop.load() && !stream->halted.load(std::memory_order_relaxed))) {
                // Underrun: emit nothing for this output frame, give the
                // worker a chance to catch up. subFrame state preserved.
                continue;
            }
            // The end. writeFrame was read at the top of the callback, and
            // the worker stores writeFrame before endOfStream, so a fresh
            // read sees every frame it wrote before it ended (the tail
            // after a seek near the end, say): play those first.
            const uint64_t latest = stream->writeFrame.load(std::memory_order_acquire);
            if (stream->seekEpoch.load(std::memory_order_acquire) != epoch) {
                // A later seek was served meanwhile, so the endOfStream seen
                // may be the one it reset and `latest` may cover its data:
                // stop here, the next callback applies the seek.
                break;
            }
            if (readFrame + 1 >= latest) {
                sound.playing = false;
                break;
            }
            writeFrame = latest;
        }

        size_t idx0 = (size_t)(readFrame & StreamInstance::RING_MASK)
                      * StreamInstance::CHANNELS;
        size_t idx1 = (size_t)((readFrame + 1) & StreamInstance::RING_MASK)
                      * StreamInstance::CHANNELS;
        float frac = (float)subFrame;

        // Pull interpolated sample for ring channel s (0 = L, 1 = R).
        auto srcAt = [&](int s) -> float {
            if (s < 0 || s >= srcCh) return 0.0f;
            float a = stream->ring[idx0 + (size_t)s];
            float b = stream->ring[idx1 + (size_t)s];
            return a + (b - a) * frac;
        };

        // Per-output-channel routing (same shape as mixEagerVoice).
        for (int c = 0; c < num_channels; c++) {
            float sample = 0.0f;

            if (mapSize > 0) {
                if (c < mapSize) {
                    for (int s : (*map)[c]) sample += srcAt(s);
                }
            } else if (mm == (int)MixMode::DownmixMono) {
                for (int s = 0; s < srcCh; s++) sample += srcAt(s);
                sample /= (float)srcCh;
            } else {
                // Auto: ring is always stereo, so multi-ch rules apply.
                sample = (c < srcCh) ? srcAt(c) : 0.0f;
            }

            float gain = (c < gainsSize) ? (*gains)[c] : 1.0f;
            float panMul = (c == 0) ? panL : ((c == 1) ? panR : 1.0f);

            float out = sample * gain * panMul * vol;
            buffer[frame * num_channels + c] += out;
            float mag = std::fabs(out);
            if (mag > level) level = mag;
        }

        subFrame += speed;
        // Each whole unit advances the integer ring read by 1.
        while (subFrame >= 1.0) {
            subFrame -= 1.0;
            readFrame += 1;
            posAdvance += 1.0;
        }
        ++produced;
    }

    stream->readFrame.store(readFrame, std::memory_order_release);
    stream->subFrame = subFrame;
    if (readFrame != readStart) StreamWorker::getInstance().noteDrainedFromMixer();

    // This callback took the ring below half full: wake the worker rather
    // than wait for its poll (which a coarse system timer stretches, 15.6 ms
    // on Windows without the high-resolution timer), e.g. at a high speed,
    // or for its 50 ms idle poll. Once per crossing, so a ring that stays
    // low (the end of the file) does not notify again.
    // Signed: after an underrun at speed > 1 readFrame can be past it.
    constexpr int64_t kLowWater = (int64_t)(StreamInstance::RING_FRAMES / 2);
    if ((int64_t)(writeFrame - readStart) >= kLowWater
        && (int64_t)(writeFrame - readFrame) < kLowWater) {
        StreamWorker::getInstance().wakeFromMixer();
    }
    sound.level.store(level, std::memory_order_relaxed);

    // positionF advances by the actual ring frames consumed (which equals
    // produced output frames * average speed). When speed = 0, posAdvance
    // stays 0 and the position freezes alongside the audio.
    //
    // When looping, wrap positionF by the file's total engine-rate frame
    // count so getPosition() cycles like the eager path does.
    if (posAdvance > 0.0) {
        sound.positionF += posAdvance;
        if (sound.loop.load() && stream->totalFramesInFile > 0) {
            double total = (double)stream->totalFramesInFile;
            if (sound.positionF >= total) {
                sound.positionF = std::fmod(sound.positionF, total);
            }
        }
    }
    (void)produced;  // kept for potential telemetry; not used at present
}

// ---------------------------------------------------------------------------
// Seek / position of a voice (Sound::setPosition() / getPosition())
// ---------------------------------------------------------------------------
namespace internal {

void seekVoice(PlayingSound& voice, double frame) {
    if (!(frame >= 0.0)) frame = 0.0;   // also NaN
    AudioEngine& engine = AudioEngine::getInstance();
    std::string refusedPath;   // logged after the engine lock is released
    bool requested = false;    // the worker is woken after the engine lock is released
    {
        std::lock_guard<std::mutex> lock(engine.mutex_);
        if (!voice.buffer || voice.buffer->kind() != SoundSource::Stream) {
            voice.positionF = frame;
            return;
        }
        // A request only; the mixer moves positionF when it applies it.
        if (!voice.stream) return;   // the voice lost its decoder (re-init failed)
        StreamInstance& s = *voice.stream;
        if (s.totalFramesInFile > 0) {
            s.seekTargetFrame.store(frame, std::memory_order_relaxed);
            s.seekRequestSeq.fetch_add(1, std::memory_order_release);
            requested = true;
        } else {
            // The length is unknown (e.g. a FLAC whose STREAMINFO total is
            // 0): there is no known end to clamp the target to, so such a
            // stream does not seek. Logged once per voice.
            if (s.unknownLengthSeekLogged) return;
            s.unknownLengthSeekLogged = true;
            refusedPath = s.pathUtf8;
        }
    }
    if (requested) {
        // The worker sleeps while no stream needs it (#447).
        StreamWorker::getInstance().wake();
        return;
    }
    logWarning("SoundStream") << refusedPath
                              << ": setPosition() is ignored, the stream's length is unknown";
}

double voicePosition(const PlayingSound& voice) {
    AudioEngine& engine = AudioEngine::getInstance();
    std::lock_guard<std::mutex> lock(engine.mutex_);
    if (voice.stream) {
        const StreamInstance& s = *voice.stream;
        if (s.seekRequestSeq.load(std::memory_order_acquire)
                != s.seekAppliedSeq.load(std::memory_order_acquire)) {
            return s.seekTargetFrame.load(std::memory_order_relaxed);
        }
    }
    return voice.positionF;
}

void releaseVoice(PlayingSound& voice) {
    AudioEngine& engine = AudioEngine::getInstance();
    std::shared_ptr<StreamInstance> stream;
    {
        // Under the engine lock: the mixer reads voice.stream under it.
        std::lock_guard<std::mutex> lock(engine.mutex_);
        voice.playing = false;
        voice.paused = false;
        stream = std::move(voice.stream);
        voice.stream.reset();
    }
    // The worker skips a disposed stream. The decoder and file close when
    // the last reference goes: here, or in the worker if it is refilling
    // this stream right now. Not under the engine lock, so the mixer does
    // not wait for the file to close.
    if (stream) stream->disposed.store(true, std::memory_order_release);
}

} // namespace internal

// ---------------------------------------------------------------------------
// AudioEngine miniaudio callback
// ---------------------------------------------------------------------------
static void playbackDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pInput;  // Playback only, input not used

    AudioEngine* engine = static_cast<AudioEngine*>(pDevice->pUserData);
    if (engine && pOutput) {
        engine->mixAudio(static_cast<float*>(pOutput), frameCount, pDevice->playback.channels);
    }
}

// ---------------------------------------------------------------------------
// AudioEngine implementation
// ---------------------------------------------------------------------------

AudioEngine& AudioEngine::getInstance() {
    // Intentionally leaked. A plain function-local static registers its
    // destructor against the __dso_handle of the image whose code runs the
    // first call — under hot reload that used to be the guest dylib (when this
    // was header-inline), so dlclose() of an old guest destroyed the engine the
    // host was still using (the next listen() then died on the destroyed Event
    // mutex). The heap instance has no exit-time destructor; the framework
    // cleanup path calls shutdown() explicitly for a clean device stop on
    // normal exit.
    static AudioEngine* instance = new AudioEngine();
    return *instance;
}

bool AudioEngine::init() {
    // Zero-arg path: use whatever's currently in the runtime fields
    // (defaults unless someone wrote to them first via init(settings)).
    return init(AudioSettings{
        .sampleRate   = sampleRate_,
        .channels     = channels_,
        .bufferSize   = bufferSize_,
        .maxPolyphony = (int)playingSounds_.size(),
        .deviceName   = std::string(),
    });
}

bool AudioEngine::init(const AudioSettings& settings) {
    const bool reinit = initialized_;
    const int  oldRate = sampleRate_;

    if (reinit) {
        // Live re-init: stop the device, swap settings, migrate voices to
        // the new rate, then start a fresh device. The device-down window
        // is the source of a short audible gap (~30-100 ms) but voices
        // preserve their playback position.
        logNotice("AudioEngine") << "re-initializing (" << sampleRate_ << " Hz, " << channels_
                                 << " ch -> " << settings.sampleRate << " Hz, "
                                 << settings.channels << " ch, dev='" << settings.deviceName << "')";

        if (device_) {
            ma_device* device = static_cast<ma_device*>(device_);
            // ma_device_uninit handles stopping internally and is more
            // reliable than the explicit stop+uninit dance on CoreAudio.
            // The second ma_device_stop in a tight re-init cycle hangs on
            // macOS waiting for the audio thread to join; uninit alone
            // releases everything in one shot.
            ma_device_uninit(device);
            delete device;
            device_ = nullptr;
        }
        initialized_ = false;
    }

    // No device runs here: drop the previous device's meters (also when the
    // init below fails, so a dead engine never reports old output).
    resetMeters();

    // Commit settings to runtime fields BEFORE migration / device init so
    // anyone reading getSampleRate() during this window sees the new value.
    sampleRate_ = settings.sampleRate > 0 ? settings.sampleRate : DEFAULT_SAMPLE_RATE;
    channels_   = settings.channels   > 0 ? settings.channels   : DEFAULT_CHANNELS;
    bufferSize_ = settings.bufferSize  > 0 ? settings.bufferSize : DEFAULT_BUFFER_SIZE;

    int polyphony = settings.maxPolyphony > 0
                  ? settings.maxPolyphony
                  : DEFAULT_MAX_PLAYING_SOUNDS;
    if ((int)playingSounds_.size() != polyphony) {
        // Preserve existing slots up to the new size; truncating drops the
        // oldest extra voices (which is the same policy as play() reusing
        // dead slots — losing them is unavoidable if the user shrinks the
        // pool while voices are active).
        playingSounds_.resize(polyphony);
    }

    // Re-init only: migrate active voices so they continue from the same
    // playback position at the new engine rate. The device is currently
    // stopped, so the audio thread can't observe partial migration state.
    if (reinit) {
        migrateVoicesToNewRate(oldRate, sampleRate_);
    }

    // Lazily create a persistent ma_context. Sharing one context across
    // every device init/uninit cycle keeps CoreAudio's internal state
    // consistent on macOS — without it, the second ma_device_uninit in a
    // tight cycle hangs waiting for the audio thread to join.
    if (!context_) {
        ma_context* ctx = new ma_context();
        ma_result ctxResult = initContext(ctx);
        if (ctxResult != MA_SUCCESS) {
            logError("AudioEngine") << "no audio backend available (ma_context_init result="
                                    << (int)ctxResult
                                    << (settings.deviceName.empty() ? std::string()
                                        : ", requested device '" + settings.deviceName + "'")
                                    << "); sounds will not play";
            delete ctx;
            return false;
        }
        context_ = ctx;
        g_engineNullBackendRequested = g_nullBackendForTests.load(std::memory_order_relaxed);
    }
    ma_context* ctxArg = static_cast<ma_context*>(context_);

    // Device selection — if the caller specified a deviceName, look it up
    // via the persistent context. Empty string = system default.
    ma_device_id  selectedDeviceID;
    ma_device_id* deviceIDPtr = nullptr;
    if (!settings.deviceName.empty()) {
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(ctxArg, &infos, &count, NULL, NULL) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < count; ++i) {
                if (settings.deviceName == infos[i].name) {
                    selectedDeviceID = infos[i].id;
                    deviceIDPtr = &selectedDeviceID;
                    break;
                }
            }
        }
        if (!deviceIDPtr) {
            logWarning("AudioEngine") << "device '" << settings.deviceName
                                      << "' not found, using the system default";
        }
    }

    // How the failure messages below name the device: the requested one, or
    // the default it fell back to (#279).
    std::string deviceDesc = "the output device";
    if (!settings.deviceName.empty()) {
        deviceDesc = deviceIDPtr
            ? "the output device '" + settings.deviceName + "'"
            : "the system default output device (requested '" + settings.deviceName
              + "' was not found)";
    }

    ma_device* device = new ma_device();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format     = ma_format_f32;
    config.playback.channels   = channels_;
    config.playback.pDeviceID  = deviceIDPtr;
    config.sampleRate          = sampleRate_;
    config.dataCallback        = playbackDataCallback;
    config.pUserData           = this;
    if (bufferSize_ > 0) {
        config.periodSizeInFrames = bufferSize_;
    }

    ma_result result = ma_device_init(ctxArg, &config, device);
    if (result != MA_SUCCESS) {
        logError("AudioEngine") << "failed to initialize " << deviceDesc << " (result="
                                << (int)result << "); sounds will not play";
        delete device;
        return false;
    }

    result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        logError("AudioEngine") << "failed to start " << deviceDesc << " (result="
                                << (int)result << "); sounds will not play";
        ma_device_uninit(device);
        delete device;
        return false;
    }

    device_ = device;
    initialized_ = true;

    logNotice("AudioEngine") << "initialized (" << sampleRate_ << " Hz, " << channels_ << " ch, "
                             << playingSounds_.size() << " playback slots, "
                             << ma_get_backend_name(ctxArg->backend) << ": "
                             << device->playback.name << ")";

    // miniaudio's default backend order ends with the null backend, so with
    // no usable real backend init() still succeeds on a silent device.
    if (ctxArg->backend == ma_backend_null && !g_engineNullBackendRequested) {
        logWarning("AudioEngine") << "no usable audio backend; output is silent (miniaudio Null device)";
    }

    // Fire audioDeviceChanged with the resolved device's real info.
    // ma_device's playback.name is populated by ma_device_init even when
    // the caller didn't specify a device name (system default path).
    AudioDeviceChangedArgs args;
    args.deviceName   = std::string(device->playback.name);
    args.sampleRate   = sampleRate_;
    args.channels     = channels_;
    // The period the device runs with, in engine-rate frames: miniaudio
    // reports it at the device's native rate, while the event's sampleRate
    // is the engine rate, so bufferSize / sampleRate is the period in seconds.
    {
        const ma_uint32 period = device->playback.internalPeriodSizeInFrames;
        const ma_uint32 deviceRate = device->playback.internalSampleRate;
        args.bufferSize = (deviceRate > 0 && deviceRate != (ma_uint32)sampleRate_)
            ? (int)(((uint64_t)period * (uint64_t)sampleRate_ + deviceRate / 2) / deviceRate)
            : (int)period;
    }
    args.maxPolyphony = (int)playingSounds_.size();

    // Use the selection ID: playback.id may be zeroed for the default.
    {
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        if (deviceIDPtr && ma_context_get_devices(ctxArg, &infos, &count,
                                                NULL, NULL) != MA_SUCCESS) {
            infos = nullptr;
            count = 0;
        }
        args.isDefaultDevice = internal::openedDeviceIsDefault(deviceIDPtr, infos, count);
    }

    diag_->deviceIsDefault = args.isDefaultDevice;
    audioDeviceChanged.notify(args);

    return true;
}

std::vector<AudioDeviceInfo> AudioEngine::listDevices() {
    std::vector<AudioDeviceInfo> result;

    ma_context context;
    if (initContext(&context) != MA_SUCCESS) {
        return result;
    }

    ma_device_info* playbackInfos = nullptr;
    ma_uint32 playbackCount = 0;
    if (ma_context_get_devices(&context, &playbackInfos, &playbackCount,
                                NULL, NULL) == MA_SUCCESS) {
        result.reserve(playbackCount);
        for (ma_uint32 i = 0; i < playbackCount; ++i) {
            AudioDeviceInfo info;
            info.name      = playbackInfos[i].name;
            info.isDefault = (playbackInfos[i].isDefault != 0);
            result.push_back(std::move(info));
        }
    }

    ma_context_uninit(&context);
    return result;
}

// ---------------------------------------------------------------------------
// migrateVoicesToNewRate — keep active voices alive across an engine
// sample-rate change.
//
// Eager voices: positionF tracks source-rate frames, so it's unaffected
// by an engine rate change. We just recompute rateRatio = source_rate /
// new_engine_rate so each output frame advances posF by the right amount.
//
// Streaming voices: the per-voice ma_decoder was configured to OUTPUT at
// the old engine rate, and the ring holds samples at that rate. Both are
// stale. We rebuild the StreamInstance from scratch (new decoder
// configured at newRate), seek it to the same wall-clock playback time
// (a stream whose length is unknown restarts from the beginning instead),
// and rejoin the StreamWorker. The old StreamInstance is dropped — its
// destructor uninits the old decoder. Worker's weak_ptr to it stops
// locking and the entry self-evicts on the next iteration.
//
// Called with the device stopped, so no audio callback can race.
// ---------------------------------------------------------------------------
void AudioEngine::migrateVoicesToNewRate(int oldRate, int newRate) {
    if (oldRate == newRate || oldRate <= 0 || newRate <= 0) return;

    for (auto& slot : playingSounds_) {
        if (!slot || !slot->buffer) continue;
        if (!slot->playing && !slot->paused) {
            // An ended voice is not rebuilt (nothing plays it again). It
            // keeps positionF, a pending seek's target and positionRateHz_
            // at the old rate, so Sound::getPosition() reads the same time,
            // like an ended voice whose slot another play() took, which this
            // loop never sees.
            continue;
        }

        if (slot->buffer->kind() == SoundSource::Eager) {
            // Pitch-preserving rate change. positionF is source-rate frames.
            slot->rateRatio = (slot->buffer->sampleRate > 0)
                ? ((float)slot->buffer->sampleRate / (float)newRate)
                : 1.0f;
        } else {
            // Streaming voice — rebuild the per-voice decoder.
            auto* src = static_cast<SoundStream*>(slot->buffer.get());

            // Current playback time in seconds, from the rate positionF
            // counts: a pending seek's target, else the position played. The
            // new StreamInstance starts at that time with no request pending.
            int rate = slot->positionRateHz_.load(std::memory_order_relaxed);
            if (rate <= 0) rate = oldRate;
            double pos = slot->positionF;
            if (slot->stream) {
                const StreamInstance& old = *slot->stream;
                if (old.seekRequestSeq.load(std::memory_order_acquire)
                        != old.seekAppliedSeq.load(std::memory_order_acquire)) {
                    pos = old.seekTargetFrame.load(std::memory_order_relaxed);
                }
            }
            if (pos < 0.0) pos = 0.0;
            const double tSec = pos / (double)rate;

            // Build a fresh StreamInstance + decoder at the new rate.
            auto newStream = std::make_shared<StreamInstance>();
            ma_result r = (g_streamFault.load(std::memory_order_relaxed)
                               == (int)internal::StreamFaultForTests::ReopenFails)
                ? MA_IO_ERROR
                : newStream->openDecoder(*src, (ma_uint32)newRate);
            if (r != MA_SUCCESS) {
                logWarning("AudioEngine") << "stream playback migration failed for "
                                          << internal::pathToUtf8(src->getPath())
                                          << " (result=" << (int)r << "); stopping the playback";
                slot->playing = false;
                // The voice ends here and keeps its position at the old rate
                // (positionRateHz_ stays). The request dies with the stream,
                // so a pending target becomes the position getPosition()
                // reports, as it did before the re-init.
                slot->positionF = pos;
                // Drop the stale stream so its old decoder is destroyed.
                slot->stream.reset();
                continue;
            }

            double startFrame = 0.0;
            if (newStream->totalFramesInFile > 0) {
                // Seek to the same time in the new decoder's output frames,
                // clamped to the last frame like the worker's seeks (a target
                // past the end fails in dr_mp3). A failed seek leaves the
                // decoder at an unknown frame: halt the stream as the worker
                // does (one error log; a later setPosition() retries).
                startFrame = newStream->clampSeekTarget(tSec * (double)newRate);
                const ma_result sr = newStream->seekDecoder((uint64_t)startFrame);
                if (sr != MA_SUCCESS) {
                    newStream->halt("seek to frame " + std::to_string((uint64_t)startFrame)
                                    + " failed after an engine re-init (result="
                                    + std::to_string((int)sr) + ")");
                }
            }
            // An unknown length (e.g. a FLAC whose STREAMINFO total is 0)
            // does not seek (there is no end to clamp to, as in seekVoice()):
            // the new decoder starts the file from the beginning, and
            // positionF says so.

            newStream->subFrame = 0.0;
            // writeFrame / readFrame default to 0; ring will be filled fresh by worker.

            // Swap in the new stream. The old StreamInstance's shared_ptr
            // refcount drops; if the worker still holds it locally via
            // refillOne's `sp`, it survives until that call returns, then
            // ~StreamInstance kills the old decoder.
            slot->stream = newStream;
            StreamWorker::getInstance().registerStream(newStream);

            // Re-express positionF in new engine-rate frames so the
            // existing loop-modulo logic in mixStreamVoice keeps working.
            slot->positionF = startFrame;
            slot->positionRateHz_.store(newRate, std::memory_order_relaxed);
        }
    }
}

void AudioEngine::shutdown() {
    if (device_) {
        ma_device* device = static_cast<ma_device*>(device_);
        ma_device_uninit(device);
        delete device;
        device_ = nullptr;
    }
    if (context_) {
        ma_context* ctx = static_cast<ma_context*>(context_);
        ma_context_uninit(ctx);
        delete ctx;
        context_ = nullptr;
    }

    // The audio thread is gone: its last meter values describe nothing now.
    resetMeters();

    // Log the drops still held back by the rate limit, so the exit path
    // does not lose them. Main thread only, like every diagnostics line.
    if (isMainThread()) reportDiagnostics(true);

    // Only a running engine announces its shutdown: the app's exit path
    // calls this unconditionally, also when audio was never used.
    if (initialized_) logNotice("AudioEngine") << "shutdown";
    initialized_ = false;
}

void AudioEngine::mixAudio(float* buffer, int num_frames, int num_channels) {
    const auto t0 = std::chrono::steady_clock::now();
    mixAudioInternal(buffer, num_frames, num_channels);
    const double busy = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - t0).count();

    // Audio-thread CPU usage: time spent mixing (playing sounds + audioOut
    // listeners) relative to the audio time this callback produced.
    AudioDiagnostics& d = *diag_;
    const int rate = sampleRate_ > 0 ? sampleRate_ : DEFAULT_SAMPLE_RATE;
    const double audio = (double)num_frames / (double)rate;
    if (audio <= 0.0) return;
    const float cbLoad = (float)(busy / audio);
    if (cbLoad > d.loadWinMax) d.loadWinMax = cbLoad;
    d.loadBusy  += busy;
    d.loadAudio += audio;
    if (d.loadAudio >= 0.5) {
        d.cpuUsage.store((float)(d.loadBusy / d.loadAudio), std::memory_order_relaxed);
        d.cpuUsagePeak.store(d.loadWinMax, std::memory_order_relaxed);
        d.loadBusy = 0.0;
        d.loadAudio = 0.0;
        d.loadWinMax = 0.0f;
    }
}

// ---------------------------------------------------------------------------
// Callbacks in flight and the teardown barrier (#256)
// ---------------------------------------------------------------------------
namespace {
// audioOut / audioIn notifies running on this thread (nested when one device
// callback fires both). Non-zero only on the audio thread inside a listener,
// where waitForAudioCallbacks() must not wait for itself.
thread_local int t_callbackDepth = 0;

// How long waitForAudioCallbacks() waits. A buffer lasts ~1-100 ms, so a
// callback still running after this is stuck, not slow.
constexpr std::chrono::seconds kCallbackIdleTimeout{1};
} // namespace

int AudioEngine::beginCallback() {
    ++t_callbackDepth;
    // seq_cst, like the barrier's epoch advance and slot load, and ordered
    // before the notify's load of the listener list: for a listener removed
    // before a barrier, either the barrier counts this callback and waits
    // for it, or this callback's notify no longer sees the listener.
    const int slot = (int)(callbackEpoch_.load() & 1u);
    callbacksInFlight_[slot].fetch_add(1);
    return slot;
}

void AudioEngine::endCallback(int slot) {
    // Release: what the listeners did happens-before the barrier returns.
    callbacksInFlight_[slot].fetch_sub(1, std::memory_order_release);
    --t_callbackDepth;
}

bool AudioEngine::waitForAudioCallbacks() {
    return waitForCallbacks(true);
}

namespace internal {
void waitForAudioCallbacksNoTimeout() {
    AudioEngine::getInstance().waitForCallbacks(false);
}
} // namespace internal

bool AudioEngine::waitForCallbacks(bool giveUp) {
    if (t_callbackDepth > 0) return true;   // audio thread, inside a listener

    auto warnGaveUp = [] {
        logWarning("AudioEngine") << "waitForAudioCallbacks: an audioOut / audioIn "
            "listener has been running for over "
            << kCallbackIdleTimeout.count() << " s; continuing without "
            "waiting for it. Is it waiting on this thread (a lock held here, "
            "or work queued to it)?";
    };
    std::unique_lock<std::timed_mutex> lock(callbackBarrierMutex_, std::defer_lock);
    auto deadline = std::chrono::steady_clock::now() + kCallbackIdleTimeout;
    if (giveUp) {
        // A framework teardown may hold the mutex while it waits for a stuck
        // listener: the one-second limit covers this wait too.
        if (!lock.try_lock_until(deadline)) { warnGaveUp(); return false; }
    } else {
        lock.lock();
        deadline = std::chrono::steady_clock::now() + kCallbackIdleTimeout;
    }
    bool reported = false;
    // Advance the epoch so new callbacks count in the other slot, then wait
    // for the old slot to drain. Twice, so both slots are drained after the
    // caller's disconnect: a callback that read the epoch just before an
    // advance still counts in the slot it read.
    for (int pass = 0; pass < 2; ++pass) {
        const int slot = (int)(callbackEpoch_.fetch_add(1) & 1u);
        while (callbacksInFlight_[slot].load() != 0) {
            if (std::chrono::steady_clock::now() < deadline) {
                std::this_thread::yield();
                continue;
            }
            if (giveUp) { warnGaveUp(); return false; }
            // Framework teardown: keep waiting (a use-after-free would be
            // worse than a hang), and say why the app is stuck, once.
            if (!reported) {
                reported = true;
                logError("AudioEngine") << "an audioOut / audioIn listener has not "
                    "returned for " << kCallbackIdleTimeout.count() << " s; the "
                    "teardown keeps waiting for it before it destroys anything the "
                    "listener may use. The listener is most likely waiting on the "
                    "main thread or on a lock.";
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// MicInput implementation (Native only - Web version in platform/web/tcMicInput_web.cpp)
// ---------------------------------------------------------------------------
#ifndef __EMSCRIPTEN__

// MicInput miniaudio callback
static void micDataCallback(ma_device* pDevice, void* pOutput, const void* pInput, ma_uint32 frameCount) {
    (void)pOutput;  // Capture only, output not used

    MicInput* mic = static_cast<MicInput*>(pDevice->pUserData);
    if (mic && pInput) {
        mic->onAudioData(static_cast<const float*>(pInput), frameCount);
    }
}

// Stop and free the capture device; logs nothing.
static void releaseCaptureDevice(void*& handle) {
    if (!handle) return;
    ma_device* device = static_cast<ma_device*>(handle);
    ma_device_stop(device);
    ma_device_uninit(device);
    delete device;
    handle = nullptr;
}

MicInput::~MicInput() {
    // Not stop(): it logs, and getMicInput()'s function-local instance is
    // destroyed during static destruction when the mic is still running at
    // exit. getLogger()'s Logger is a function-local static too; when the
    // mic's first start() was the first thing to log, the Logger was
    // constructed after this instance and is already destroyed here.
    releaseCaptureDevice(device_);
}

bool MicInput::start(int sampleRate) {
    if (running_) {
        stop();
    }

    sampleRate_ = sampleRate;
    buffer_.resize(BUFFER_SIZE, 0.0f);
    writePos_ = 0;

    // Create ma_device
    ma_device* device = new ma_device();

    ma_device_config config = ma_device_config_init(ma_device_type_capture);
    config.capture.format = ma_format_f32;
    config.capture.channels = 1;  // Mono
    config.sampleRate = sampleRate;
    config.dataCallback = micDataCallback;
    config.pUserData = this;

    // Same backend choice as the engine (internal::setNullAudioBackendForTests()).
    ma_result result = g_nullBackendForTests.load(std::memory_order_relaxed)
        ? ma_device_init_ex(&kNullBackend, 1, nullptr, &config, device)
        : ma_device_init(nullptr, &config, device);
    if (result != MA_SUCCESS) {
        logError("MicInput") << "failed to initialize the capture device (result="
                             << (int)result << ")";
        delete device;
        return false;
    }

    result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        logError("MicInput") << "failed to start the capture device (result="
                             << (int)result << ")";
        ma_device_uninit(device);
        delete device;
        return false;
    }

    device_ = device;
    running_ = true;
    deviceName_ = device->capture.name;

    logNotice("MicInput") << "started (" << sampleRate << " Hz, mono, " << deviceName_ << ")";
    return true;
}

void MicInput::stop() {
    if (!running_) return;

    releaseCaptureDevice(device_);

    running_ = false;
    deviceName_.clear();
    logNotice("MicInput") << "stopped";
}

size_t MicInput::getBuffer(float* outBuffer, size_t numSamples) {
    if (!running_ || numSamples == 0) return 0;

    numSamples = std::min(numSamples, (size_t)BUFFER_SIZE);

    std::lock_guard<std::mutex> lock(mutex_);

    // Copy latest samples from ring buffer
    size_t readPos = (writePos_ + BUFFER_SIZE - numSamples) % BUFFER_SIZE;

    for (size_t i = 0; i < numSamples; i++) {
        outBuffer[i] = buffer_[(readPos + i) % BUFFER_SIZE];
    }

    return numSamples;
}

void MicInput::onAudioData(const float* input, size_t frameCount) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (size_t i = 0; i < frameCount; i++) {
        buffer_[writePos_] = input[i];
        writePos_ = (writePos_ + 1) % BUFFER_SIZE;
    }
}

// ---------------------------------------------------------------------------
// Global instance
// ---------------------------------------------------------------------------
MicInput& getMicInput() {
    static MicInput instance;
    return instance;
}

#endif // !__EMSCRIPTEN__

} // namespace trussc
