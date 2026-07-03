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
#include "miniaudio.h"

#include "tc/sound/tcSound.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
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
} // namespace


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
    // Seek request: writer side honors it before its next decode read.
    std::atomic<bool>     seekRequested{false};
    std::atomic<uint64_t> seekTargetFrame{0};

    uint64_t totalFramesInFile = 0;           // duration in source frames

    // Sub-frame position inside the ring for setSpeed-aware linear interp.
    // Only touched by the mixer (audio callback thread) — single-writer, no
    // atomicity needed.
    double subFrame = 0.0;

    StreamInstance() : ring(RING_FRAMES * CHANNELS, 0.0f) {}
    ~StreamInstance() {
        if (decoderInitialized) {
            ma_decoder_uninit(&decoder);
            decoderInitialized = false;
        }
    }

    // Frames available for read (monotonic, never overflows in practice
    // — uint64 at 96 kHz lasts ~6 million years).
    uint64_t available() const {
        return writeFrame.load(std::memory_order_acquire)
             - readFrame.load(std::memory_order_relaxed);
    }
    uint64_t space() const { return RING_FRAMES - available(); }
};

} // namespace internal

// Implementation detail: the rest of this TU refers to StreamInstance
// unqualified. (This is a .cpp, not a public header.)
using internal::StreamInstance;

// ---------------------------------------------------------------------------
// StreamWorker — single thread + condition variable; refills any registered
// StreamInstance whose ring buffer is below half capacity. Wakes up on a CV
// signal whenever a new instance is registered, plus a periodic timeout so
// long-running streams stay topped up.
// ---------------------------------------------------------------------------
class StreamWorker {
public:
    static StreamWorker& getInstance() {
        static StreamWorker instance;
        return instance;
    }

