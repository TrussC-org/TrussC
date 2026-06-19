// =============================================================================
// tcVideoRecorder_win.cpp - Windows Media Foundation (IMFSinkWriter) impl
// =============================================================================
//
// Mirrors the macOS AVFoundation / Linux GStreamer backends: it implements the
// three platform hooks declared in tc/video/tcVideoRecorder.h
//   - openPlatform()   : create an H.264 sink writer and begin writing
//   - appendPlatform() : encode one RGBA8 (top-down) frame
//   - closePlatform()  : finalize and flush the .mp4
//
// The encoder input type is RGB32 (BGRA in memory). The SinkWriter inserts the
// color-converter MFT automatically to reach the encoder's native NV12, so no
// manual conversion to YUV is needed here. Media Foundation treats RGB32 as
// bottom-up, while the recorder feeds RGBA top-down, so frames are flipped
// vertically (and swizzled R<->B) while copying into the sample buffer.
//
// Hardware -> software encoder fallback (mirrors the Linux backend's intent):
// the SinkWriter is opened first with MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
// which lets MF pick a hardware H.264 MFT (NVENC / Intel QuickSync / AMD VCE)
// when one is present and usable. Unlike GStreamer's deferred element creation,
// MF activates and negotiates the encoder MFT synchronously inside BeginWriting
// -- the realistic point of hardware-encoder failure (missing/limited driver,
// unsupported size). So if the hardware-enabled open fails at any step we tear
// it down and reopen with hardware transforms disabled (pure software MFT),
// which always works where the OS H.264 encoder is available. We then query the
// resolved encoder MFT to report whether hardware or software was actually used.
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
#include <mftransform.h>
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
    bool  mfStarted = false;   // this instance called MFStartup
    bool  usedHardware = false;
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

// Add the H.264 output + RGB32 input streams and begin writing. Returns the
// stream index. Failure here (notably for a hardware encoder that can't
// negotiate on this machine) is what drives the software fallback in open().
HRESULT configureWriter(IMFSinkWriter* writer, int w, int h,
                        UINT32 fpsNum, UINT32 fpsDen, int br,
                        DWORD& streamIndex) {
    // --- Output media type: H.264 ---
    IMFMediaType* outType = nullptr;
    HRESULT hr = MFCreateMediaType(&outType);
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
    if (FAILED(hr)) return hr;

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
    if (FAILED(hr)) return hr;

    // Activates and negotiates the encoder MFT synchronously: a hardware
    // encoder that exists but can't run here fails at this point.
    return writer->BeginWriting();
}

// Query the encoder MFT the SinkWriter resolved for this stream and report
// whether it is a hardware transform (presence of the hardware-URL attribute).
// Best-effort: if the service isn't exposed we just report "not confirmed HW".
bool streamUsesHardwareEncoder(IMFSinkWriter* writer, DWORD streamIndex) {
    bool isHw = false;
    IMFTransform* mft = nullptr;
    if (SUCCEEDED(writer->GetServiceForStream(streamIndex, GUID_NULL,
                                              __uuidof(IMFTransform),
                                              (void**)&mft)) && mft) {
        IMFAttributes* a = nullptr;
        if (SUCCEEDED(mft->GetAttributes(&a)) && a) {
            UINT32 cch = 0;
            if (SUCCEEDED(a->GetStringLength(MFT_ENUM_HARDWARE_URL_Attribute, &cch))
                && cch > 0) {
                isHw = true;
            }
            a->Release();
        }
        mft->Release();
    }
    return isHw;
}

// Create a sink writer for `wpath`, configure the H.264/RGB32 streams and begin
// writing, with hardware encoder MFTs enabled or disabled. On success the
// caller owns *outWriter; *outHardware reports the encoder actually resolved.
HRESULT openWriter(const std::wstring& wpath, int w, int h,
                   UINT32 fpsNum, UINT32 fpsDen, int br, bool hwEnabled,
                   IMFSinkWriter** outWriter, DWORD* outStream,
                   bool* outHardware) {
    *outWriter = nullptr;
    *outStream = 0;
    *outHardware = false;

    // SinkWriter creation flags: allow/forbid hardware MFTs, and don't throttle
    // to real time (we encode offline as fast as the encoder accepts frames).
    IMFAttributes* attrs = nullptr;
    HRESULT hr = MFCreateAttributes(&attrs, 2);
    if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
                                             hwEnabled ? 1u : 0u);
    if (SUCCEEDED(hr)) hr = attrs->SetUINT32(MF_SINK_WRITER_DISABLE_THROTTLING, 1u);

    IMFSinkWriter* writer = nullptr;
    if (SUCCEEDED(hr)) {
        hr = MFCreateSinkWriterFromURL(wpath.c_str(), nullptr, attrs, &writer);
    }
    if (attrs) attrs->Release();
    if (FAILED(hr) || !writer) {
        if (writer) writer->Release();
        return FAILED(hr) ? hr : E_FAIL;
    }

    DWORD streamIndex = 0;
    hr = configureWriter(writer, w, h, fpsNum, fpsDen, br, streamIndex);
    if (FAILED(hr)) {
        writer->Release();
        return hr;
    }

    *outWriter   = writer;
    *outStream   = streamIndex;
    *outHardware = streamUsesHardwareEncoder(writer, streamIndex);
    return S_OK;
}

} // namespace

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

    // ~0.1 bits per pixel per second — same default as the mac/Linux backends.
    int br = (bitrate > 0)
                 ? bitrate
                 : (int)((double)w * h * pd->fps * 0.1);

    // UTF-8 path -> wide.
    std::wstring wpath;
    {
        int n = MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, nullptr, 0);
        if (n > 0) {
            wpath.resize(n - 1);
            MultiByteToWideChar(CP_UTF8, 0, fullPath.c_str(), -1, &wpath[0], n);
        }
    }

    IMFSinkWriter* writer = nullptr;
    DWORD streamIndex = 0;
    bool  isHw = false;

    // Hardware first (auto-prefer the efficient path). Overwrite any existing
    // file each attempt (the writer otherwise locks/appends).
    DeleteFileW(wpath.c_str());
    hr = openWriter(wpath, w, h, fpsNum, fpsDen, br,
                    /*hwEnabled*/ true, &writer, &streamIndex, &isHw);

    // Software fallback: only the hardware path can fail here, so a failed open
    // means "hardware encoder unusable on this machine" -> retry pure software.
    if (FAILED(hr) || !writer) {
        logWarning("VideoRecorder")
            << "hardware H.264 encoder path unavailable (hr=0x"
            << std::hex << (unsigned)hr << "); falling back to software";
        DeleteFileW(wpath.c_str());
        hr = openWriter(wpath, w, h, fpsNum, fpsDen, br,
                        /*hwEnabled*/ false, &writer, &streamIndex, &isHw);
    }

    if (FAILED(hr) || !writer) {
        logError("VideoRecorder")
            << "failed to open H.264 sink writer (hr=0x"
            << std::hex << (unsigned)hr << ")";
        MFShutdown();
        delete pd;
        return false;
    }

    pd->writer       = writer;
    pd->streamIndex  = streamIndex;
    pd->usedHardware = isHw;
    platform_ = pd;

    logNotice("VideoRecorder")
        << "Media Foundation H.264 encoder: "
        << (isHw ? "hardware" : "software");
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
    // keeps the recorded video upright, matching the mac/Linux output.
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
