// =============================================================================
// tcSound module implementation
// - Embeds stb_vorbis (used for OGG; miniaudio does not bundle a Vorbis decoder)
// - Defines SoundBuffer file/memory decoder methods (declared in tcSound.h)
//   WAV / MP3 / FLAC go through ma_decoder; OGG goes through stb_vorbis directly
// =============================================================================

#define TC_SOUND_IMPL

#include <cctype>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include <utility>
#include <vector>
#include <stdlib.h>
#include "tc/utils/tcFile.h"

#include "tc/sound/internal/tcAudioDecodeBudget.h"
#include "miniaudio.h"

namespace trussc::internal {
namespace {
std::atomic<uint64_t> decodeRejected{0}, streamRejected{0};
std::atomic<size_t> workingLimitTest{0}, noticeThresholdTest{0};
std::atomic<bool> largeLoadNoticed{false};
thread_local AudioDecodeBudget* vorbisBudget = nullptr;
}
uint64_t audioDecodeRejections(bool streaming) {
    return (streaming ? streamRejected : decodeRejected).load(std::memory_order_relaxed);
}
size_t audioWorkingLimitForTests() { return workingLimitTest.load(std::memory_order_relaxed); }
void setAudioWorkingLimitForTests(size_t bytes) { workingLimitTest.store(bytes, std::memory_order_relaxed); }
void setAudioNoticeThresholdForTests(size_t bytes) { noticeThresholdTest.store(bytes, std::memory_order_relaxed); }
void noticeAudioDecodedBytes(size_t bytes) {
    const size_t test = noticeThresholdTest.load(std::memory_order_relaxed);
    const size_t threshold = test ? test : audioLargeLoadNoticeBytes;
    if (bytes > threshold && !largeLoadNoticed.exchange(true, std::memory_order_relaxed))
        logNotice("SoundBuffer") << "decoded audio exceeds " << threshold
            << " bytes; use SoundStream::loadStream() / Sound::loadStream() for long tracks";
}
void reportAudioDecodeRejection(bool streaming, const char* store, uint64_t needed, uint64_t limit) {
    if ((streaming ? streamRejected : decodeRejected).fetch_add(1, std::memory_order_relaxed) == 0) {
        logWarning(streaming ? "SoundStream" : "SoundBuffer") << store << ": capacity "
            << limit << " (needed " << needed << "); rejecting whole " << (streaming ? "stream" : "sound")
            << (streaming ? "" : "; load long audio with SoundStream::loadStream() / Sound::loadStream()");
    }
}
struct VorbisBudgetScope {
    AudioDecodeBudget* previous;
    explicit VorbisBudgetScope(AudioDecodeBudget& b) : previous(vorbisBudget) { vorbisBudget = &b; }
    ~VorbisBudgetScope() { vorbisBudget = previous; }
};
}
// Bound (platform): Vorbis also keeps its compiled channel-array ceiling. Overflow rejects the whole sound with a first Warning and is counted.
#define STB_VORBIS_VALIDATE_FORMAT(ch, rate) (!trussc::internal::vorbisBudget || \
    (trussc::internal::vorbisBudget->format(ch, rate) && ((ch) <= STB_VORBIS_MAX_CHANNELS || \
     trussc::internal::vorbisBudget->reject("Vorbis decoder channels", ch, STB_VORBIS_MAX_CHANNELS))))
// TrussC does not use comment metadata; consume it without allocating strings.
#define STB_VORBIS_NO_COMMENTS
// stb_vorbis - OGG Vorbis decoder
//
// The header has an overflow-detection branch ("stream_start + loc < stream_start")
// that modern Clang flags as `-Wtautological-compare` because pointer arithmetic
// past the end of an object is UB and the compiler may elide the check anyway.
// The semantics are an upstream concern; suppress the noise locally so the
// build stays clean. Keep the suppression as tight as possible around just
// the third-party include.
extern "C" {
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-compare"
#endif
#include "stb_vorbis.c"
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
}

#undef STB_VORBIS_NO_COMMENTS
#undef STB_VORBIS_VALIDATE_FORMAT

