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

// miniaudio is included for ma_decoder. The implementation itself lives in
// tcAudio_impl.cpp (the only TU that defines MINIAUDIO_IMPLEMENTATION).
#include "miniaudio.h"

#include "tc/sound/tcSound.h"
#include "tc/utils/tcFile.h"

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

// Frames decoded per step. Samples are appended as they decode, so the
// stated stream length never decides how much is written anywhere.
constexpr int kDecodeChunkFrames = 4096;

// Reserve the first `count` samples of a decode buffer. Only a hint: when the
// reservation cannot be made, the buffer still grows as data decodes.
void reserveDecodeBuffer(std::vector<float>& buf, size_t count) {
    if (count == 0 || !internal::allocationFits(count * sizeof(float))) return;
    try {
        buf.reserve(count);
    } catch (const std::exception&) {
    }
}

// Append `n` decoded samples to a decode buffer, growing it by
// internal::growSampleBuffer (towards `statedSamples`, the stream's stated
// length, when that is ahead). False when the buffer cannot grow.
bool appendDecoded(std::vector<float>& buf, const float* data, size_t n, size_t statedSamples) {
    if (n > buf.max_size() - buf.size()) return false;
    if (!internal::growSampleBuffer(buf, buf.size() + n, statedSamples)) return false;
    buf.insert(buf.end(), data, data + n);
    return true;
}

// Drop the unused capacity of a finished decode buffer (a stated length above
// what decoded, or growth past the reservation). Also only a hint, and skipped
// for spare capacity up to an eighth of the samples, where the copy it takes
// would cost more than it frees.
void trimDecodeBuffer(std::vector<float>& buf) {
    if (buf.capacity() - buf.size() <= buf.size() / 8) return;
    if (!internal::allocationFits(buf.size() * sizeof(float))) return;
    try {
        buf.shrink_to_fit();
    } catch (const std::exception&) {
    }
}

// Decoded samples per input byte the first reservation is capped at, for a
// ma_decoder format (see internal::kReserveSamplesPerInputByte*)
uint64_t reserveSamplesPerInputByte(ma_encoding_format format) {
    return format == ma_encoding_format_mp3 ? internal::kReserveSamplesPerInputByteMp3
                                            : internal::kReserveSamplesPerInputByte;
}

// Decode the entire stream of an initialized ma_decoder into a SoundBuffer.
// The stream's stated length only sizes the first reservation
// (internal::decodeReserveSamples, capped by inputBytes of encoded input at
// samplesPerInputByte) and steers growth past it; the buffer grows as frames
// actually decode. On success, fills samples / channels / sampleRate /
// numSamples and uninits the decoder. On failure (including running out of
// memory), uninits the decoder, logs, leaves `out` as it was and returns
// false.
bool drainDecoder(ma_decoder& decoder, SoundBuffer& out, const char* sourceLabel,
                  uint64_t inputBytes, uint64_t samplesPerInputByte) {
    ma_uint64 frameCount = 0;
    ma_result result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
    if (result != MA_SUCCESS || frameCount == 0) {
        logError("SoundBuffer") << "failed to query length for " << sourceLabel
                                << " (result=" << (int)result << ")";
        ma_decoder_uninit(&decoder);
        return false;
    }

    const int ch = (int)decoder.outputChannels;
    const int sr = (int)decoder.outputSampleRate;
    if (ch < 1) {
        logError("SoundBuffer") << "invalid channel count " << ch << " in " << sourceLabel;
        ma_decoder_uninit(&decoder);
        return false;
    }

    std::vector<float> buf;
    size_t statedSamples = 0;   // stays 0 (no growth target) past max_size()
    internal::interleavedSampleCount(frameCount, ch, buf.max_size(), statedSamples);
    reserveDecodeBuffer(buf, internal::decodeReserveSamples(frameCount, ch, inputBytes,
                                                            samplesPerInputByte,
                                                            buf.max_size()));
    uint64_t framesRead = 0;
    bool outOfMemory = false;
    try {
        std::vector<float> chunk((size_t)kDecodeChunkFrames * (size_t)ch);
        for (;;) {
            ma_uint64 got = 0;
            result = ma_decoder_read_pcm_frames(&decoder, chunk.data(), kDecodeChunkFrames, &got);
            if (got > 0) {
                if (!appendDecoded(buf, chunk.data(), (size_t)got * (size_t)ch, statedSamples)) {
                    outOfMemory = true;
                    break;
                }
                framesRead += got;
            }
            if (result != MA_SUCCESS || got == 0) break;
        }
    } catch (const std::exception&) {
        outOfMemory = true;
    }
    ma_decoder_uninit(&decoder);
    if (outOfMemory) {
        logError("SoundBuffer") << "not enough memory to decode " << sourceLabel << " ("
                                << (unsigned long long)framesRead << " frames of " << ch
                                << " ch decoded)";
        return false;
    }

    if ((result != MA_SUCCESS && result != MA_AT_END) || framesRead == 0) {
        logError("SoundBuffer") << "failed to decode " << sourceLabel << " (result="
                                << (int)result << ", framesRead=" << (unsigned long long)framesRead
                                << ")";
        return false;
    }
    trimDecodeBuffer(buf);

    out.channels = ch;
    out.sampleRate = sr;
    out.numSamples = (size_t)framesRead;
    out.samples = std::move(buf);
    return true;
}

