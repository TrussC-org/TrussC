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
            // post-seek stream from this point.
            s.readFrame.store(0, std::memory_order_relaxed);
            s.writeFrame.store(0, std::memory_order_release);
            s.endOfStream.store(false, std::memory_order_release);
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
bool SoundStream::loadStream(const std::string& path, int maxPolyphony) {
    if (maxPolyphony < 1) maxPolyphony = 1;

    // Detect format by extension. We don't go through stb_vorbis here —
    // OGG support for streaming would need a separate code path. WAV /
    // MP3 / FLAC are routed through ma_decoder, which handles all three
    // with the same API.
    std::string ext;
    auto dot = path.find_last_of('.');
    if (dot != std::string::npos) {
        ext = path.substr(dot + 1);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    }

    ma_encoding_format fmt = ma_encoding_format_unknown;
    if (ext == "wav")       fmt = ma_encoding_format_wav;
    else if (ext == "mp3")  fmt = ma_encoding_format_mp3;
    else if (ext == "flac") fmt = ma_encoding_format_flac;
    else {
        printf("SoundStream: unsupported extension for streaming '.%s' (use load() for full decode)\n",
               ext.c_str());
        return false;
    }

    // Probe decode: open, query, close. Per-voice decoders re-open later.
    ma_decoder probe;
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32,
                                                    StreamInstance::CHANNELS,
                                                    AudioEngine::SAMPLE_RATE);
    cfg.encodingFormat = fmt;
    ma_result r = ma_decoder_init_file(path.c_str(), &cfg, &probe);
    if (r != MA_SUCCESS) {
        printf("SoundStream: failed to open %s (result=%d)\n", path.c_str(), (int)r);
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
           path.c_str(), channels, sampleRate, duration_, maxPolyphony);
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
            ma_format_f32, StreamInstance::CHANNELS, AudioEngine::SAMPLE_RATE);
        cfg.encodingFormat = (ma_encoding_format)s->encodingFormatHint_;
        ma_result r = ma_decoder_init_file(s->getPath().c_str(), &cfg, &stream->decoder);
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
                slot->rateRatio = (source->sampleRate > 0)
                    ? ((float)source->sampleRate / (float)SAMPLE_RATE)
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

    uint64_t writeFrame = stream->writeFrame.load(std::memory_order_acquire);
    uint64_t readFrame  = stream->readFrame.load(std::memory_order_relaxed);

    int produced = 0;
    for (int frame = 0; frame < num_frames; ++frame) {
        if (readFrame >= writeFrame) {
            // Ring is empty. If the worker has signaled EOS and we're not
            // looping, the voice is done.
            if (stream->endOfStream.load(std::memory_order_acquire)
                && !sound.loop.load()) {
                sound.playing = false;
                break;
            }
            // Underrun (worker behind): output silence for this frame.
            // Don't advance read position — give the worker a chance to
            // catch up next callback.
            continue;
        }

        size_t idx = (size_t)(readFrame & StreamInstance::RING_MASK)
                     * StreamInstance::CHANNELS;
        float l = stream->ring[idx];
        float r = stream->ring[idx + 1];
        l *= vol * panL;
        r *= vol * panR;

        buffer[frame * num_channels] += l;
        if (num_channels > 1) {
            buffer[frame * num_channels + 1] += r;
        }
        ++readFrame;
        ++produced;
    }

    stream->readFrame.store(readFrame, std::memory_order_release);

    // Track positionF for getPosition()/setPosition() bookkeeping. Stream
    // doesn't use posF for sample indexing (sequential read), but exposing
    // the played frame count keeps getPosition()/getDuration() consistent.
    if (produced > 0) {
        sound.positionF += (double)produced;
    }
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
    if (initialized_) return true;

    ma_device* device = new ma_device();

    ma_device_config config = ma_device_config_init(ma_device_type_playback);
    config.playback.format = ma_format_f32;
    config.playback.channels = NUM_CHANNELS;
    config.sampleRate = SAMPLE_RATE;
    config.dataCallback = playbackDataCallback;
    config.pUserData = this;

    ma_result result = ma_device_init(nullptr, &config, device);
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

    printf("AudioEngine: initialized (%d Hz, %d ch) [miniaudio]\n", SAMPLE_RATE, NUM_CHANNELS);
    return true;
}

void AudioEngine::shutdown() {
    if (!initialized_) return;

    if (device_) {
        ma_device* device = static_cast<ma_device*>(device_);
        ma_device_stop(device);
        ma_device_uninit(device);
        delete device;
        device_ = nullptr;
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
