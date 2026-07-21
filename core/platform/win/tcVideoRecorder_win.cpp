// =============================================================================
// tcVideoRecorder_win.cpp - Windows Media Foundation (IMFSinkWriter) impl
// =============================================================================
//
// Mirrors the macOS AVFoundation / Linux GStreamer backends: it implements the
// three platform hooks declared in tc/video/tcVideoRecorder.h
//   - openPlatform()   : create an H.264/HEVC sink writer and begin writing
//   - appendPlatform() : encode one RGBA8 (top-down) frame
//   - closePlatform()  : finalize and flush the file
//
// Codecs: H.264 and HEVC (both mux into .mp4). Bitrate comes from
// VideoRecordSettings. ProRes is a macOS-only mastering codec and is rejected.
// keyframeInterval is applied via MF_MT_MAX_KEYFRAME_SPACING but is best-effort:
// the Microsoft/NVENC encoders in this offline SinkWriter configuration treat it
// as a hint (a short clip may stay a single GOP). This matches the Linux
// backend's "best-effort where the encoder honors it" stance.
//
// The encoder input type is RGB32 (BGRA in memory). The SinkWriter inserts the
// color-converter MFT automatically to reach the encoder's native NV12, so no
// manual conversion to YUV is needed here. Media Foundation treats RGB32 as
// bottom-up, while the recorder feeds RGBA top-down, so frames are flipped
// vertically (and swizzled R<->B) while copying into the sample buffer.
//
// Hardware -> software encoder fallback (mirrors the Linux backend's intent):
// the SinkWriter is opened first with MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS,
// which lets MF pick a hardware encoder MFT (NVENC / Intel QuickSync / AMD VCE)
// when one is present and usable. Unlike GStreamer's deferred element creation,
// MF activates and negotiates the encoder MFT synchronously inside BeginWriting
// -- the realistic point of hardware-encoder failure (missing/limited driver,
// unsupported size). So if the hardware-enabled open fails at any step we tear
// it down and reopen with hardware transforms disabled (pure software MFT),
// which always works where the OS encoder is available. We then query the
// resolved encoder MFT to report whether hardware or software was actually used.
// (HEVC has no guaranteed software encoder on Windows, so HEVC can still fail on
// a machine without a hardware H.265 encoder -- this is reported, not silent.)
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
#include <cstring>
#include <string>
#include <vector>

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfreadwrite.lib")
#pragma comment(lib, "mfuuid.lib")

namespace trussc {

// ---------------------------------------------------------------------------
// Platform state (IMFSinkWriter holds the encoder + mp4 file sink)
// ---------------------------------------------------------------------------
namespace internal {
struct VideoWriterPlatformData {
    IMFSinkWriter* writer = nullptr;
    DWORD streamIndex = 0;
    int   width  = 0;
    int   height = 0;
    double fps   = 30.0;
    bool  mfStarted = false;   // this instance called MFStartup
    bool  usedHardware = false;
    bool  failed = false;

    // Audio track (AAC). hasAudio stays false when settings.audio is off, the
    // engine format is outside the MF AAC encoder's input constraints, or the
    // audio stream could not be negotiated (video-only degradation).
    bool  hasAudio = false;
    DWORD audioStream = 0;
    int   audioRate = 0;
    int   audioChannels = 0;
    std::vector<short> s16;    // float -> PCM16 conversion scratch
};
}  // namespace internal
using internal::VideoWriterPlatformData;

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

// Audio track parameters, validated by openPlatform(). rate/channels must be
// within the MF AAC encoder's input constraints (PCM16, 44.1/48 kHz, 1-2 ch);
// bytesPerSec one of the encoder's discrete CBR steps (12000/16000/20000/24000).
struct AudioParams {
    bool wanted = false;
    int  rate = 0;
    int  channels = 0;
    UINT32 bytesPerSec = 24000;
};

// Add the AAC output + PCM16 input audio streams to the sink writer. Called
// before BeginWriting(); a failure fails the whole open (streams cannot be
// removed from a sink writer), and the caller retries without audio.
HRESULT configureAudioStream(IMFSinkWriter* writer, const AudioParams& ap,
                             DWORD& audioStreamIndex) {
    // --- Output media type: AAC-LC ---
    IMFMediaType* outType = nullptr;
    HRESULT hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_AAC);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                               (UINT32)ap.rate);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                                               (UINT32)ap.channels);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                               ap.bytesPerSec);
    if (SUCCEEDED(hr)) hr = writer->AddStream(outType, &audioStreamIndex);
    if (outType) outType->Release();
    if (FAILED(hr)) return hr;

    // --- Input media type: interleaved PCM16 ---
    const UINT32 blockAlign = (UINT32)ap.channels * 2;
    IMFMediaType* inType = nullptr;
    hr = MFCreateMediaType(&inType);
    if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Audio);
    if (SUCCEEDED(hr)) hr = inType->SetGUID(MF_MT_SUBTYPE, MFAudioFormat_PCM);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_SAMPLES_PER_SECOND,
                                              (UINT32)ap.rate);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_NUM_CHANNELS,
                                              (UINT32)ap.channels);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BITS_PER_SAMPLE, 16);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_BLOCK_ALIGNMENT, blockAlign);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_AUDIO_AVG_BYTES_PER_SECOND,
                                              (UINT32)ap.rate * blockAlign);
    if (SUCCEEDED(hr)) hr = inType->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, 1);
    if (SUCCEEDED(hr)) hr = writer->SetInputMediaType(audioStreamIndex, inType,
                                                      nullptr);
    if (inType) inType->Release();
    return hr;
}

