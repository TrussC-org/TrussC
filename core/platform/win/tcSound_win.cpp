// =============================================================================
// tcSound_win.cpp - Windows AAC decoding using Media Foundation
// =============================================================================

#include "tc/sound/internal/tcAudioDecode.h"
#include <cstdio>
#include <mutex>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <propvarutil.h>
#include <shlwapi.h>

#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "shlwapi.lib")

namespace trussc {

namespace {
    template <class T> void SafeRelease(T **ppT) {
        if (*ppT) {
            (*ppT)->Release();
            *ppT = NULL;
        }
    }

    // "0x8007000E" — HRESULTs read best in hex.
    std::string hrHex(HRESULT hr) {
        char buf[16];
        snprintf(buf, sizeof(buf), "0x%08X", (unsigned)hr);
        return buf;
    }

    void EnsureMFStartup() {
        static std::once_flag flag;
        HRESULT hr = S_OK;
        std::call_once(flag, [&]() { hr = MFStartup(MF_VERSION); });
        // Only the caller that performed startup reports its failure, after
        // call_once has completed so an onLog listener can load AAC again.
        if (FAILED(hr)) {
            logError("SoundBuffer") << "MFStartup failed (" << hrHex(hr) << ")";
        }
    }

    // Common logic for configuring Source Reader and reading samples
    bool ReadFromSourceReader(IMFSourceReader* pReader, SoundBuffer* buffer, internal::AudioDecodeStatus& status,
                              const std::string& input = "memory input") {
        HRESULT hr = S_OK;
        IMFMediaType* pPartialType = NULL;
        IMFMediaType* pUncompressedAudioType = NULL;

        // Select the first audio stream
        hr = pReader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (FAILED(hr)) return false;

        hr = pReader->SetStreamSelection(MF_SOURCE_READER_FIRST_AUDIO_STREAM, TRUE);
        if (FAILED(hr)) return false;

        // Create a partial media type that specifies uncompressed PCM audio
        hr = MFCreateMediaType(&pPartialType);
        if (FAILED(hr)) return false;

        hr = pPartialType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
        if (FAILED(hr)) { SafeRelease(&pPartialType); return false; }

        hr = pPartialType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_Float); // Use Float for TrussC
        if (FAILED(hr)) { SafeRelease(&pPartialType); return false; }

        // Set the current media type
        hr = pReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, NULL, pPartialType);
        SafeRelease(&pPartialType);
        if (FAILED(hr)) return false;

        // Get the complete uncompressed format
        hr = pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &pUncompressedAudioType);
        if (FAILED(hr)) return false;

        UINT32 channels = 0;
        UINT32 sampleRate = 0;
        hr = pUncompressedAudioType->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &channels);
        if (FAILED(hr)) { SafeRelease(&pUncompressedAudioType); return false; }

        hr = pUncompressedAudioType->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &sampleRate);
        if (FAILED(hr)) { SafeRelease(&pUncompressedAudioType); return false; }

        SafeRelease(&pUncompressedAudioType);
        if (!channels || channels > INT_MAX || !sampleRate || !status.sampleRateFits(sampleRate, input)) return false;

        // Read samples
        std::vector<float> allSamples;
        bool ended = false;
        while (true) {
            IMFSample* pSample = NULL;
            DWORD dwFlags = 0;

            hr = pReader->ReadSample(
                MF_SOURCE_READER_FIRST_AUDIO_STREAM,
                0,
                NULL,
                &dwFlags,
                NULL,
                &pSample
            );

            if (FAILED(hr) || (dwFlags & MF_SOURCE_READERF_ERROR)) { SafeRelease(&pSample); break; }
            if (dwFlags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                IMFMediaType* changed = nullptr;
                UINT32 changedChannels = 0, changedRate = 0;
                GUID changedSubtype{};
                const bool compatible = SUCCEEDED(pReader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_AUDIO_STREAM, &changed)) && changed &&
                    SUCCEEDED(changed->GetUINT32(MF_MT_AUDIO_NUM_CHANNELS, &changedChannels)) &&
                    SUCCEEDED(changed->GetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND, &changedRate)) &&
                    SUCCEEDED(changed->GetGUID(MF_MT_SUBTYPE, &changedSubtype)) &&
                    changedChannels == channels && changedRate == sampleRate && IsEqualGUID(changedSubtype, MFAudioFormat_Float);
                SafeRelease(&changed);
                // A notification can refine compressed metadata without
                // changing decoded PCM. Keep that supported transition.
                if (!compatible) {
                    status.reject("AAC decoded format change", changedChannels, channels);
                    SafeRelease(&pSample);
                    return false;
                }
            }
            if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) ended = true;
            if (pSample == NULL) {
                if (ended) break;
                continue;
            }

            DWORD sampleBytes = 0;
            if (FAILED(pSample->GetTotalLength(&sampleBytes)) || sampleBytes / sizeof(float) % channels || sampleBytes % sizeof(float)) {
                SafeRelease(&pSample);
                return false;
            }
            IMFMediaBuffer* pBuffer = NULL;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);

            if (SUCCEEDED(hr)) {
                BYTE* pAudioData = NULL;
                DWORD cbBuffer = 0;
                hr = pBuffer->Lock(&pAudioData, NULL, &cbBuffer);

                if (SUCCEEDED(hr)) {
                    const bool appended = cbBuffer == sampleBytes &&
                        internal::appendAudio(allSamples, reinterpret_cast<const float*>(pAudioData),
                                              cbBuffer / sizeof(float), status);
                    pBuffer->Unlock();
                    if (!appended) hr = E_FAIL;
                }
                SafeRelease(&pBuffer);
            }
            SafeRelease(&pSample);
            if (FAILED(hr)) return false;
            if (ended) break;
        }

        if (!ended || allSamples.empty()) return false;
        buffer->channels = int(channels);
        buffer->sampleRate = int(sampleRate);
        buffer->samples = std::move(allSamples);
        buffer->numSamples = buffer->samples.size() / buffer->channels;

        return true;
    }
}