// Decode the entire stream of an open stb_vorbis into `out`, the same way as
// drainDecoder: the stream's stated length (the last page's granule) only
// sizes the first reservation and steers growth, and samples are appended as
// they decode. A stream whose length is unknown (0) loads what decodes.
// Closes `vorbis`. On failure, logs, leaves `out` as it was and returns the
// error.
LoadResult drainVorbis(stb_vorbis* vorbis, uint64_t inputBytes, const std::string& label,
                       SoundBuffer& out) {
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    const int ch = info.channels;
    if (ch < 1) {
        stb_vorbis_close(vorbis);
        logError("SoundBuffer") << "invalid channel count " << ch << " in " << label;
        return LoadResult::fail(LoadError::DecodeFailed, "invalid channel count in " + label);
    }
    const unsigned int statedFrames = stb_vorbis_stream_length_in_samples(vorbis);

    std::vector<float> buf;
    size_t statedSamples = 0;   // stays 0 (no growth target) past max_size()
    internal::interleavedSampleCount(statedFrames, ch, buf.max_size(), statedSamples);
    reserveDecodeBuffer(buf, internal::decodeReserveSamples(
                                 statedFrames, ch, inputBytes,
                                 internal::kReserveSamplesPerInputByteVorbis, buf.max_size()));
    uint64_t framesRead = 0;
    bool outOfMemory = false;
    try {
        // Frames are taken one packet at a time with stb_vorbis_get_frame_float
        // and interleaved here, so whatever decodes is kept whatever length the
        // stream states. The packet decode trims the last packet to the last
        // page's granule.
        std::vector<float> chunk((size_t)kDecodeChunkFrames * (size_t)ch);
        for (;;) {
            float** outputs = nullptr;
            const int got = stb_vorbis_get_frame_float(vorbis, nullptr, &outputs);
            if (got <= 0) break;
            const size_t n = (size_t)got * (size_t)ch;
            if (n > chunk.size()) chunk.resize(n);
            for (int i = 0; i < got; ++i) {
                for (int c = 0; c < ch; ++c) chunk[(size_t)i * ch + c] = outputs[c][i];
            }
            if (!appendDecoded(buf, chunk.data(), n, statedSamples)) {
                outOfMemory = true;
                break;
            }
            framesRead += (uint64_t)got;
        }
    } catch (const std::exception&) {
        outOfMemory = true;
    }
    stb_vorbis_close(vorbis);
    if (outOfMemory) {
        logError("SoundBuffer") << "not enough memory to decode " << label << " ("
                                << (unsigned long long)framesRead << " frames of " << ch
                                << " ch decoded)";
        return LoadResult::fail(LoadError::DecodeFailed, "not enough memory to decode " + label);
    }

    if (framesRead == 0) {
        logError("SoundBuffer") << "no samples decoded from " << label;
        return LoadResult::fail(LoadError::DecodeFailed, "no samples decoded");
    }
    trimDecodeBuffer(buf);

    out.channels = ch;
    out.sampleRate = (int)info.sample_rate;
    out.numSamples = (size_t)framesRead;
    out.samples = std::move(buf);
    return LoadResult::success();
}