// Add the compressed output + RGB32 input streams and begin writing. Returns
// the stream index. Failure here (notably for a hardware encoder that can't
// negotiate on this machine) is what drives the software fallback in open().
HRESULT configureWriter(IMFSinkWriter* writer, int w, int h,
                        UINT32 fpsNum, UINT32 fpsDen, int br,
                        VideoCodec codec, int keyframeInterval,
                        DWORD& streamIndex,
                        const AudioParams& audio, DWORD& audioStreamIndex) {
    // --- Output media type: H.264 or HEVC (both mux into .mp4) ---
    const GUID subtype = (codec == VideoCodec::HEVC) ? MFVideoFormat_HEVC
                                                     : MFVideoFormat_H264;
    IMFMediaType* outType = nullptr;
    HRESULT hr = MFCreateMediaType(&outType);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    if (SUCCEEDED(hr)) hr = outType->SetGUID(MF_MT_SUBTYPE, subtype);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_AVG_BITRATE, (UINT32)br);
    if (SUCCEEDED(hr)) hr = outType->SetUINT32(MF_MT_INTERLACE_MODE,
                                               MFVideoInterlace_Progressive);
    if (SUCCEEDED(hr)) hr = MFSetAttributeSize(outType, MF_MT_FRAME_SIZE, w, h);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_FRAME_RATE,
                                                fpsNum, fpsDen);
    if (SUCCEEDED(hr)) hr = MFSetAttributeRatio(outType, MF_MT_PIXEL_ASPECT_RATIO,
                                                1, 1);
    // Keyframe cadence (GOP length). Best-effort: honored by encoders that
    // respect it; 0 leaves the encoder's default.
    if (SUCCEEDED(hr) && keyframeInterval > 0) {
        hr = outType->SetUINT32(MF_MT_MAX_KEYFRAME_SPACING,
                                (UINT32)keyframeInterval);
    }
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

    // Audio track, if requested (must be added before BeginWriting).
    if (audio.wanted) {
        hr = configureAudioStream(writer, audio, audioStreamIndex);
        if (FAILED(hr)) return hr;
    }

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

// Create a sink writer for `wpath`, configure the compressed/RGB32 streams and
// begin writing, with hardware encoder MFTs enabled or disabled. On success the
// caller owns *outWriter; *outHardware reports the encoder actually resolved.
HRESULT openWriter(const std::wstring& wpath, int w, int h,
                   UINT32 fpsNum, UINT32 fpsDen, int br,
                   VideoCodec codec, int keyframeInterval, bool hwEnabled,
                   const AudioParams& audio,
                   IMFSinkWriter** outWriter, DWORD* outStream,
                   DWORD* outAudioStream, bool* outHardware) {
    *outWriter = nullptr;
    *outStream = 0;
    *outAudioStream = 0;
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
    DWORD audioStreamIndex = 0;
    hr = configureWriter(writer, w, h, fpsNum, fpsDen, br,
                         codec, keyframeInterval, streamIndex,
                         audio, audioStreamIndex);
    if (FAILED(hr)) {
        writer->Release();
        return hr;
    }

    *outWriter      = writer;
    *outStream      = streamIndex;
    *outAudioStream = audioStreamIndex;
    *outHardware    = streamUsesHardwareEncoder(writer, streamIndex);
    return S_OK;
}

} // namespace