    void registerStream(std::weak_ptr<StreamInstance> w) {
        std::lock_guard<std::mutex> lock(mutex_);
        streams_.push_back(std::move(w));
        ensureRunningLocked();
        cv_.notify_one();
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stop_ = true;
            cv_.notify_all();
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    StreamWorker() = default;
    ~StreamWorker() { shutdown(); }

    void ensureRunningLocked() {
        if (running_) return;
        stop_ = false;
        running_ = true;
        thread_ = std::thread([this] { run(); });
    }

    // Refill one stream up to roughly full. Honor seek requests first.
    void refillOne(StreamInstance& s) {
        if (s.disposed.load(std::memory_order_acquire)) return;
        if (!s.decoderInitialized) return;

        if (s.seekRequested.exchange(false, std::memory_order_acq_rel)) {
            uint64_t target = s.seekTargetFrame.load(std::memory_order_relaxed);
            ma_decoder_seek_to_pcm_frame(&s.decoder, target);
            // Reset ring: drain any stale samples so the mixer sees the
            // post-seek stream from this point. subFrame is owned by the
            // mixer thread; clearing it from here is a benign race in
            // practice (the mixer reads it again next callback and the
            // worst case is one sample of phase wobble at the seek point).
            s.readFrame.store(0, std::memory_order_relaxed);
            s.writeFrame.store(0, std::memory_order_release);
            s.endOfStream.store(false, std::memory_order_release);
            s.subFrame = 0.0;
        }

        // Decode in chunks of up to scratchFrames frames at a time.
        constexpr size_t scratchFrames = 1024;
        float scratch[scratchFrames * StreamInstance::CHANNELS];

        while (s.space() >= scratchFrames) {
            ma_uint64 read = 0;
            ma_result r = ma_decoder_read_pcm_frames(
                &s.decoder, scratch, scratchFrames, &read);
            if (read == 0) {
                if (s.looping.load(std::memory_order_acquire)) {
                    ma_decoder_seek_to_pcm_frame(&s.decoder, 0);
                    continue;
                }
                s.endOfStream.store(true, std::memory_order_release);
                break;
            }

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

            if ((ma_uint64)read < (ma_uint64)scratchFrames) {
                // Short read = end of file. Next iteration's read=0 path
                // handles the EOS / loop bookkeeping; bail out for now.
                if (!s.looping.load(std::memory_order_acquire)) {
                    s.endOfStream.store(true, std::memory_order_release);
                }
                break;
            }
            (void)r;
        }
    }

    void run() {
        while (true) {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait_for(lock, std::chrono::milliseconds(5),
                         [this] { return stop_ || !streams_.empty(); });
            if (stop_) break;

            // Snapshot strong refs while holding the lock, then drop the
            // lock for the actual decode work (which calls into miniaudio
            // and shouldn't block other registrations).
            std::vector<std::shared_ptr<StreamInstance>> live;
            live.reserve(streams_.size());
            auto it = streams_.begin();
            while (it != streams_.end()) {
                if (auto sp = it->lock()) {
                    if (!sp->disposed.load(std::memory_order_acquire)) {
                        live.push_back(std::move(sp));
                    }
                    ++it;
                } else {
                    it = streams_.erase(it);
                }
            }
            lock.unlock();

            for (auto& sp : live) {
                refillOne(*sp);
            }
        }
        running_ = false;
    }

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::weak_ptr<StreamInstance>> streams_;
    bool stop_ = false;
    bool running_ = false;
};

// ---------------------------------------------------------------------------
// SoundStream::loadStream — open the file once to validate format and
// query duration/channels/sampleRate. Per-voice decoders are opened
// lazily by AudioEngine::play().
// ---------------------------------------------------------------------------
bool SoundStream::loadStream(const fs::path& path, int maxPolyphony) {
    if (maxPolyphony < 1) maxPolyphony = 1;

    // Detect format by extension. We don't go through stb_vorbis here —
    // OGG support for streaming would need a separate code path. WAV /
    // MP3 / FLAC are routed through ma_decoder, which handles all three
    // with the same API.
    std::string ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);

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
        printf("SoundStream: unsupported extension for streaming '.%s' (use load() for full decode)\n",
               ext.c_str());
        return false;
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
        printf("SoundStream: failed to open %s (result=%d)\n",
               internal::pathToUtf8(path).c_str(), (int)r);
        return false;
    }

    ma_uint64 totalFrames = 0;
    ma_decoder_get_length_in_pcm_frames(&probe, &totalFrames);
    channels = (int)probe.outputChannels;
    sampleRate = (int)probe.outputSampleRate;
    duration_ = (sampleRate > 0)
                ? (float)((double)totalFrames / (double)sampleRate)
                : 0.0f;
    ma_decoder_uninit(&probe);

    path_ = path;
    maxPolyphony_ = maxPolyphony;
    encodingFormatHint_ = (int)fmt;

    printf("SoundStream: ready %s (%d ch, %d Hz, %.2f s, maxPolyphony=%d)\n",
           internal::pathToUtf8(path).c_str(), channels, sampleRate, duration_, maxPolyphony);
    return true;
}