namespace {
LoadResult decodeAacMemory(const void* data, size_t dataSize, SoundBuffer& out) {
    internal::AudioDecodeStatus status;
    if (!data || !dataSize)
        return LoadResult::fail(LoadError::DecodeFailed, "empty AAC memory input");
    // Bound (abi): SHCreateMemStream takes a UINT byte length; larger inputs fail before narrowing.
    if (dataSize > size_t(UINT_MAX)) {
        logError("SoundBuffer") << "AAC memory input size does not fit the Windows stream's UINT byte length";
        return LoadResult::fail(LoadError::DecodeFailed, "AAC input size cannot be represented");
    }
    EnsureMFStartup();
    IStream* stream = SHCreateMemStream(static_cast<const BYTE*>(data), UINT(dataSize));
    if (!stream) {
        logError("SoundBuffer") << "failed to create AAC memory stream; previous PCM kept";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to create AAC memory stream");
    }
    IMFByteStream* bytes = nullptr;
    HRESULT result = MFCreateMFByteStreamOnStream(stream, &bytes);
    SafeRelease(&stream);
    if (FAILED(result)) {
        logError("SoundBuffer") << "failed to create AAC byte stream; previous PCM kept";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to create AAC byte stream");
    }
    IMFSourceReader* reader = nullptr;
    result = MFCreateSourceReaderFromByteStream(bytes, nullptr, &reader);
    SafeRelease(&bytes);
    if (FAILED(result)) {
        logError("SoundBuffer") << "failed to create AAC source reader; previous PCM kept";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to create AAC source reader");
    }
    const bool ok = ReadFromSourceReader(reader, &out, status);
    SafeRelease(&reader);
    if (!ok && !status.rejected && !status.allocationFailed) logError("SoundBuffer") << "AAC decoder failed; previous PCM kept";
    return ok ? LoadResult::success() : LoadResult::fail(LoadError::DecodeFailed,
        "AAC decode failed; load long audio with SoundStream::loadStream()");
}
} // namespace

LoadResult SoundBuffer::loadAacPlatform(const fs::path& path) {
    internal::AudioDecodeStatus status;
    EnsureMFStartup();
    IMFSourceReader* reader = nullptr;
    HRESULT result = MFCreateSourceReaderFromURL(path.c_str(), nullptr, &reader);
    if (FAILED(result)) {
        logError("SoundBuffer") << "failed to open AAC file";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to open AAC file");
    }
    const bool ok = ReadFromSourceReader(reader, this, status, internal::pathToDisplayUtf8(path));
    SafeRelease(&reader);
    if (!ok && !status.rejected && !status.allocationFailed)
        logError("SoundBuffer") << "AAC decoder failed; previous PCM kept";
    return ok ? LoadResult::success() : LoadResult::fail(LoadError::DecodeFailed, "AAC decode failed");
}

LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    async_.cancel();
    internal::clearLoadName(path_);
    return decodeAacMemory(data, dataSize, *this);
}

} // namespace trussc