namespace trussc {

namespace internal {
namespace {
// Set by setAllocationLimitForTests(); 0 (no limit) normally.
std::atomic<size_t> g_allocationLimitForTests{0};
} // namespace

void setAllocationLimitForTests(size_t bytes) {
    g_allocationLimitForTests.store(bytes, std::memory_order_relaxed);
}

size_t allocationLimitForTests() {
    return g_allocationLimitForTests.load(std::memory_order_relaxed);
}
} // namespace internal

namespace {

// Bound (security): scratch holds one 4096-frame decode step. Overflow continues in later steps; encoded-input/format rejection rejects the whole sound and is counted.
constexpr int kDecodeChunkFrames = 4096;

bool drainDecoder(ma_decoder& decoder, SoundBuffer& out, internal::AudioDecodeBudget& budget) {
    const unsigned ch = decoder.outputChannels, sr = decoder.outputSampleRate;
    ma_uint64 stated = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &stated);
    bool ok = !budget.rejected && !budget.allocationFailed && budget.format(ch, sr);
    std::vector<float> buf, chunk;
    // A landing hint can only reduce geometric growth, never enlarge the first
    // reservation. An unrepresentable header is ignored; actual appends decide.
    size_t landingSamples = 0;
    internal::interleavedSampleCount(stated, int(ch), buf.max_size(), landingSamples);
    uint64_t frames = 0;
    ma_result result = MA_SUCCESS;
    ma_uint32 nativeChannels = 0, nativeRate = 0;
    ma_data_source_get_data_format(decoder.pBackend, nullptr, &nativeChannels, &nativeRate, nullptr, 0);
    if (ok) ok = internal::growAudioVector(chunk, size_t(kDecodeChunkFrames) * ch, budget);
    if (ok) chunk.resize(size_t(kDecodeChunkFrames) * ch);
    while (ok) {
        ma_uint64 got = 0;
        result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kDecodeChunkFrames, &got);
        ma_uint32 currentChannels = 0, currentRate = 0;
        ma_data_source_get_data_format(decoder.pBackend, nullptr, &currentChannels, &currentRate, nullptr, 0);
        if (currentChannels != nativeChannels || currentRate != nativeRate) {
            budget.reject("audio format change", currentChannels, nativeChannels);
            ok = false; break;
        }
        if (budget.rejected || budget.allocationFailed || got > kDecodeChunkFrames) { ok = false; break; }
        if (got && !internal::appendAudio(buf, chunk.data(), size_t(got) * ch, budget,
                                          chunk.capacity() * sizeof(float), landingSamples)) { ok = false; break; }
        frames += got;
        if (result != MA_SUCCESS || !got) break;
    }
    ma_decoder_uninit(&decoder);
    if (!ok || !frames || (result != MA_SUCCESS && result != MA_AT_END)) return false;
    internal::trimAudioVector(buf, budget, chunk.capacity() * sizeof(float));
    out.channels = int(ch); out.sampleRate = int(sr); out.numSamples = size_t(frames);
    out.samples = std::move(buf);
    return true;
}

LoadResult drainVorbis(stb_vorbis* vorbis, internal::AudioDecodeBudget& budget, SoundBuffer& out) {
    const auto info = stb_vorbis_get_info(vorbis);
    const unsigned stated = stb_vorbis_stream_length_in_samples(vorbis);
    const int lengthError = stb_vorbis_get_error(vorbis);
    // An unavailable final granule is a duration hint failure, not a packet
    // decode failure. Clear it before checking each actual decoded packet.
    bool ok = (lengthError == VORBIS__no_error || (stated == 0 && lengthError == VORBIS_cant_find_last_page)) &&
        budget.format(info.channels, info.sample_rate);
    std::vector<float> buf, chunk;
    size_t landingSamples = 0;
    internal::interleavedSampleCount(stated, info.channels, buf.max_size(), landingSamples);
    uint64_t frames = 0;
    int error = VORBIS__no_error;
    while (ok) {
        float** output = nullptr;
        int channels = 0;
        const int got = stb_vorbis_get_frame_float(vorbis, &channels, &output);
        error = stb_vorbis_get_error(vorbis);
        if (budget.rejected || error != VORBIS__no_error) { ok = false; break; }
        if (got <= 0) break;
        if (channels != info.channels) { ok = budget.reject("audio format change", channels, info.channels); break; }
        const size_t count = size_t(got) * channels;
        if (!internal::growAudioVector(chunk, count, budget, buf.capacity() * sizeof(float))) { ok = false; break; }
        chunk.resize(count);
        for (int i = 0; i < got; ++i)
            for (int c = 0; c < channels; ++c) chunk[size_t(i) * channels + c] = output[c][i];
        if (!internal::appendAudio(buf, chunk.data(), count, budget, chunk.capacity() * sizeof(float), landingSamples)) { ok = false; break; }
        frames += got;
    }
    stb_vorbis_close(vorbis);
    if (!ok || !frames) {
        if (!budget.rejected && !budget.allocationFailed) logError("SoundBuffer") << "OGG decode failed";
        return LoadResult::fail(LoadError::DecodeFailed, "OGG decode failed");
    }
    internal::trimAudioVector(buf, budget, chunk.capacity() * sizeof(float));
    out.channels = info.channels; out.sampleRate = int(info.sample_rate); out.numSamples = size_t(frames);
    out.samples = std::move(buf);
    return LoadResult::success();
}