// ---------------------------------------------------------------------------
// AudioEngine::play(SoundSource) — unified entry point for both eager
// SoundBuffer and streaming SoundStream sources.
// ---------------------------------------------------------------------------
std::shared_ptr<PlayingSound> AudioEngine::play(std::shared_ptr<SoundSource> source) {
    if (!initialized_ || !source) return nullptr;

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
            // Conservative: log + reject. The caller (Sound::play()) will
            // see nullptr and stop() its previous instance before
            // retrying, matching the documented "default = single-instance"
            // behavior.
            printf("SoundStream: maxPolyphony=%d reached for %s — "
                   "stop a previous instance or raise maxPolyphony\n",
                   s->getMaxPolyphony(), s->getPath().c_str());
            return nullptr;
        }

        // Open a fresh decoder for this voice.
        stream = std::make_shared<StreamInstance>();
        ma_decoder_config cfg = ma_decoder_config_init(
            ma_format_f32, StreamInstance::CHANNELS, sampleRate_);
        cfg.encodingFormat = (ma_encoding_format)s->encodingFormatHint_;
        ma_result r = maDecoderInitPathA(s->path_, &cfg, &stream->decoder);
        if (r != MA_SUCCESS) {
            printf("SoundStream: per-voice decoder init failed for %s (result=%d)\n",
                   s->getPath().c_str(), (int)r);
            return nullptr;
        }
        stream->decoderInitialized = true;
        ma_uint64 totalFrames = 0;
        ma_decoder_get_length_in_pcm_frames(&stream->decoder, &totalFrames);
        stream->totalFramesInFile = (uint64_t)totalFrames;

        StreamWorker::getInstance().registerStream(stream);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& slot : playingSounds_) {
        if (!slot || !slot->playing) {
            slot = std::make_shared<PlayingSound>();
            slot->buffer = source;
            slot->stream = stream;
            slot->positionF = 0.0;
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

    printf("AudioEngine: max playing sounds reached\n");
    // Mark the just-opened stream as disposed so the worker drops it.
    if (stream) stream->disposed.store(true, std::memory_order_release);
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

    uint64_t writeFrame = stream->writeFrame.load(std::memory_order_acquire);
    uint64_t readFrame  = stream->readFrame.load(std::memory_order_relaxed);
    double   subFrame   = stream->subFrame;

    int produced = 0;
    double posAdvance = 0.0;  // sum of consumed ring frames this callback

    for (int frame = 0; frame < num_frames; ++frame) {
        // Need both readFrame and readFrame+1 for interpolation.
        if (readFrame + 1 >= writeFrame) {
            if (stream->endOfStream.load(std::memory_order_acquire)
                && !sound.loop.load()) {
                sound.playing = false;
                break;
            }
            // Underrun: emit nothing for this output frame, give the
            // worker a chance to catch up. subFrame state preserved.
            continue;
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

            buffer[frame * num_channels + c] += sample * gain * panMul * vol;
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
        printf("AudioEngine: re-initializing (%d Hz, %d ch -> %d Hz, %d ch, dev='%s')\n",
               sampleRate_, channels_,
               settings.sampleRate, settings.channels,
               settings.deviceName.c_str());

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
        if (ma_context_init(NULL, 0, NULL, ctx) != MA_SUCCESS) {
            printf("AudioEngine: ma_context_init failed\n");
            delete ctx;
            return false;
        }
        context_ = ctx;
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
            printf("AudioEngine: device '%s' not found, using system default\n",
                   settings.deviceName.c_str());
        }
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
        printf("AudioEngine: failed to initialize device (error=%d)\n", result);
        delete device;
        return false;
    }

    result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        printf("AudioEngine: failed to start device (error=%d)\n", result);
        ma_device_uninit(device);
        delete device;
        return false;
    }

    device_ = device;
    initialized_ = true;

    printf("AudioEngine: initialized (%d Hz, %d ch, %d voices) [miniaudio]\n",
           sampleRate_, channels_, (int)playingSounds_.size());

    // Fire audioDeviceChanged with the resolved device's real info.
    // ma_device's playback.name is populated by ma_device_init even when
    // the caller didn't specify a device name (system default path).
    AudioDeviceChangedArgs args;
    args.deviceName   = std::string(device->playback.name);
    args.sampleRate   = sampleRate_;
    args.channels     = channels_;
    args.bufferSize   = bufferSize_;
    args.maxPolyphony = (int)playingSounds_.size();

    // Determine whether the opened device is the OS default by comparing
    // its device ID against the isDefault flag from the playback device
    // enumeration. We compare ma_device_id by raw bytes because its
    // contents vary by backend (CoreAudio uses UID strings, WASAPI uses
    // wide strings, ALSA uses device strings, etc.).
    args.isDefaultDevice = false;
    {
        ma_device_info* infos = nullptr;
        ma_uint32 count = 0;
        if (ma_context_get_devices(ctxArg, &infos, &count,
                                    NULL, NULL) == MA_SUCCESS) {
            for (ma_uint32 i = 0; i < count; ++i) {
                if (std::memcmp(&infos[i].id, &device->playback.id,
                                sizeof(ma_device_id)) == 0) {
                    args.isDefaultDevice = (infos[i].isDefault != 0);
                    break;
                }
            }
        }
    }

    audioDeviceChanged.notify(args);

    return true;
}

