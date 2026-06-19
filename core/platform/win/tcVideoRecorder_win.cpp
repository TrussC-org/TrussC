// =============================================================================
// tcVideoRecorder_win.cpp - Windows Media Foundation (IMFSinkWriter) impl
// =============================================================================
//
// Mirrors the macOS AVFoundation backend: it implements the three platform
// hooks declared in tc/video/tcVideoRecorder.h
//   - openPlatform()   : create an H.264 sink writer and begin writing
//   - appendPlatform() : encode one RGBA8 (top-down) frame
//   - closePlatform()  : finalize and flush the .mp4
//
// The encoder input type is RGB32 (BGRA in memory). The SinkWriter inserts the
// color-converter MFT automatically to reach the encoder's native NV12, so no
// manual conversion to YUV is needed here. Media Foundation treats RGB32 as
// bottom-up, while the recorder feeds RGBA top-down, so frames are flipped
// vertically (and swizzled R<->B) while copying into the sample buffer.
// =============================================================================

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>

#include "TrussC.h"

#include <cmath>
#include <string>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace trussc {

// ---------------------------------------------------------------------------
// Platform state (IMFSinkWriter holds the H.264 encoder + mp4 file sink)
// ---------------------------------------------------------------------------
struct VideoRecorderPlatformData {
    IMFSinkWriter* writer = nullptr;
    DWORD streamIndex = 0;
    int   width  = 0;
    int   height = 0;
    double fps   = 30.0;
    bool  mfStarted = false;  // this instance called MFStartup
    bool  failed = false;
};

namespace {
    // Split a float fps into an exact rational (num/den) for MF_MT_FRAME_RATE.
    void fpsToRatio(double fps, UINT32& num, UINT32& den) {
        if (fps <= 0.0) fps = 30.0;
        // Near-integer rates stay exact; otherwise keep 1/1000 precision.
        double rounded = std::floor(fps + 0.5);
        if (std::fabs(fps - rounded) < 1e-4) {
            num = (UINT32)rounded;
            den = 1;
        } else {
            num = (UINT32)std::floor(fps * 1000.0 + 0.5);
            den = 1000;
        }
    }
}

// ---------------------------------------------------------------------------
// openPlatform - create the H.264 sink writer and begin a writing session
// ---------------------------------------------------------------------------
bool VideoRecorder::openPlatform(const std::string& fullPath, int w, int h,
                                 float fps, int bitrate) {
    // Media Foundation startup (ref-counted internally; matched in close()).
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        logError("VideoRecorder") << "MFStartup failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        return false;
    }

    auto* pd = new VideoRecorderPlatformData();
    pd->mfStarted = true;
    pd->width  = w;
    pd->height = h;
    pd->fps    = (fps > 0.0f) ? fps : 30.0f;

    UINT32 fpsNum = 30, fpsDen = 1;
    fpsToRatio(pd->fps, fpsNum, fpsDen);

    // ~0.1 bits per pixel per second — same default as the macOS backend.
    int br = (bitrate > 0)
                 ? bitrate
                 : (int)((double)w * h * pd->fps * 0.1);

    // Overwrite any existing file (the sink writer otherwise appends/locks).
    std::wstring wpath;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0);
        if (n > 0) {
            wpath.resize(n - 1);
            MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, &wpath[0], n);
        }
    }
    DeleteFileW(wpath.c_str());

    IMFSinkWriter* writer = nullptr;
    hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, nullptr, &writer);
    if (FAILED(hr) || !writer) {
        logError("VideoRecorder") << "MFCreateSinkWriterFromURL failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        MFShutdown();
        delete pd;
        return false;
    }

    // --- Output media type: H.264 ---
    IMFMediaType* outType = nullptr;
    DWORD streamIndex = 0;
    hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)br);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE,
                                               MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, w, h);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE,
                                                fpsNum, fpsDen);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO,
                                                1, 1);
    if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &streamIndex);
    if (outType) outType->Release();
    if (FAILED(hr)) {
        logError("VideoRecorder") << "H.264 output type / AddStream failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        writer->Release();
        MFShutdown();
        delete pd;
        return false;
    }

    // --- Input media type: RGB32 (BGRA), top-down handled at copy time ---
    IMFMediaType* inType = nullptr;
    hr = MFCreateMediaType(&inType);
    if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_INTERLACE_MODE,
                                              MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(inType, MF_MT_FRAME_SIZE, w, h);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_FRAME_RATE,
                                                fpsNum, fpsDen);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(inType, MF_MT_PIXEL_ASPECT_RATIO,
                                                1, 1);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(streamIndex, inType, nullptr);
    if (inType) inType->Release();
    if (FAILED(hr)) {
        logError("VideoRecorder") << "RGB32 input type / SetInputMediaType failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        writer->Release();
        MFShutdown();
        delete pd;
        return false;
    }

    hr = writer->BeginWriting();
    if (FAILED(hr)) {
        logError("VideoRecorder") << "BeginWriting failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        writer->Release();
        MFShutdown();
        delete pd;
        return false;
    }

    pd->writer = writer;
    pd->streamIndex = streamIndex;
    platform_ = pd;
    return true;
}