bool decodeMemoryWithMiniaudio(const void* data, size_t bytes, ma_encoding_format hint,
                               const char* label, SoundBuffer& out) {
    internal::AudioDecodeBudget budget;
    if (!data || !bytes || !budget.encoded(bytes)) return false;
    ma_decoder decoder;
    auto cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    cfg.encodingFormat = hint;
    cfg.seekPointCount = 0;
    cfg.allocationCallbacks = {&budget, internal::AudioDecodeBudget::allocate,
        internal::AudioDecodeBudget::reallocate, internal::AudioDecodeBudget::release};
    if (ma_decoder_init_memory(data, bytes, &cfg, &decoder) != MA_SUCCESS) {
        if (!budget.rejected && !budget.allocationFailed) logError("SoundBuffer") << "audio decoder initialization failed";
        return false;
    }
    const bool ok = drainDecoder(decoder, out, budget);
    if (!ok && !budget.rejected && !budget.allocationFailed) logError("SoundBuffer") << "audio decode failed; load long audio with SoundStream::loadStream() / Sound::loadStream()";
    if (ok) logVerbose("SoundBuffer") << "decoded " << label << " from memory (" << out.channels << " ch, "
        << out.sampleRate << " Hz, " << out.numSamples << " samples)";
    return ok;
}
bool decodeFileWithMiniaudio(const fs::path& path, ma_encoding_format hint, const char* label, SoundBuffer& out) {
    internal::AudioDecodeBudget budget;
    std::vector<unsigned char> bytes;
    const bool ok = internal::readAudioFile(path, bytes, budget) &&
        decodeMemoryWithMiniaudio(bytes.data(), bytes.size(), hint, label, out);
    if (ok) logVerbose("SoundBuffer") << "loaded " << label << " " << path << " (" << out.channels
        << " ch, " << out.sampleRate << " Hz, " << out.numSamples << " samples)";
    return ok;
}

} // namespace

