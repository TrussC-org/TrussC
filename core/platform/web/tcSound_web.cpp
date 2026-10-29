// =============================================================================
// tcSound_web.cpp - AAC decoding for Emscripten/WebAssembly
// =============================================================================
// Uses Web Audio API's decodeAudioData() for AAC/M4A decoding.
// loadAac() yields via Asyncify until decoded PCM is ready, including in setup().

#ifdef __EMSCRIPTEN__

#include <TrussC.h>
#include "tc/sound/internal/tcAudioDecodeBudget.h"
#include <emscripten.h>
#include <algorithm>
#include <climits>
#include <cstring>

namespace trussc {

// Each invocation owns a separate slot. Removing a timed-out slot invalidates
// only that promise; a late settlement cannot overwrite another decode.
EM_JS(int, decodeAacFile, (const char* pathPtr, double encodedCap, int channelCap, int rateCap), {
    var slots = Module['trusscAacSlots'] || (Module['trusscAacSlots'] = new Map());
    // C++ receives a signed wasm i32. Recycle vacant IDs at its boundary;
    // active slots and late promises remain protected by object identity.
    var slotLimit = 0x7fffffff;
    var id = ((Module['trusscAacNext'] || 0) % slotLimit) + 1;
    while (slots.has(id)) id = id === slotLimit ? 1 : id + 1;
    Module['trusscAacNext'] = id;
    var decode = {complete: false, success: false, data: null};
    slots.set(id, decode);
    try {
        // Bound (security): check encoded input before browser decode and format before any TrussC copy. Overflow rejects the whole sound and is counted on completion.
        var size = FS.stat(UTF8ToString(pathPtr)).size;
        if (!Number.isSafeInteger(size) || size < 0 || size > encodedCap) {
            decode.bound = 1; decode.needed = size; decode.capacity = encodedCap; decode.complete = true; return id;
        }
        var fileData = FS.readFile(UTF8ToString(pathPtr));
        if (fileData.byteLength > encodedCap) {
            decode.bound = 1; decode.needed = fileData.byteLength; decode.capacity = encodedCap; decode.complete = true; return id;
        }
        var arrayBuffer = fileData.buffer;
        var OfflineContext = globalThis.OfflineAudioContext || globalThis.webkitOfflineAudioContext;
        var audioCtx = new OfflineContext(2, 1, 44100);
        slots.set(id, decode);
        audioCtx.decodeAudioData(arrayBuffer).then(function(buffer) {
            if (slots.get(id) !== decode) return;
            decode.channels = buffer.numberOfChannels;
            decode.sampleRate = buffer.sampleRate;
            decode.length = buffer.length;
            var bytes = buffer.length * buffer.numberOfChannels * 4;
            if (!Number.isSafeInteger(bytes) || !Number.isInteger(buffer.length) || buffer.length < 1 ||
                !Number.isInteger(buffer.numberOfChannels) || buffer.numberOfChannels < 1 || buffer.numberOfChannels > channelCap ||
                !Number.isFinite(buffer.sampleRate) || buffer.sampleRate < 1 || buffer.sampleRate > rateCap) {
                decode.bound = 2; decode.needed = bytes; decode.capacity = Number.MAX_SAFE_INTEGER;
                if (buffer.numberOfChannels > channelCap) { decode.bound = 3; decode.needed = buffer.numberOfChannels; decode.capacity = channelCap; }
                if (buffer.sampleRate > rateCap) { decode.bound = 4; decode.needed = buffer.sampleRate; decode.capacity = rateCap; }
                decode.complete = true; return;
            }
            // Keep the AudioBuffer only: copy its channels straight to wasm.
            decode.data = buffer;
            decode.success = true;
            decode.complete = true;
        }).catch(function() {
            if (slots.get(id) === decode) decode.complete = true;
        });
        fileData = null; arrayBuffer = null;
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
EM_JS(double, getAacBoundNeeded, (int slot), { return Module['trusscAacSlots'].get(slot).needed || 0; });
EM_JS(double, getAacBoundCapacity, (int slot), { return Module['trusscAacSlots'].get(slot).capacity || 0; });
EM_JS(int, isAacDecodeBound, (int slot), { return Module['trusscAacSlots'].get(slot).bound || 0; });
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
    if (!data) return;
    var channels = data.numberOfChannels;
    for (var ch = 0; ch < channels; ++ch) {
        var samples = data.getChannelData(ch);
        for (var i = 0; i < data.length; ++i) HEAPF32[(outPtr >>> 2) + i * channels + ch] = samples[i];
    }
});

// -----------------------------------------------------------------------------
// SoundBuffer::loadAacPlatform - wait for decoded PCM at load time
// -----------------------------------------------------------------------------
LoadResult SoundBuffer::loadAacPlatform(const fs::path& path) {
    const auto pathStr = internal::pathToUtf8(path);
    const double deadline = emscripten_get_now() + internal::webLoadTimeoutMSecs;
    // Start async decode
    const int slot = decodeAacFile(pathStr.c_str(), double(internal::audioEncodedLimit), internal::audioChannelLimit, internal::audioRateLimit);
    if (!slot) {
        logError("SoundBuffer") << "Failed to start AAC decode: " << pathStr;
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
                                 std::function<void(LoadResult)> complete, const fs::path& path,
                                 const std::shared_ptr<internal::ResourceLoadState>& owner) {
    struct Run {
        std::shared_ptr<SoundBuffer> decoded;
        std::function<void(LoadResult)> complete;
        fs::path path;
        int slot;
        double deadline;
        std::weak_ptr<internal::ResourceLoadState> owner;
        std::shared_ptr<std::function<void()>> stop;
        bool done = false;
        void finish(LoadResult result) {
            if (done) return;
            done = true;
            releaseAacDecode(slot);
            if (auto state = owner.lock()) state->cancelBackend = {};
            owner.reset();
            stop.reset();
            decoded.reset();
            auto callback = std::move(complete);
            callback(std::move(result));
        }
        static void poll(void* data) {
            auto* run = static_cast<Run*>(data);
            if (run->done) { delete run; return; }
            LoadResult result;
            try {
                if (isAacDecodeComplete(run->slot)) result = run->decoded->finishAacWebDecode(run->slot, run->path);
                else if (emscripten_get_now() >= run->deadline) result = internal::reportWebLoadTimeout(run->path, "SoundBuffer");
                else { emscripten_async_call(poll, run, 10); return; }
            } catch (...) {
                result.error = LoadError::Unknown;
                result.message.clear();
            }
            run->finish(std::move(result));
            delete run;
        }
    };
    const auto pathStr = internal::pathToUtf8(path);
    const int slot = decodeAacFile(pathStr.c_str(), double(internal::audioEncodedLimit), internal::audioChannelLimit, internal::audioRateLimit);
    if (!slot) {
        logError("SoundBuffer") << "Failed to start AAC decode: " << pathStr;
        complete(LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr));
        return;
    }
    Run* run = nullptr;
    try {
        // Keep completion available if copying the callback/path fails too.
        run = new (std::nothrow) Run{decoded, complete, path, slot,
                                   emscripten_get_now() + internal::webLoadTimeoutMSecs, owner, {}};
    } catch (const std::bad_alloc&) {}
    if (!run) {
        releaseAacDecode(slot);
        logError("SoundBuffer") << "audio allocation failed; previous PCM kept";
        complete(LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed"));
        return;
    }
    try {
        run->stop = internal::registerResourceStop([run] { run->finish(LoadResult::fail(LoadError::Cancelled)); });
        owner->cancelBackend = [run] { run->finish(LoadResult::fail(LoadError::Cancelled)); };
    } catch (const std::bad_alloc&) {
        run->finish(LoadResult::fail(LoadError::Unknown));
        delete run;
        return;
    }
    emscripten_async_call(Run::poll, run, 0);
}

LoadResult SoundBuffer::finishAacWebDecode(int slot, const fs::path& path) {
    const auto pathStr = internal::pathToUtf8(path);
    internal::AudioDecodeBudget budget;
    if (isAacDecodeBound(slot)) {
        const int kind = isAacDecodeBound(slot);
        const double needed = getAacBoundNeeded(slot);
        budget.reject(kind == 1 ? "encoded audio bytes" : kind == 3 ? "audio channels" :
                      kind == 4 ? "audio sample rate" : "decoded audio bytes",
                      std::isfinite(needed) && needed >= 0 && needed < double(UINT64_MAX) ? uint64_t(needed) : UINT64_MAX,
                      uint64_t(getAacBoundCapacity(slot)));
        return LoadResult::fail(LoadError::DecodeFailed, "AAC input or format rejected; use SoundStream::loadStream()");
    }
    if (!isAacDecodeSuccess(slot)) {
        logError("SoundBuffer") << "Failed to decode AAC: " << pathStr;
        return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
    }

    // Get decoded data
    int srcChannels = getAacChannels(slot);
    int srcSampleRate = getAacSampleRate(slot);
    int srcNumSamples = getAacLength(slot);
    size_t srcTotal = 0;
    if (srcNumSamples < 1 || !budget.format(srcChannels, srcSampleRate) ||
        !internal::interleavedSampleCount((uint64_t)srcNumSamples, srcChannels,
                                          std::min(samples.max_size(), (size_t)INT_MAX),
                                          srcTotal)) {
        logWarning("SoundBuffer") << "Unusable decoded AAC (" << srcChannels << " ch, "
                                  << srcSampleRate << " Hz, " << srcNumSamples
                                  << " samples): " << pathStr;
        return LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed: " + pathStr);
    }

    if (!budget.pcm(uint64_t(srcNumSamples), unsigned(srcChannels)))
        return LoadResult::fail(LoadError::DecodeFailed, "AAC length out of range");
    internal::noticeAudioDecodedBytes(srcTotal * sizeof(float));
    std::vector<float> decoded;

    // Target sample rate (AudioEngine's rate)
    const int targetSampleRate = 44100;

    if (srcSampleRate == targetSampleRate) {
        if (!internal::growAudioVector(decoded, srcTotal, budget))
            return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed; use SoundStream::loadStream()");
        decoded.resize(srcTotal);
        copyAacData(slot, decoded.data(), (int)srcTotal);
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
            budget.reject("resampled audio frames", uint64_t(newFrames), budget.limit() / sizeof(float) / unsigned(srcChannels));
            return LoadResult::fail(LoadError::DecodeFailed, "AAC length rejected; use SoundStream::loadStream()");
        }
        std::vector<float> srcSamples;
        if (!internal::growAudioVector(srcSamples, srcTotal, budget))
            return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed; use SoundStream::loadStream()");
        srcSamples.resize(srcTotal);

        // First, get source data
        copyAacData(slot, srcSamples.data(), (int)srcTotal);

        // Resample with linear interpolation
        if (!internal::growAudioVector(decoded, newTotal, budget))
            return LoadResult::fail(LoadError::DecodeFailed, "AAC allocation failed; use SoundStream::loadStream()");
        decoded.resize(newTotal);

        double srcRatio = (double)srcNumSamples / newNumSamples;
        for (size_t i = 0; i < newNumSamples; i++) {
            double srcPos = i * srcRatio;
            size_t srcIdx = (size_t)srcPos;
            double frac = srcPos - srcIdx;
            size_t nextIdx = std::min(srcIdx + 1, (size_t)(srcNumSamples - 1));

            for (int ch = 0; ch < srcChannels; ch++) {
                float s0 = srcSamples[srcIdx * srcChannels + ch];
                float s1 = srcSamples[nextIdx * srcChannels + ch];
                decoded[i * srcChannels + ch] = s0 + (s1 - s0) * frac;
            }
        }

        logVerbose("SoundBuffer") << "resampled " << srcSampleRate << " Hz -> "
                                  << targetSampleRate << " Hz (" << srcNumSamples << " -> "
                                  << newNumSamples << " samples)";
    }

    channels = srcChannels;
    sampleRate = targetSampleRate;
    numSamples = decoded.size() / size_t(srcChannels);
    internal::noticeAudioDecodedBytes(decoded.size() * sizeof(float));
    samples = std::move(decoded);
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
