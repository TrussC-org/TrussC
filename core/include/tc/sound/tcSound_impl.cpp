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
#include <string>
#include <utility>

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

namespace trussc {

namespace {

// Decode the entire stream of an initialized ma_decoder into a SoundBuffer.
// On success, fills samples / channels / sampleRate / numSamples and uninits
// the decoder. On failure, uninits the decoder and returns false.
bool drainDecoder(ma_decoder& decoder, SoundBuffer& out, const char* sourceLabel) {
    ma_uint64 frameCount = 0;
    ma_result result = ma_decoder_get_length_in_pcm_frames(&decoder, &frameCount);
    if (result != MA_SUCCESS || frameCount == 0) {
        printf("SoundBuffer: failed to query length for %s (result=%d)\n",
               sourceLabel, (int)result);
        ma_decoder_uninit(&decoder);
        return false;
    }

    const int ch = (int)decoder.outputChannels;
    const int sr = (int)decoder.outputSampleRate;

    std::vector<float> buf((size_t)frameCount * (size_t)ch);
    ma_uint64 framesRead = 0;
    result = ma_decoder_read_pcm_frames(&decoder, buf.data(), frameCount, &framesRead);
    ma_decoder_uninit(&decoder);

    if (result != MA_SUCCESS || framesRead == 0) {
        printf("SoundBuffer: failed to decode %s (result=%d, framesRead=%llu)\n",
               sourceLabel, (int)result, (unsigned long long)framesRead);
        return false;
    }

    if (framesRead != frameCount) {
        // Trim to actually-decoded length (rare, but possible with streams
        // whose declared length differs from what the decoder yields).
        buf.resize((size_t)framesRead * (size_t)ch);
    }

    out.channels = ch;
    out.sampleRate = sr;
    out.numSamples = (size_t)framesRead;
    out.samples = std::move(buf);
    return true;
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
        printf("SoundBuffer: failed to open %s %s (result=%d)\n",
               label, pathStr.c_str(), (int)result);
        return false;
    }
    if (!drainDecoder(decoder, out, pathStr.c_str())) return false;
    printf("SoundBuffer: loaded %s %s (%d ch, %d Hz, %zu samples)\n",
           label, pathStr.c_str(), out.channels, out.sampleRate, out.numSamples);
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
        printf("SoundBuffer: failed to decode %s from memory (result=%d)\n",
               label, (int)result);
        return false;
    }
    if (!drainDecoder(decoder, out, "memory")) return false;
    printf("SoundBuffer: decoded %s from memory (%d ch, %d Hz, %zu samples)\n",
           label, out.channels, out.sampleRate, out.numSamples);
    return true;
}

} // namespace

// -----------------------------------------------------------------------------
// OGG Vorbis: stb_vorbis (miniaudio does not bundle a Vorbis decoder)
// -----------------------------------------------------------------------------

bool SoundBuffer::loadOgg(const fs::path& path) {
    std::string pathStr = internal::pathToUtf8(path);
    // stb_vorbis has no UTF-8 filename mode on Windows — open the FILE*
    // ourselves (wide API) and hand it over (close_handle_on_close=TRUE).
    FILE* f = internal::openFile(path, "rb");
    if (!f) {
        printf("SoundBuffer: failed to open %s\n", pathStr.c_str());
        return false;
    }
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_file(f, 1, &error, nullptr);
    if (!vorbis) {
        fclose(f);
        printf("SoundBuffer: failed to open %s (error=%d)\n", pathStr.c_str(), error);
        return false;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    channels = info.channels;
    sampleRate = info.sample_rate;
    numSamples = stb_vorbis_stream_length_in_samples(vorbis);

    samples.resize(numSamples * channels);

    int decoded = stb_vorbis_get_samples_float_interleaved(
        vorbis, channels, samples.data(), static_cast<int>(samples.size()));

    stb_vorbis_close(vorbis);

    printf("SoundBuffer: loaded %s (%d ch, %d Hz, %zu samples)\n",
           pathStr.c_str(), channels, sampleRate, numSamples);

    return decoded > 0;
}

// -----------------------------------------------------------------------------
// WAV / MP3 / FLAC: routed through ma_decoder
// -----------------------------------------------------------------------------

bool SoundBuffer::loadWav(const fs::path& path) {
    return decodeFileWithMiniaudio(path, ma_encoding_format_wav, "WAV", *this);
}

bool SoundBuffer::loadMp3(const fs::path& path) {
    return decodeFileWithMiniaudio(path, ma_encoding_format_mp3, "MP3", *this);
}

bool SoundBuffer::loadFlac(const fs::path& path) {
    return decodeFileWithMiniaudio(path, ma_encoding_format_flac, "FLAC", *this);
}

bool SoundBuffer::loadWavFromMemory(const void* data, size_t dataSize) {
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_wav, "WAV", *this);
}

bool SoundBuffer::loadMp3FromMemory(const void* data, size_t dataSize) {
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_mp3, "MP3", *this);
}

bool SoundBuffer::loadFlacFromMemory(const void* data, size_t dataSize) {
    return decodeMemoryWithMiniaudio(data, dataSize, ma_encoding_format_flac, "FLAC", *this);
}

bool SoundBuffer::loadOggFromMemory(const void* data, size_t dataSize) {
    if (data == nullptr || dataSize == 0) {
        printf("SoundBuffer: empty memory range for OGG decode\n");
        return false;
    }
    int error = 0;
    stb_vorbis* vorbis = stb_vorbis_open_memory(
        static_cast<const unsigned char*>(data), static_cast<int>(dataSize),
        &error, nullptr);
    if (!vorbis) {
        printf("SoundBuffer: failed to decode OGG from memory (error=%d)\n", error);
        return false;
    }

    stb_vorbis_info info = stb_vorbis_get_info(vorbis);
    channels = info.channels;
    sampleRate = info.sample_rate;
    numSamples = stb_vorbis_stream_length_in_samples(vorbis);

    samples.resize(numSamples * channels);
    int decoded = stb_vorbis_get_samples_float_interleaved(
        vorbis, channels, samples.data(), static_cast<int>(samples.size()));

    stb_vorbis_close(vorbis);
    printf("SoundBuffer: decoded OGG from memory (%d ch, %d Hz, %zu samples)\n",
           channels, sampleRate, numSamples);
    return decoded > 0;
}

// -----------------------------------------------------------------------------
// Auto-detect by extension. Mirrors the dispatch in Sound::load() so callers
// that already have a SoundBuffer (e.g., for sharing across multiple Sounds)
// can use it directly.
// -----------------------------------------------------------------------------

bool SoundBuffer::load(const fs::path& path) {
    // Lowercase the extension once
    std::string ext = path.extension().string();
    if (!ext.empty() && ext[0] == '.') ext.erase(0, 1);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);

    if (ext == "wav")  return loadWav(path);
    if (ext == "mp3")  return loadMp3(path);
    if (ext == "ogg")  return loadOgg(path);
    if (ext == "flac") return loadFlac(path);
    if (ext == "aac" || ext == "m4a") return loadAac(path);

    printf("SoundBuffer: unsupported extension '.%s' for %s\n",
           ext.c_str(), internal::pathToUtf8(path).c_str());
    return false;
}

} // namespace trussc
