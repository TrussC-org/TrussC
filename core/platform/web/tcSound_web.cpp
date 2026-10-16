// =============================================================================
// tcSound_web.cpp - AAC decoding for Emscripten/WebAssembly
// =============================================================================
// Uses Web Audio API's decodeAudioData() for AAC/M4A decoding.
// loadAac() yields via Asyncify until decoded PCM is ready, including in setup().

#ifdef __EMSCRIPTEN__

#include "tc/sound/tcSound.h"
#include "tc/utils/tcLog.h"
#include <emscripten.h>
#include <algorithm>
#include <climits>
#include <cstring>

namespace trussc {

// Each invocation owns a separate slot. Removing a timed-out slot invalidates
// only that promise; a late settlement cannot overwrite another decode.
EM_JS(int, decodeAacFile, (const char* pathPtr), {
    var slots = Module['trusscAacSlots'] || (Module['trusscAacSlots'] = new Map());
    // C++ receives a signed wasm i32. Recycle vacant IDs at its boundary;
    // active slots and late promises remain protected by object identity.
    var slotLimit = 0x7fffffff;
    var id = ((Module['trusscAacNext'] || 0) % slotLimit) + 1;
    while (slots.has(id)) id = id === slotLimit ? 1 : id + 1;
    Module['trusscAacNext'] = id;
    var decode = {complete: false, success: false, data: null};
    try {
        var fileData = FS.readFile(UTF8ToString(pathPtr));
        var arrayBuffer = fileData.buffer.slice(fileData.byteOffset, fileData.byteOffset + fileData.byteLength);
        var OfflineContext = globalThis.OfflineAudioContext || globalThis.webkitOfflineAudioContext;
        var audioCtx = new OfflineContext(2, 1, 44100);
        slots.set(id, decode);
        audioCtx.decodeAudioData(arrayBuffer).then(function(buffer) {
            if (slots.get(id) !== decode) return;
            decode.channels = buffer.numberOfChannels;
            decode.sampleRate = buffer.sampleRate;
            decode.length = buffer.length;
            var data = new Float32Array(buffer.length * buffer.numberOfChannels);
            var channels = [];
            for (var ch = 0; ch < buffer.numberOfChannels; ch++) channels.push(buffer.getChannelData(ch));
            for (var i = 0; i < buffer.length; i++) {
                for (var ch = 0; ch < buffer.numberOfChannels; ch++) data[i * buffer.numberOfChannels + ch] = channels[ch][i];
            }
            decode.data = data;
            decode.success = true;
            decode.complete = true;
        }).catch(function() {
            if (slots.get(id) === decode) decode.complete = true;
        });
        return id;
    } catch(e) {
        slots.delete(id);
        return 0;
    }
});
EM_JS(int, isAacDecodeComplete, (int slot), {
    var decode = Module['trusscAacSlots'].get(slot);
    return !decode || decode.complete ? 1 : 0;
});
EM_JS(int, isAacDecodeSuccess, (int slot), {
    var decode = Module['trusscAacSlots'].get(slot);
    return decode && decode.success ? 1 : 0;
});
EM_JS(int, getAacChannels, (int slot), { return Module['trusscAacSlots'].get(slot).channels || 0; });
EM_JS(int, getAacSampleRate, (int slot), { return Module['trusscAacSlots'].get(slot).sampleRate || 0; });
EM_JS(int, getAacLength, (int slot), { return Module['trusscAacSlots'].get(slot).length || 0; });
EM_JS(void, releaseAacDecode, (int slot), { Module['trusscAacSlots'].delete(slot); });
EM_JS(void, copyAacData, (int slot, float* outPtr, int totalSamples), {
    var data = Module['trusscAacSlots'].get(slot).data;
    if (data) HEAPF32.set(data.subarray(0, totalSamples), outPtr >> 2);
});

// -----------------------------------------------------------------------------
// SoundBuffer::loadAacPlatform - wait for decoded PCM at load time
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAacPlatform(const fs::path& path) {
    const auto pathStr = internal::pathToUtf8(path);
    const double deadline = emscripten_get_now() + internal::webLoadTimeoutMSecs;
    // Start async decode
    const int slot = decodeAacFile(pathStr.c_str());
    if (!slot) {
        logWarning("SoundBuffer") << "Failed to start AAC decode: " << pathStr;
        return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
    }

    while (!isAacDecodeComplete(slot)) {
        if (emscripten_get_now() >= deadline) {
            releaseAacDecode(slot);
            return internal::reportWebLoadTimeout(path, "SoundBuffer");
        }
        emscripten_sleep(10);
    }
    auto result = finishAacWebDecode(slot, path);
    releaseAacDecode(slot);
    return result;
}

void SoundBuffer::loadAacWebAsync(std::shared_ptr<SoundBuffer> decoded,
                                 std::function<void(LoadResult)> complete, const fs::path& path) {
    struct Run {
        std::shared_ptr<SoundBuffer> decoded;
        std::function<void(LoadResult)> complete;
        fs::path path;
        int slot;
        double deadline;
        static void poll(void* data) {
            auto* run = static_cast<Run*>(data);
            if (isAacDecodeComplete(run->slot)) {
                auto result = run->decoded->finishAacWebDecode(run->slot, run->path);
                releaseAacDecode(run->slot);
                run->complete(std::move(result));
                delete run;
            } else if (emscripten_get_now() >= run->deadline) {
                releaseAacDecode(run->slot);
                run->complete(internal::reportWebLoadTimeout(run->path, "SoundBuffer"));
                delete run;
            } else {
                emscripten_async_call(poll, run, 10);
            }
        }
    };
    const auto pathStr = internal::pathToUtf8(path);
    const int slot = decodeAacFile(pathStr.c_str());
    if (!slot) {
        logWarning("SoundBuffer") << "Failed to start AAC decode: " << pathStr;
        complete(LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr));
        return;
    }
    auto* run = new Run{std::move(decoded), std::move(complete), path, slot,
                        emscripten_get_now() + internal::webLoadTimeoutMSecs};
    emscripten_async_call(Run::poll, run, 0);
}