// ---------------------------------------------------------------------------
// openPlatform - create the encoder sink writer and begin a writing session
// ---------------------------------------------------------------------------
bool VideoWriter::openPlatform(const std::string& fullPath, int w, int h,
                               float fps, const VideoRecordSettings& settings) {
    // Audio track: the MF AAC encoder only accepts PCM16 at 44.1/48 kHz, 1-2
    // channels. Outside those constraints we warn and record video-only
    // (mirrors the mac backend's degradation behavior).
    AudioParams audio;
    if (settings.audio) {
        const int aRate = settings.audioSampleRate;
        const int aCh   = settings.audioChannels;
        if ((aRate == 44100 || aRate == 48000) && aCh >= 1 && aCh <= 2) {
            audio.wanted   = true;
            audio.rate     = aRate;
            audio.channels = aCh;
            // The encoder supports discrete CBR steps only:
            // 12000/16000/20000/24000 bytes/sec (96/128/160/192 kbit).
            const UINT32 steps[] = {12000, 16000, 20000, 24000};
            const int want = settings.audioBitrate / 8;
            audio.bytesPerSec = steps[0];
            for (UINT32 s : steps) {
                if ((int)s <= want) audio.bytesPerSec = s;
            }
        } else {
            logWarning("VideoWriter")
                << "audio format " << aRate << " Hz / " << aCh
                << " ch not supported by the AAC encoder "
                   "(needs 44100/48000 Hz, 1-2 ch) - recording video only";
        }
    }
    // This backend encodes H.264 and HEVC (both into .mp4). ProRes is a macOS-
    // only mastering codec; reject it clearly rather than silently substituting.
    if (settings.codec != VideoCodec::H264 && settings.codec != VideoCodec::HEVC) {
        logError("VideoWriter") << "codec " << videoCodecName(settings.codec)
                                << " not supported on Windows (use H.264 or HEVC)";
        return false;
    }

    // Media Foundation startup (ref-counted internally; matched in close()).
    HRESULT hr = MFStartup(MF_VERSION, MFSTARTUP_FULL);
    if (FAILED(hr)) {
        logError("VideoWriter") << "MFStartup failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        return false;
    }

    auto* pd = new VideoWriterPlatformData();
    pd->mfStarted = true;
    pd->width  = w;
    pd->height = h;
    pd->fps    = (fps > 0.0f) ? fps : 30.0f;

    UINT32 fpsNum = 30, fpsDen = 1;
    fpsToRatio(pd->fps, fpsNum, fpsDen);

    // ~0.1 bits per pixel per second — same default as the mac/Linux backends.
    int br = (settings.bitrate > 0)
                 ? settings.bitrate
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
    DWORD audioStreamIndex = 0;
    bool  isHw = false;

    const VideoCodec codec = settings.codec;
    const int kf = settings.keyframeInterval;

    // Hardware first (auto-prefer the efficient path). Overwrite any existing
    // file each attempt (the writer otherwise locks/appends).
    DeleteFileW(wpath.c_str());
    hr = openWriter(wpath, w, h, fpsNum, fpsDen, br, codec, kf,
                    /*hwEnabled*/ true, audio,
                    &writer, &streamIndex, &audioStreamIndex, &isHw);

    // Software fallback: a failed open normally means "hardware encoder
    // unusable on this machine" -> retry pure software.
    if (FAILED(hr) || !writer) {
        logWarning("VideoWriter")
            << "hardware " << videoCodecName(codec)
            << " encoder path unavailable (hr=0x"
            << std::hex << (unsigned)hr << "); falling back to software";
        DeleteFileW(wpath.c_str());
        hr = openWriter(wpath, w, h, fpsNum, fpsDen, br, codec, kf,
                        /*hwEnabled*/ false, audio,
                        &writer, &streamIndex, &audioStreamIndex, &isHw);
    }

    // Last resort with audio requested: the AAC stream itself may be what
    // failed (sink writer streams can't be removed, so a failed audio setup
    // fails the whole open). Degrade to video-only rather than not recording.
    if ((FAILED(hr) || !writer) && audio.wanted) {
        logWarning("VideoWriter")
            << "could not set up the AAC audio track (hr=0x"
            << std::hex << (unsigned)hr << ") - recording video only";
        audio.wanted = false;
        DeleteFileW(wpath.c_str());
        hr = openWriter(wpath, w, h, fpsNum, fpsDen, br, codec, kf,
                        /*hwEnabled*/ true, audio,
                        &writer, &streamIndex, &audioStreamIndex, &isHw);
        if (FAILED(hr) || !writer) {
            DeleteFileW(wpath.c_str());
            hr = openWriter(wpath, w, h, fpsNum, fpsDen, br, codec, kf,
                            /*hwEnabled*/ false, audio,
                            &writer, &streamIndex, &audioStreamIndex, &isHw);
        }
    }

    if (FAILED(hr) || !writer) {
        logError("VideoWriter")
            << "failed to open " << videoCodecName(codec)
            << " sink writer (hr=0x" << std::hex << (unsigned)hr << ")";
        MFShutdown();
        delete pd;
        return false;
    }

    pd->writer       = writer;
    pd->streamIndex  = streamIndex;
    pd->usedHardware = isHw;
    if (audio.wanted) {
        pd->hasAudio      = true;
        pd->audioStream   = audioStreamIndex;
        pd->audioRate     = audio.rate;
        pd->audioChannels = audio.channels;
    }
    platform_ = pd;

    logNotice("VideoWriter")
        << "Media Foundation " << videoCodecName(codec) << " encoder: "
        << (isHw ? "hardware" : "software")
        << (pd->hasAudio ? " + AAC audio" : "");
    return true;
}

