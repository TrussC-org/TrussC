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

    void EnsureMFStartup() {
        static std::once_flag flag;
        std::call_once(flag, []() {
            HRESULT hr = MFStartup(MF_VERSION);
            if (FAILED(hr)) {
                printf("tcSound_win: MFStartup failed (0x%08X)\n", hr);
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

bool SoundBuffer::loadAac(const std::string& path) {
    EnsureMFStartup();

    // Convert path to wide string
    int size_needed = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), NULL, 0);
    std::wstring wpath(size_needed, 0);
    MultiByteToWideChar(CP_UTF8, 0, path.c_str(), (int)path.size(), &wpath[0], size_needed);

    IMFSourceReader* pReader = NULL;
    HRESULT hr = MFCreateSourceReaderFromURL(wpath.c_str(), NULL, &pReader);

    if (FAILED(hr)) {
        printf("SoundBuffer: loadAac failed to open %s (0x%08X)\n", path.c_str(), hr);
        return false;
    }

    bool result = ReadFromSourceReader(pReader, this);
    SafeRelease(&pReader);

    if (result) {
        printf("SoundBuffer: loaded AAC %s (%d ch, %d Hz, %zu samples)\n",
               path.c_str(), (int)channels, (int)sampleRate, (size_t)numSamples);
    } else {
        printf("SoundBuffer: failed to decode AAC %s\n", path.c_str());
    }

    return result;
}

bool SoundBuffer::loadAacFromMemory(const void* data, size_t dataSize) {
    EnsureMFStartup();

    IStream* pStream = SHCreateMemStream((const BYTE*)data, (UINT)dataSize);
    if (!pStream) {
        printf("SoundBuffer: loadAacFromMemory failed to create stream\n");
        return false;
    }

    IMFByteStream* pByteStream = NULL;
    HRESULT hr = MFCreateMFByteStreamOnStream(pStream, &pByteStream);
    pStream->Release();  // MFByteStream holds a reference

    if (FAILED(hr)) {
        printf("SoundBuffer: loadAacFromMemory failed to create MF byte stream\n");
        return false;
    }

    IMFSourceReader* pReader = NULL;
    hr = MFCreateSourceReaderFromByteStream(pByteStream, NULL, &pReader);
    SafeRelease(&pByteStream);

    if (FAILED(hr)) {
        printf("SoundBuffer: loadAacFromMemory failed to create SourceReader\n");
        return false;
    }

    bool result = ReadFromSourceReader(pReader, this);
    SafeRelease(&pReader);

    if (result) {
        printf("SoundBuffer: decoded AAC from memory (%d ch, %d Hz, %zu samples)\n",
               (int)channels, (int)sampleRate, (size_t)numSamples);
    } else {
        printf("SoundBuffer: failed to decode AAC from memory\n");
    }

    return result;
}

} // namespace trussc
