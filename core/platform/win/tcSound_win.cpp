// =============================================================================
// tcSound_win.cpp - Windows AAC decoding using Media Foundation
// =============================================================================

#include "tc/sound/tcSound.h"
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
        std::call_once(flag, []() {
            HRESULT hr = MFStartup(MF_VERSION);
            if (FAILED(hr)) {
                logError("SoundBuffer") << "MFStartup failed (" << hrHex(hr) << ")";
            }
        });
    }

    // Common logic for configuring Source Reader and reading samples
    bool ReadFromSourceReader(IMFSourceReader* pReader, SoundBuffer* buffer) {
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

        buffer->channels = channels;
        buffer->sampleRate = sampleRate;

        SafeRelease(&pUncompressedAudioType);

        // Read samples
        std::vector<float> allSamples;
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

            if (FAILED(hr)) break;
            if (dwFlags & MF_SOURCE_READERF_ENDOFSTREAM) {
                SafeRelease(&pSample);
                break;
            }

            if (pSample == NULL) continue;

            IMFMediaBuffer* pBuffer = NULL;
            hr = pSample->ConvertToContiguousBuffer(&pBuffer);

            if (SUCCEEDED(hr)) {
                BYTE* pAudioData = NULL;
                DWORD cbBuffer = 0;
                hr = pBuffer->Lock(&pAudioData, NULL, &cbBuffer);

                if (SUCCEEDED(hr)) {
                    // Assume float data (MFAudioFormat_Float)
                    int count = cbBuffer / sizeof(float);
                    allSamples.insert(allSamples.end(), (float*)pAudioData, (float*)pAudioData + count);
                    pBuffer->Unlock();
                }
                SafeRelease(&pBuffer);
            }
            SafeRelease(&pSample);
        }

        buffer->samples = std::move(allSamples);
        buffer->numSamples = buffer->samples.size() / buffer->channels;

        return true;
    }
}

LoadResult SoundBuffer::loadAac(const fs::path& path) {
    EnsureMFStartup();

    std::error_code ec;
    if (!fs::exists(path, ec)) {
        logError("SoundBuffer") << "file not found: " << path;
        return LoadResult::fail(LoadError::FileNotFound,
                                "file not found: " + internal::pathToDisplayUtf8(path));
    }

    // fs::path is already wide on Windows
    std::wstring wpath = path.wstring();

    IMFSourceReader* pReader = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), NULL, &pReader);

    if (FAILED(hr)) {
        logError("SoundBuffer") << "loadAac failed to open " << path
                                << " (" << hrHex(hr) << ")";
        return LoadResult::fail(LoadError::DecodeFailed,
                                "failed to open " + internal::pathToDisplayUtf8(path) +
                                " (hr=" + hrHex(hr) + ")");
    }

    bool result = ReadFromSourceReader(pReader, this);
    SafeRelease(&pReader);

    if (result) {
        logVerbose("SoundBuffer") << "loaded AAC " << path << " ("
                                  << (int)channels << " ch, " << (int)sampleRate << " Hz, "
                                  << (size_t)numSamples << " samples)";
    } else {
        logError("SoundBuffer") << "failed to decode AAC " << path;
    }

    return result ? LoadResult::success()
                  : LoadResult::fail(LoadError::DecodeFailed,
                                     "failed to decode AAC: " + internal::pathToDisplayUtf8(path));
}

LoadResult SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    EnsureMFStartup();

    IStream* pStream = SHCreateMemStream((const BYTE*)data, (UINT)dataSize);
    if (!pStream) {
        logError("SoundBuffer") << "loadAacFromMemory failed to create a memory stream";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to create memory stream");
    }

    IMFByteStream* pByteStream = NULL;
    HRESULT hr = MFCreateMFByteStreamOnStream(pStream, &pByteStream);
    pStream->Release();  // MFByteStream holds a reference

    if (FAILED(hr)) {
        logError("SoundBuffer") << "loadAacFromMemory failed to create an MF byte stream ("
                                << hrHex(hr) << ")";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to create MF byte stream");
    }

    IMFSourceReader* pReader = NULL;
    hr = MFCreateSourceReaderFromByteStream(pByteStream, NULL, &pReader);
    SafeRelease(&pByteStream);

    if (FAILED(hr)) {
        logError("SoundBuffer") << "loadAacFromMemory failed to create a SourceReader ("
                                << hrHex(hr) << ")";
        return LoadResult::fail(LoadError::DecodeFailed, "failed to create SourceReader");
    }

    bool result = ReadFromSourceReader(pReader, this);
    SafeRelease(&pReader);

    if (result) {
        logVerbose("SoundBuffer") << "decoded AAC from memory (" << (int)channels << " ch, "
                                  << (int)sampleRate << " Hz, " << (size_t)numSamples
                                  << " samples)";
    } else {
        logError("SoundBuffer") << "failed to decode AAC from memory";
    }

    return result ? LoadResult::success()
                  : LoadResult::fail(LoadError::DecodeFailed, "failed to decode AAC from memory");
}

} // namespace trussc