// ---------------------------------------------------------------------------
// appendPlatform - encode one RGBA8 (top-down) frame
// ---------------------------------------------------------------------------
bool VideoRecorder::appendPlatform(const unsigned char* rgba, double timeSec) {
    VideoRecorderPlatformData* pd = platform_;
    if (!pd || !pd->writer || pd->failed) return false;

    const int w = pd->width;
    const int h = pd->height;
    const DWORD frameBytes = (DWORD)w * h * 4;

    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(frameBytes, &buffer);
    if (FAILED(hr) || !buffer) {
        logError("VideoRecorder") << "MFCreateMemoryBuffer failed";
        pd->failed = true;
        return false;
    }

    BYTE* dst = nullptr;
    hr = buffer->Lock(&dst, nullptr, nullptr);
    if (FAILED(hr)) {
        buffer->Release();
        pd->failed = true;
        return false;
    }

    // RGBA top-down -> BGRA bottom-up (MF RGB32 is bottom-up). Flipping here
    // keeps the recorded video upright, matching the macOS output.
    const int stride = w * 4;
    for (int y = 0; y < h; ++y) {
        const unsigned char* srow = rgba + (size_t)y * stride;
        BYTE* drow = dst + (size_t)(h - 1 - y) * stride;
        for (int x = 0; x < w; ++x) {
            drow[x * 4 + 0] = srow[x * 4 + 2];  // B
            drow[x * 4 + 1] = srow[x * 4 + 1];  // G
            drow[x * 4 + 2] = srow[x * 4 + 0];  // R
            drow[x * 4 + 3] = srow[x * 4 + 3];  // A
        }
    }
    buffer->Unlock();
    buffer->SetCurrentLength(frameBytes);

    IMFSample* sample = nullptr;
    hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    if (SUCCEEDED(hr)) {
        // MF time units are 100-nanosecond ticks (1e7 per second).
        LONGLONG t   = (LONGLONG)(timeSec * 1.0e7 + 0.5);
        LONGLONG dur = (LONGLONG)(1.0e7 / (pd->fps > 0 ? pd->fps : 30.0) + 0.5);
        sample->SetSampleTime(t);
        sample->SetSampleDuration(dur);
        hr = pd->writer->WriteSample(pd->streamIndex, sample);
    }

    if (sample) sample->Release();
    buffer->Release();

    if (FAILED(hr)) {
        logError("VideoRecorder") << "WriteSample failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        pd->failed = true;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// closePlatform - finalize the file and release MF resources
// ---------------------------------------------------------------------------
void VideoRecorder::closePlatform() {
    VideoRecorderPlatformData* pd = platform_;
    if (!pd) return;

    if (pd->writer) {
        if (!pd->failed) {
            HRESULT hr = pd->writer->Finalize();
            if (FAILED(hr)) {
                logError("VideoRecorder") << "Finalize failed (hr=0x"
                                          << std::hex << (unsigned)hr << ")";
            }
        }
        pd->writer->Release();
        pd->writer = nullptr;
    }

    if (pd->mfStarted) MFShutdown();

    delete pd;
    platform_ = nullptr;
}

} // namespace trussc

#endif // defined(_WIN32)