// Build a ma_decoder_config that asks miniaudio for float32 output, keeping
// the source channel count and sample rate.
ma_decoder_config makeFloat32Config(ma_encoding_format hint) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 0, 0);
    cfg.encodingFormat = hint;
    return cfg;
}

// Open a ma_decoder from a path. Uses the wide-path entry point on Windows so
// non-ASCII paths survive (ma_fopen would take the narrow ACP route).
ma_result maDecoderInitPath(const fs::path& path,
                            const ma_decoder_config* cfg, ma_decoder* dec) {
#ifdef _WIN32
    return ma_decoder_init_file_w(path.c_str(), cfg, dec);
#else
    return ma_decoder_init_file(path.c_str(), cfg, dec);
#endif
}

bool decodeFileWithMiniaudio(const fs::path& path,
                             ma_encoding_format hint,
                             const char* label,
                             SoundBuffer& out) {
    ma_decoder decoder;
    ma_decoder_config cfg = makeFloat32Config(hint);
    std::string pathStr = internal::pathToUtf8(path);
    ma_result result = maDecoderInitPath(path, &cfg, &decoder);
    if (result != MA_SUCCESS) {
        logError("SoundBuffer") << "failed to open " << label << " " << pathStr
                                << " (result=" << (int)result << ")";
        return false;
    }
    std::error_code sizeEc;
    const uintmax_t fileBytes = fs::file_size(path, sizeEc);
    if (!drainDecoder(decoder, out, pathStr.c_str(), sizeEc ? 0 : (uint64_t)fileBytes,
                      reserveSamplesPerInputByte(hint))) {
        return false;
    }
    logVerbose("SoundBuffer") << "loaded " << label << " " << pathStr << " (" << out.channels
                              << " ch, " << out.sampleRate << " Hz, " << out.numSamples
                              << " samples)";
    return true;
}

bool decodeMemoryWithMiniaudio(const void* data, size_t dataSize,
                               ma_encoding_format hint,
                               const char* label,
                               SoundBuffer& out) {
    ma_decoder decoder;
    ma_decoder_config cfg = makeFloat32Config(hint);
    ma_result result = ma_decoder_init_memory(data, dataSize, &cfg, &decoder);
    if (result != MA_SUCCESS) {
        logError("SoundBuffer") << "failed to decode " << label << " from memory (result="
                                << (int)result << ")";
        return false;
    }
    if (!drainDecoder(decoder, out, "memory", (uint64_t)dataSize,
                      reserveSamplesPerInputByte(hint))) {
        return false;
    }
    logVerbose("SoundBuffer") << "decoded " << label << " from memory (" << out.channels
                              << " ch, " << out.sampleRate << " Hz, " << out.numSamples
                              << " samples)";
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// OGG Vorbis: stb_vorbis (miniaudio does not bundle a Vorbis decoder)
// -----------------------------------------------------------------------------

LoadResult SoundBuffer::loadOgg(const fs::path& path) {
    std::string pathStr = internal::pathToUtf8(path);
    // stb_vorbis has no UTF-8 filename mode on Windows — open the FILE*
    // ourselves (wide API) and hand it over (close_handle_on_close=TRUE).
    FILE* f = internal::openFile(path, "rb");
    if (!f) {
        logError("SoundBuffer") << "failed to open " << pathStr;
        return LoadResult::fail(LoadError::FileNotFound,
                                "failed to open: " + pathStr);
    }
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_file(f, 1, &error, nullptr);
    if (!vorbis) {
        // No fclose(f) here: with close_on_free=1, stb_vorbis owns f and has
        // already closed it when the open failed.
        logError("SoundBuffer") << "failed to open " << pathStr << " (stb_vorbis error="
                                << error << ")";
        return LoadResult::fail(LoadError::DecodeFailed,
                                "stb_vorbis failed to open " + pathStr +
                                " (error=" + std::to_string(error) + ")");
    }

    std::error_code sizeEc;
    const uintmax_t fileBytes = fs::file_size(path, sizeEc);
    LoadResult r = drainVorbis(vorbis, sizeEc ? 0 : (uint64_t)fileBytes, pathStr, *this);
    if (!r) return r;
    path_ = path;
    logVerbose("SoundBuffer") << "loaded " << pathStr << " (" << channels << " ch, "
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
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        logError("SoundBuffer") << "file not found: " << internal::pathToUtf8(path);
        return LoadResult::fail(LoadError::FileNotFound,
                                "file not found: " + internal::pathToUtf8(path));
    }
    if (!decodeFileWithMiniaudio(path, hint, label, out)) {
        return LoadResult::fail(LoadError::DecodeFailed,
                                std::string(label) + " decode failed: " +
                                internal::pathToUtf8(path));
    }
    return LoadResult::success();
}

} // namespace