SoundBuffer::SoundBuffer(const SoundBuffer& other) : SoundBuffer() {
    *this = other;
}
SoundBuffer& SoundBuffer::operator=(const SoundBuffer& other) {
    if (this == &other) return *this;
    async_.cancel();
    samples = other.samples;
    channels = other.channels;
    sampleRate = other.sampleRate;
    numSamples = other.numSamples;
    path_ = other.path_;
    return *this;
}
SoundBuffer::SoundBuffer(SoundBuffer&& other) noexcept : SoundBuffer() {
    *this = std::move(other);
}
SoundBuffer& SoundBuffer::operator=(SoundBuffer&& other) noexcept {
    if (this == &other) return *this;
    async_.cancel();
    other.async_.cancel();
    samples = std::move(other.samples);
    channels = other.channels;
    sampleRate = other.sampleRate;
    numSamples = other.numSamples;
    path_ = std::move(other.path_);
    return *this;
}
void SoundBuffer::loadAsync(const fs::path& path) {
    auto state = async_.begin(onLoad, [this](AsyncState& ready) {
        auto& decoded = *ready.decoded;
        std::lock_guard<std::mutex> lock(AudioEngine::getInstance().mutex_);
        // Publish PCM and metadata only after the entire decode succeeds.
        const unsigned fields = ready.result ? 15 : 0;
        if (fields & 1) samples = std::move(decoded.samples);
        if (fields & 2) channels = decoded.channels;
        if (fields & 4) sampleRate = decoded.sampleRate;
        if (fields & 8) numSamples = decoded.numSamples;
        path_ = std::move(decoded.path_);
    });
    state->input = path;
    state->path = internal::resolveLoadPath(path);
    state->decoded = std::make_shared<SoundBuffer>();
    internal::startResourceLoad([state] {
        internal::probeResourceLoad("sound.decode", state->path);
        if (state->cancelled.load()) return;
#ifdef __EMSCRIPTEN__
        const auto ext = toLower(getFileExtension(state->input));
        if (ext == "aac" || ext == "m4a") {
            auto entry = internal::checkLoadFile(state->path, "SoundBuffer");
            if (entry) {
                loadAacWebAsync(state->decoded, [state](LoadResult result) {
                    state->result = std::move(result);
                    if (state->result) internal::setLoadName(state->decoded->path_, state->path);
                    if (!state->cancelled.load()) internal::completeResourceLoad(state);
                }, state->path);
                return;
            }
            state->result = entry;
        } else
#endif
        state->result = state->decoded->loadResolved(state->input, state->path);
        internal::probeResourceLoad("sound.ready", state->path);
        if (!state->cancelled.load()) internal::completeResourceLoad(state);
    });
}

// -----------------------------------------------------------------------------
// OGG Vorbis: stb_vorbis (miniaudio does not bundle a Vorbis decoder)
// -----------------------------------------------------------------------------

LoadResult SoundBuffer::loadOgg(const fs::path& filePath) {
    async_.cancel();
    internal::clearLoadName(path_);
    const auto path = internal::resolveLoadPath(filePath);
    std::string pathStr = internal::pathToDisplayUtf8(path);
    internal::AudioDecodeBudget budget;
    LoadResult entry;
    FILE* file = internal::openLoadFile(path, "SoundBuffer", entry);
    if (!file) return entry;
    std::vector<unsigned char> bytes;
    if (!internal::readAudioFile(path, bytes, budget, file))
        return LoadResult::fail(LoadError::DecodeFailed, "OGG input rejected");
    internal::VorbisBudgetScope scope(budget);
    int error = 0;
    auto* vorbis = stb_vorbis_open_memory(bytes.data(), int(bytes.size()), &error, nullptr);
    if (!vorbis) {
        if (!budget.rejected) logError("SoundBuffer") << "failed to open " << pathStr << " (stb_vorbis error=" << error << ")";
        return LoadResult::fail(LoadError::DecodeFailed, "OGG decoder initialization failed");
    }
    LoadResult r = drainVorbis(vorbis, budget, *this);
    if (!r) return r;
    internal::setLoadName(path_, path);
    logVerbose("SoundBuffer") << "loaded " << path << " (" << channels << " ch, "
                              << sampleRate << " Hz, " << numSamples << " samples)";
    return LoadResult::success();
}

// -----------------------------------------------------------------------------
// WAV / MP3 / FLAC: routed through ma_decoder
// -----------------------------------------------------------------------------

namespace {

// Shared file-based decode wrapper: classify missing files before handing
// the path to miniaudio (whose error codes don't distinguish the two cases
// cheaply).
LoadResult loadFileViaMiniaudio(const fs::path& path, ma_encoding_format hint,
                                const char* label, SoundBuffer& out) {
    auto entry = internal::checkLoadFile(path, "SoundBuffer");
    if (!entry) return entry;
    if (!decodeFileWithMiniaudio(path, hint, label, out)) {
        return LoadResult::fail(LoadError::DecodeFailed,
                                std::string(label) + " decode failed: " +
                                internal::pathToDisplayUtf8(path));
    }
    return LoadResult::success();
}

} // namespace

LoadResult SoundBuffer::loadWav(const fs::path& filePath) {
    async_.cancel();
    internal::clearLoadName(path_);
    const auto path = internal::resolveLoadPath(filePath);
    LoadResult r = loadFileViaMiniaudio(path, ma_encoding_format_wav, "WAV", *this);
    if (r) internal::setLoadName(path_, path);
    return r;
}