// ---------------------------------------------------------------------------
// appendAudioPlatform - append interleaved float samples to the AAC track
// ---------------------------------------------------------------------------
// Called on the recorder's writer thread, interleaved with video WriteSample
// calls on the same thread; the sink writer handles the muxing (throttling is
// disabled at creation, so neither stream ever blocks waiting for the other).
bool VideoWriter::appendAudioPlatform(const float* interleaved, int frames,
                                      double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->writer || pd->failed || !pd->hasAudio) return false;
    if (!interleaved || frames <= 0) return false;

    // float [-1,1] -> interleaved PCM16, clamped.
    const int samples = frames * pd->audioChannels;
    pd->s16.resize((size_t)samples);
    for (int i = 0; i < samples; ++i) {
        float v = interleaved[i];
        if (v > 1.0f) v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        pd->s16[(size_t)i] = (short)(v * 32767.0f);
    }

    const DWORD byteCount = (DWORD)samples * sizeof(short);
    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(byteCount, &buffer);
    if (FAILED(hr) || !buffer) {
        logError("VideoWriter") << "MFCreateMemoryBuffer (audio) failed";
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
    memcpy(dst, pd->s16.data(), byteCount);
    buffer->Unlock();
    buffer->SetCurrentLength(byteCount);

    IMFSample* sample = nullptr;
    hr = MFCreateSample(&sample);
    if (SUCCEEDED(hr)) hr = sample->AddBuffer(buffer);
    if (SUCCEEDED(hr)) {
        // MF time units are 100-nanosecond ticks (1e7 per second).
        sample->SetSampleTime((LONGLONG)(timeSec * 1.0e7 + 0.5));
        sample->SetSampleDuration(
            (LONGLONG)((double)frames / pd->audioRate * 1.0e7 + 0.5));
        hr = pd->writer->WriteSample(pd->audioStream, sample);
    }

    if (sample) sample->Release();
    buffer->Release();

    if (FAILED(hr)) {
        logError("VideoWriter") << "WriteSample (audio) failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        pd->failed = true;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// appendPlatform - encode one RGBA8 (top-down) frame
// ---------------------------------------------------------------------------

bool VideoWriter::appendPlatform(const unsigned char* rgba, double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->writer || pd->failed) return false;

    const int w = pd->width;
    const int h = pd->height;
    const DWORD frameBytes = (DWORD)w * h * 4;

    IMFMediaBuffer* buffer = nullptr;
    HRESULT hr = MFCreateMemoryBuffer(frameBytes, &buffer);
    if (FAILED(hr) || !buffer) {
        logError("VideoWriter") << "MFCreateMemoryBuffer failed";
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
        logError("VideoWriter") << "WriteSample failed (hr=0x"
                                  << std::hex << (unsigned)hr << ")";
        pd->failed = true;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// closePlatform - finalize the file and release MF resources
// ---------------------------------------------------------------------------
void VideoWriter::closePlatform() {
    VideoWriterPlatformData* pd = platform_;
    if (!pd) return;

    if (pd->writer) {
        if (!pd->failed) {
            HRESULT hr = pd->writer->Finalize();
            if (FAILED(hr)) {
                logError("VideoWriter") << "Finalize failed (hr=0x"
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