std::vector<AudioDeviceInfo> AudioEngine::listDevices() {
    std::vector<AudioDeviceInfo> result;

    ma_context context;
    if (ma_context_init(NULL, 0, NULL, &context) != MA_SUCCESS) {
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
// configured at newRate), seek it to the same wall-clock playback time,
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
        if (!slot->playing && !slot->paused) continue;

        if (slot->buffer->kind() == SoundSource::Eager) {
            // Pitch-preserving rate change. positionF is source-rate frames.
            slot->rateRatio = (slot->buffer->sampleRate > 0)
                ? ((float)slot->buffer->sampleRate / (float)newRate)
                : 1.0f;
        } else {
            // Streaming voice — rebuild the per-voice decoder.
            auto* src = static_cast<SoundStream*>(slot->buffer.get());

            // Current playback time in seconds, derived from the old engine rate.
            double tSec = slot->positionF / (double)oldRate;
            if (tSec < 0.0) tSec = 0.0;

            // Build a fresh StreamInstance + decoder at the new rate.
            auto newStream = std::make_shared<StreamInstance>();
            ma_decoder_config cfg = ma_decoder_config_init(
                ma_format_f32, StreamInstance::CHANNELS, newRate);
            cfg.encodingFormat = (ma_encoding_format)src->encodingFormatHint_;
            ma_result r = maDecoderInitPathA(src->path_, &cfg, &newStream->decoder);
            if (r != MA_SUCCESS) {
                printf("AudioEngine: stream voice migration failed for %s (result=%d) — stopping voice\n",
                       src->getPath().c_str(), (int)r);
                slot->playing = false;
                // Drop the stale stream so its old decoder is destroyed.
                slot->stream.reset();
                continue;
            }
            newStream->decoderInitialized = true;

            // Seek to the same wall-clock time in the new decoder's output frames.
            ma_uint64 seekFrames = (ma_uint64)(tSec * (double)newRate);
            ma_decoder_seek_to_pcm_frame(&newStream->decoder, seekFrames);

            ma_uint64 total = 0;
            ma_decoder_get_length_in_pcm_frames(&newStream->decoder, &total);
            newStream->totalFramesInFile = (uint64_t)total;
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
            slot->positionF = tSec * (double)newRate;
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

    initialized_ = false;
    printf("AudioEngine: shutdown\n");
}

void AudioEngine::mixAudio(float* buffer, int num_frames, int num_channels) {
    mixAudioInternal(buffer, num_frames, num_channels);
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

MicInput::~MicInput() {
    stop();
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

    ma_result result = ma_device_init(nullptr, &config, device);
    if (result != MA_SUCCESS) {
        printf("MicInput: failed to initialize device (error=%d)\n", result);
        delete device;
        return false;
    }

    result = ma_device_start(device);
    if (result != MA_SUCCESS) {
        printf("MicInput: failed to start device (error=%d)\n", result);
        ma_device_uninit(device);
        delete device;
        return false;
    }

    device_ = device;
    running_ = true;

    printf("MicInput: started (%d Hz, mono)\n", sampleRate);
    return true;
}

void MicInput::stop() {
    if (!running_) return;

    if (device_) {
        ma_device* device = static_cast<ma_device*>(device_);
        ma_device_stop(device);
        ma_device_uninit(device);
        delete device;
        device_ = nullptr;
    }

    running_ = false;
    printf("MicInput: stopped\n");
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