LoadResult SoundBuffer::loadWav(const fs::path& path) {
    LoadResult r = loadFileViaMiniaudio(path, ma_encoding_format_wav, "WAV", *this);
    if (r) path_ = path;
    return r;
}

LoadResult SoundBuffer::loadMp3(const fs::path& path) {
    LoadResult r = loadFileViaMiniaudio(path, ma_encoding_format_mp3, "MP3", *this);
    if (r) path_ = path;
    return r;
}

LoadResult SoundBuffer::loadFlac(const fs::path& path) {
    LoadResult r = loadFileViaMiniaudio(path, ma_encoding_format_flac, "FLAC", *this);
    if (r) path_ = path;
    return r;
}

LoadResult SoundBuffer::loadWavFromMemory(const void* data, size_t dataSize) {
    path_.clear();
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_wav, "WAV", *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed, "WAV decode from memory failed");
}

LoadResult SoundBuffer::loadMp3FromMemory(const void* data, size_t dataSize) {
    path_.clear();
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_mp3, "MP3", *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed, "MP3 decode from memory failed");
}

LoadResult SoundBuffer::loadFlacFromMemory(const void* data, size_t dataSize) {
    path_.clear();
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_flac, "FLAC", *this)
               ? LoadResult::success()
               : LoadResult::fail(LoadError::DecodeFailed, "FLAC decode from memory failed");
}

LoadResult SoundBuffer::loadOggFromMemory(const void* data, size_t dataSize) {
    path_.clear();
    if (data == nullptr || dataSize == 0) {
        logError("SoundBuffer") << "empty memory range for OGG decode";
        return LoadResult::fail(LoadError::DecodeFailed, "empty memory range");
    }
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(
        static_cast<const unsigned char*>(data), static_cast<int>(dataSize),
        &error, nullptr);
    if (!vorbis) {
        logError("SoundBuffer") << "failed to decode OGG from memory (stb_vorbis error="
                                << error << ")";
        return LoadResult::fail(LoadError::DecodeFailed,
                                "stb_vorbis failed to decode OGG from memory (error=" +
                                std::to_string(error) + ")");
    }

    LoadResult r = drainVorbis(vorbis, (uint64_t)dataSize, "OGG in memory", *this);
    if (!r) return r;
    logVerbose("SoundBuffer") << "decoded OGG from memory (" << channels << " ch, "
                              << sampleRate << " Hz, " << numSamples << " samples)";
    return LoadResult::success();
}

// -----------------------------------------------------------------------------
// Auto-detect by extension. Sound::load() delegates here; callers that
// already have a SoundBuffer (e.g., for sharing across multiple Sounds) can
// use it directly.
// -----------------------------------------------------------------------------

LoadResult SoundBuffer::load(const fs::path& path) {
    // Lowercase the extension once
    std::string ext = toLower(getFileExtension(path));

    if (ext == "wav")  return loadWav(path);
    if (ext == "mp3")  return loadMp3(path);
    if (ext == "ogg")  return loadOgg(path);
    if (ext == "flac") return loadFlac(path);
    if (ext == "aac" || ext == "m4a") {
        // loadAac is per platform; record the path here for all of them.
        LoadResult r = loadAac(path);
        if (r) path_ = path;
        return r;
    }

    logError("SoundBuffer") << "unsupported extension '." << ext << "' for "
                            << internal::pathToUtf8(path);
    return LoadResult::fail(LoadError::UnsupportedFormat,
                            "unsupported extension '." + ext + "' for " +
                            internal::pathToUtf8(path));
}

} // namespace trussc
