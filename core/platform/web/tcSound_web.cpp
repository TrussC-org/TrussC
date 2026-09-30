// =============================================================================
// tcSound_web.cpp - AAC decoding for Emscripten/WebAssembly
// =============================================================================
// Uses Web Audio API's decodeAudioData() for AAC/M4A decoding.
// Loading is deferred until play() is called to avoid blocking setup().

#ifdef __EMSCRIPTEN__

#include "tc/sound/tcSound.h"
#include "tc/utils/tcLog.h"
#include <emscripten.h>
#include <algorithm>
#include <climits>
#include <cstring>

namespace trussc {

// Start async decode and wait for completion (blocking via emscripten_sleep)
EM_JS(int, decodeAacFile, (const char* pathPtr, float** outSamples, int* outChannels, int* outSampleRate, int* outNumSamples), {
    var path = UTF8ToString(pathPtr);

    // Reset result state
    window._trussc_aac_complete = false;
    window._trussc_aac_success = false;
    window._trussc_aac_channels = 0;
    window._trussc_aac_sampleRate = 0;
    window._trussc_aac_length = 0;
    window._trussc_aac_data = null;

    // Check if file exists in virtual FS
    var fileData = null;
    try {
        fileData = FS.readFile(path);
    } catch(e) {
        if (path.startsWith('data/')) {
            try {
                fileData = FS.readFile(path.substring(5));
            } catch(e2) {
                console.error('[SoundBuffer] AAC file not found:', path);
                return 0;
            }
        } else {
            console.error('[SoundBuffer] AAC file not found:', path);
            return 0;
        }
    }

    var arrayBuffer = fileData.buffer.slice(
        fileData.byteOffset,
        fileData.byteOffset + fileData.byteLength
    );

    var AudioContext = window.AudioContext || window.webkitAudioContext;
    if (!AudioContext) {
        console.error('[SoundBuffer] Web Audio API not supported');
        return 0;
    }

    var audioCtx = new AudioContext();

    audioCtx.decodeAudioData(arrayBuffer).then(function(audioBuffer) {
        window._trussc_aac_channels = audioBuffer.numberOfChannels;
        window._trussc_aac_sampleRate = audioBuffer.sampleRate;
        window._trussc_aac_length = audioBuffer.length;

        // Interleave channel data
        var totalSamples = audioBuffer.length * audioBuffer.numberOfChannels;
        var interleaved = new Float32Array(totalSamples);

        var channelData = [];
        for (var ch = 0; ch < audioBuffer.numberOfChannels; ch++) {
            channelData.push(audioBuffer.getChannelData(ch));
        }

        for (var i = 0; i < audioBuffer.length; i++) {
            for (var ch = 0; ch < audioBuffer.numberOfChannels; ch++) {
                interleaved[i * audioBuffer.numberOfChannels + ch] = channelData[ch][i];
            }
        }

        window._trussc_aac_data = interleaved;
        window._trussc_aac_success = true;
        window._trussc_aac_complete = true;

        audioCtx.close();
        console.log('[SoundBuffer] AAC decoded:', audioBuffer.numberOfChannels, 'ch,',
                    audioBuffer.sampleRate, 'Hz,', audioBuffer.length, 'samples');
    }).catch(function(err) {
        console.error('[SoundBuffer] Failed to decode AAC:', err);
        window._trussc_aac_complete = true;
        audioCtx.close();
    });

    return 1;  // Started decoding
});

// Check if decode is complete (non-blocking)
EM_JS(int, isAacDecodeComplete, (), {
    return window._trussc_aac_complete ? 1 : 0;
});

EM_JS(int, isAacDecodeSuccess, (), {
    return window._trussc_aac_success ? 1 : 0;
});

// Get decoded data info
EM_JS(int, getAacChannels, (), { return window._trussc_aac_channels || 0; });
EM_JS(int, getAacSampleRate, (), { return window._trussc_aac_sampleRate || 0; });
EM_JS(int, getAacLength, (), { return window._trussc_aac_length || 0; });

// Copy decoded data to buffer
EM_JS(void, copyAacData, (float* outPtr, int totalSamples), {
    var data = window._trussc_aac_data;
    if (!data) return;
    for (var i = 0; i < totalSamples && i < data.length; i++) {
        HEAPF32[(outPtr >> 2) + i] = data[i];
    }
    window._trussc_aac_data = null;  // Free JS memory
});

// -----------------------------------------------------------------------------
// SoundBuffer::loadAac - Web implementation (deferred loading)
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAac(const fs::path& path) {
    logVerbose("SoundBuffer") << "deferring AAC load: " << path.string() << " [Web]";

    // Save path for deferred loading (web paths are plain UTF-8 strings)
    deferredAacPath_ = path.string();

    // Set placeholder values so the app doesn't crash
    channels = 2;
    sampleRate = 44100;
    numSamples = 44100;  // 1 second placeholder
    samples.resize(numSamples * channels, 0.0f);

    return LoadResult::success();  // Will actually load when play() is called
}

// -----------------------------------------------------------------------------
// SoundBuffer::ensureAacLoaded - Complete deferred AAC loading (blocking)
// -----------------------------------------------------------------------------
void SoundBuffer::ensureAacLoaded() {
    if (deferredAacPath_.empty()) return;

    logVerbose("SoundBuffer") << "loading AAC now: " << deferredAacPath_ << " [Web]";

    // Start async decode
    if (!decodeAacFile(deferredAacPath_.c_str(), nullptr, nullptr, nullptr, nullptr)) {
        logWarning("SoundBuffer") << "Failed to start AAC decode: " << deferredAacPath_;
        deferredAacPath_.clear();
        return;
    }

    // Wait for decode to complete (blocking via emscripten_sleep)
    while (!isAacDecodeComplete()) {
        emscripten_sleep(10);  // Sleep 10ms, yield to browser
    }

    if (!isAacDecodeSuccess()) {
        logWarning("SoundBuffer") << "Failed to decode AAC: " << deferredAacPath_;
        deferredAacPath_.clear();
        return;
    }

    // Get decoded data
    int srcChannels = getAacChannels();
    int srcSampleRate = getAacSampleRate();
    int srcNumSamples = getAacLength();
    size_t srcTotal = 0;
    if (srcNumSamples < 1 || srcSampleRate < 1 ||
        !internal::interleavedSampleCount((uint64_t)srcNumSamples, srcChannels,
                                          std::min(samples.max_size(), (size_t)INT_MAX),
                                          srcTotal)) {
        logWarning("SoundBuffer") << "Unusable decoded AAC (" << srcChannels << " ch, "
                                  << srcSampleRate << " Hz, " << srcNumSamples
                                  << " samples): " << deferredAacPath_;
        deferredAacPath_.clear();
        return;
    }

    // Target sample rate (AudioEngine's rate)
    const int targetSampleRate = 44100;

    if (srcSampleRate == targetSampleRate) {
        // No resampling needed
        channels = srcChannels;
        sampleRate = srcSampleRate;
        numSamples = srcNumSamples;

        samples.resize(srcTotal);
        copyAacData(samples.data(), (int)srcTotal);
    } else {
        // Resample to target sample rate
        double ratio = (double)targetSampleRate / srcSampleRate;
        // Range-checked as a double first: the cast of an out-of-range
        // double to size_t is undefined.
        const double newFrames = srcNumSamples * ratio;
        size_t newNumSamples = 0;
        size_t newTotal = 0;
        if (newFrames < (double)samples.max_size()) newNumSamples = (size_t)newFrames;
        if (newNumSamples == 0 ||
            !internal::interleavedSampleCount(newNumSamples, srcChannels, samples.max_size(),
                                              newTotal)) {
            logWarning("SoundBuffer") << "Resampled AAC length out of range: " << deferredAacPath_;
            deferredAacPath_.clear();
            return;
        }

        // First, get source data
        std::vector<float> srcSamples(srcTotal);
        copyAacData(srcSamples.data(), (int)srcTotal);

        // Resample with linear interpolation
        channels = srcChannels;
        sampleRate = targetSampleRate;
        numSamples = newNumSamples;
        samples.resize(newTotal);

        double srcRatio = (double)srcNumSamples / newNumSamples;
        for (size_t i = 0; i < newNumSamples; i++) {
            double srcPos = i * srcRatio;
            size_t srcIdx = (size_t)srcPos;
            double frac = srcPos - srcIdx;
            size_t nextIdx = std::min(srcIdx + 1, (size_t)(srcNumSamples - 1));

            for (int ch = 0; ch < channels; ch++) {
                float s0 = srcSamples[srcIdx * channels + ch];
                float s1 = srcSamples[nextIdx * channels + ch];
                samples[i * channels + ch] = s0 + (s1 - s0) * frac;
            }
        }

        logVerbose("SoundBuffer") << "resampled " << srcSampleRate << " Hz -> "
                                  << targetSampleRate << " Hz (" << srcNumSamples << " -> "
                                  << newNumSamples << " samples)";
    }

    logVerbose("SoundBuffer") << "loaded AAC (" << channels << " ch, " << sampleRate << " Hz, "
                              << numSamples << " samples, duration="
                              << (sampleRate > 0 ? (float)numSamples / sampleRate : 0.0f)
                              << " s) [Web]";

    // Clear deferred path (loading complete)
    deferredAacPath_.clear();
}

// -----------------------------------------------------------------------------
// SoundBuffer::loadAacFromMemory - Web implementation
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    if (!data || dataSize == 0) {
        logWarning("SoundBuffer") << "loadAacFromMemory() called with empty data";
        return LoadResult::fail(LoadError::DecodeFailed, "empty memory range");
    }

    // TODO: Implement memory-based AAC decode for Web
    logWarning("SoundBuffer") << "loadAacFromMemory() not yet implemented for Web (use file path instead)";
    return LoadResult::fail(LoadError::UnsupportedFormat,
                            "AAC decode from memory not yet implemented for Web");
}

} // namespace trussc

#endif // __EMSCRIPTEN__