LoadResult SoundBuffer::loadMp3(const fs::path& filePath) {
    async_.cancel();
    internal::clearLoadName(path_);
    const auto path = internal::resolveLoadPath(filePath);
    LoadResult r = loadFileViaMiniaudio(path, ma_encoding_format_mp3, "MP3", *this);
    if (r) internal::setLoadName(path_, path);
    return r;
}

LoadResult SoundBuffer::loadFlac(const fs::path& filePath) {
    async_.cancel();
    internal::clearLoadName(path_);
    const auto path = internal::resolveLoadPath(filePath);
    LoadResult r = loadFileViaMiniaudio(path, ma_encoding_format_flac, "FLAC", *this);
    if (r) internal::setLoadName(path_, path);
    return r;
}

LoadResult SoundBuffer::loadWavFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_wav, "WAV", *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed, "WAV decode from memory failed");
}

LoadResult SoundBuffer::loadMp3FromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_mp3, "MP3", *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed, "MP3 decode from memory failed");
}

LoadResult SoundBuffer::loadFlacFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_flac, "FLAC", *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed, "FLAC decode from memory failed");
}

LoadResult SoundBuffer::loadOggFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    if (data == nullptr || dataSize == 0) {
        logError("SoundBuffer") << "empty memory range for OGG decode";
        return LoadResult::fail(LoadError::DecodeFailed, "empty memory range");
    }
    internal::AudioDecodeBudget budget;
    if (!budget.encoded(dataSize)) return LoadResult::fail(LoadError::DecodeFailed, "OGG input rejected");
    internal::VorbisBudgetScope scope(budget);
    int error = 0;
    auto* vorbis = stb_vorbis_open_memory(static_cast<const unsigned char*>(data), int(dataSize), &error, nullptr);
    if (!vorbis) {
        if (!budget.rejected) logError("SoundBuffer") << "failed to open OGG decoder (error=" << error << ")";
        return LoadResult::fail(LoadError::DecodeFailed, "OGG decoder initialization failed");
    }
    LoadResult r = drainVorbis(vorbis, budget, *this);
    if (!r) return r;
    logVerbose("SoundBuffer") << "decoded OGG from memory (" << channels << " ch, "
                              << sampleRate << " Hz, " << numSamples << " samples)";
    return LoadResult::success();
}

// -----------------------------------------------------------------------------
// AAC's file entry is shared; only the decode belongs to the platform.
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAac(const fs::path& filePath) {
    async_.cancel();
    internal::clearLoadName(path_);
    const auto path = internal::resolveLoadPath(filePath);
    auto entry = internal::checkLoadFile(path, "SoundBuffer");
    if (!entry) return entry;
    auto result = loadAacPlatform(path);
    if (result) internal::setLoadName(path_, path);
    return result;
}

// -----------------------------------------------------------------------------
// Auto-detect by extension. Sound::load() delegates here; callers that
// already have a SoundBuffer (e.g., for sharing across multiple Sounds) can
// use it directly.
// -----------------------------------------------------------------------------

LoadResult SoundBuffer::load(const fs::path& path) {
    return loadResolved(path, internal::resolveLoadPath(path));
}

LoadResult SoundBuffer::loadResolved(const fs::path& input, const fs::path& path) {
    async_.cancel();
    internal::clearLoadName(path_);
    // Lowercase the extension once
    std::string ext = toLower(getFileExtension(input));

    if (ext == "wav")  return loadWav(path);
    if (ext == "mp3")  return loadMp3(path);
    if (ext == "ogg")  return loadOgg(path);
    if (ext == "flac") return loadFlac(path);
    if (ext == "aac" || ext == "m4a") {
        return loadAac(path);
    }

    auto entry = internal::checkLoadFile(internal::resolveLoadPath(path), "SoundBuffer");
    if (!entry) return entry;
    logError("SoundBuffer") << "unsupported extension '." << ext << "' for "
                            << input;
    return LoadResult::fail(LoadError::UnsupportedFormat,
                            "unsupported extension '." + ext + "' for " +
                            internal::pathToDisplayUtf8(input));
}

} // namespace trussc