LoadResult SoundBuffer::finishAacWebDecode(int slot, const fs::path& path) {
    const auto pathStr = internal::pathToUtf8(path);
    if (!isAacDecodeSuccess(slot)) {
        logWarning("SoundBuffer") << "Failed to decode AAC: " << pathStr;
        return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
    }

    // Get decoded data
    int srcChannels = getAacChannels(slot);
    int srcSampleRate = getAacSampleRate(slot);
    int srcNumSamples = getAacLength(slot);
    size_t srcTotal = 0;
    if (srcNumSamples < 1 || srcSampleRate < 1 ||
        !internal::interleavedSampleCount((uint64_t)srcNumSamples, srcChannels,
                                          std::min(samples.max_size(), (size_t)INT_MAX),
                                          srcTotal)) {
        logWarning("SoundBuffer") << "Unusable decoded AAC (" << srcChannels << " ch, "
                                  << srcSampleRate << " Hz, " << srcNumSamples
                                  << " samples): " << pathStr;
        return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
    }

    // Target sample rate (AudioEngine's rate)
    const int targetSampleRate = 44100;

    if (srcSampleRate == targetSampleRate) {
        // No resampling needed. Exception catching is off on the web, so a
        // failed allocation would abort: check it first.
        if (!internal::allocationFits(srcTotal * sizeof(float))) {
            logWarning("SoundBuffer") << "Out of memory for decoded AAC (" << srcTotal
                                      << " samples): " << pathStr;
            return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
        }
        channels = srcChannels;
        sampleRate = srcSampleRate;
        numSamples = srcNumSamples;

        samples.resize(srcTotal);
        copyAacData(slot, samples.data(), (int)srcTotal);
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
            logWarning("SoundBuffer") << "Resampled AAC length out of range: " << pathStr;
            return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
        }
        // Exception catching is off on the web, so a failed allocation would
        // abort: check each one first. The resampled buffer is checked while
        // the source one is held, as both are held at once.
        if (!internal::allocationFits(srcTotal * sizeof(float))) {
            logWarning("SoundBuffer") << "Out of memory for decoded AAC (" << srcTotal
                                      << " samples): " << pathStr;
            return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
        }
        std::vector<float> srcSamples(srcTotal);
        if (!internal::allocationFits(newTotal * sizeof(float))) {
            logWarning("SoundBuffer") << "Out of memory for resampled AAC (" << newTotal
                                      << " samples): " << pathStr;
            return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
        }

        // First, get source data
        copyAacData(slot, srcSamples.data(), (int)srcTotal);

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

    return LoadResult::success();
}

// -----------------------------------------------------------------------------
// SoundBuffer::loadAacFromMemory - Web implementation
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
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
