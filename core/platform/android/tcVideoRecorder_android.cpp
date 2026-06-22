// =============================================================================
// tcVideoRecorder_android.cpp - Android NDK MediaCodec + MediaMuxer impl
// =============================================================================
// Mirrors the macOS AVAssetWriter / Windows Media Foundation / Linux GStreamer
// backends: implements the three VideoWriter platform hooks from
// tc/video/tcVideoRecorder.h
//   - openPlatform()   : create an AMediaCodec encoder (H.264 / HEVC) + muxer
//   - appendPlatform() : convert one RGBA8 (top-down) frame to NV12 and encode
//   - closePlatform()  : flush (EOS), finalize the .mp4, release everything
//
// Uses the pure-C NDK media stack (libmediandk) so the whole backend lives in
// C++ with no JNI. Hardware encoders want YUV, so frames are converted RGBA ->
// NV12 (COLOR_FormatYUV420SemiPlanar) on the CPU before being queued. ProRes is
// AVFoundation-only and rejected here (no silent fallback, matching win/linux).
// =============================================================================

#if defined(__ANDROID__)

#include "TrussC.h"

#include <media/NdkMediaCodec.h>
#include <media/NdkMediaMuxer.h>
#include <media/NdkMediaFormat.h>

#include <fcntl.h>
#include <unistd.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace trussc {

// Android OMX color format constant (not exposed by the NDK headers).
static constexpr int32_t kColorFormatYUV420SemiPlanar = 21;  // NV12-style

// AMediaCodec dequeue sentinels / flags (stable values; NDK does not always
// expose them as named constants across versions).
static constexpr ssize_t  kInfoTryAgainLater        = -1;
static constexpr ssize_t  kInfoOutputFormatChanged  = -2;
static constexpr ssize_t  kInfoOutputBuffersChanged = -3;
static constexpr uint32_t kFlagCodecConfig          = 2;
static constexpr uint32_t kFlagEndOfStream          = 4;

struct VideoWriterPlatformData {
    AMediaCodec* codec = nullptr;
    AMediaMuxer* muxer = nullptr;
    int     fd = -1;
    ssize_t track = -1;
    bool    muxerStarted = false;
    int     width = 0;
    int     height = 0;
    int     yStride = 0;      // encoder input row stride (>= width, e.g. 1088)
    int     sliceHeight = 0;  // encoder input plane height (>= height)
    bool    layoutResolved = false;  // stride/slice-height learned from capacity
    bool    failed = false;
    std::vector<uint8_t> nv12;  // reusable conversion scratch (stride-padded)
};

// ---------------------------------------------------------------------------
// RGBA8 (top-down) -> NV12 (Y plane, then interleaved U,V). BT.601 limited.
// Honors the encoder's input stride / slice-height (rows are padded), so the
// content is not sheared on devices whose codec aligns the width (e.g. Exynos
// pads 1080 -> 1088).
// ---------------------------------------------------------------------------
static void rgbaToNV12(const unsigned char* rgba, int w, int h,
                       int yStride, int sliceHeight, uint8_t* out) {
    uint8_t* yPlane = out;
    uint8_t* uvPlane = out + (size_t)yStride * sliceHeight;
    for (int j = 0; j < h; ++j) {
        for (int i = 0; i < w; ++i) {
            const unsigned char* p = rgba + ((size_t)j * w + i) * 4;
            int r = p[0], g = p[1], b = p[2];
            int y = (66 * r + 129 * g + 25 * b + 128) >> 8;
            yPlane[(size_t)j * yStride + i] = (uint8_t)(y + 16);
        }
    }
    // Chroma is subsampled 2x2: average each block for a cleaner result.
    for (int j = 0; j < h; j += 2) {
        for (int i = 0; i < w; i += 2) {
            int sr = 0, sg = 0, sb = 0;
            for (int dj = 0; dj < 2; ++dj) {
                for (int di = 0; di < 2; ++di) {
                    const unsigned char* p =
                        rgba + (((size_t)(j + dj) * w) + (i + di)) * 4;
                    sr += p[0]; sg += p[1]; sb += p[2];
                }
            }
            int r = sr >> 2, g = sg >> 2, b = sb >> 2;
            int u = (-38 * r - 74 * g + 112 * b + 128) >> 8;
            int v = (112 * r - 94 * g - 18 * b + 128) >> 8;
            size_t idx = (size_t)(j / 2) * yStride + i;  // 2 bytes per sample
            uvPlane[idx + 0] = (uint8_t)(u + 128);  // U
            uvPlane[idx + 1] = (uint8_t)(v + 128);  // V
        }
    }
}

// ---------------------------------------------------------------------------
// Drain available encoded output into the muxer. On the first FORMAT_CHANGED we
// learn the real output format (with CSD) and start the muxer.
// ---------------------------------------------------------------------------
static bool drainOutput(VideoWriterPlatformData* pd, bool endOfStream) {
    for (;;) {
        AMediaCodecBufferInfo info;
        ssize_t idx = AMediaCodec_dequeueOutputBuffer(
            pd->codec, &info, endOfStream ? 10000 : 0);

        if (idx == kInfoTryAgainLater) {
            if (!endOfStream) return true;   // nothing pending right now
            continue;                        // EOS: keep waiting for the tail
        }
        if (idx == kInfoOutputFormatChanged) {
            AMediaFormat* fmt = AMediaCodec_getOutputFormat(pd->codec);
            pd->track = AMediaMuxer_addTrack(pd->muxer, fmt);
            AMediaFormat_delete(fmt);
            if (pd->track < 0) {
                logError("VideoWriter") << "muxer addTrack failed";
                pd->failed = true;
                return false;
            }
            if (AMediaMuxer_start(pd->muxer) != AMEDIA_OK) {
                logError("VideoWriter") << "muxer start failed";
                pd->failed = true;
                return false;
            }
            pd->muxerStarted = true;
            continue;
        }
        if (idx == kInfoOutputBuffersChanged) continue;  // deprecated, ignore
        if (idx < 0) continue;

        size_t outSize = 0;
        uint8_t* buf = AMediaCodec_getOutputBuffer(pd->codec, idx, &outSize);

        // Codec-config (SPS/PPS) is already carried in the output format's CSD.
        if (info.flags & kFlagCodecConfig) info.size = 0;

        if (info.size > 0 && pd->muxerStarted && buf) {
            AMediaMuxer_writeSampleData(pd->muxer, pd->track, buf, &info);
        }
        AMediaCodec_releaseOutputBuffer(pd->codec, idx, false);

        if (info.flags & kFlagEndOfStream) return true;
    }
}

// ---------------------------------------------------------------------------
// openPlatform
// ---------------------------------------------------------------------------
bool VideoWriter::openPlatform(const std::string& fullPath, int w, int h,
                               float fps, const VideoRecordSettings& settings) {
    const char* mime = nullptr;
    switch (settings.codec) {
        case VideoCodec::H264: mime = "video/avc";  break;
        case VideoCodec::HEVC: mime = "video/hevc"; break;
        case VideoCodec::ProRes422:
        case VideoCodec::ProRes4444:
            logError("VideoWriter")
                << "ProRes is not available on Android (use H264 or HEVC)";
            return false;
    }

    AMediaCodec* codec = AMediaCodec_createEncoderByType(mime);
    if (!codec) {
        logError("VideoWriter") << "no encoder for " << mime;
        return false;
    }

    double rate = (fps > 0) ? fps : 30.0;
    int bitrate = (settings.bitrate > 0)
                      ? settings.bitrate
                      : (int)((double)w * h * rate * 0.1);
    int iFrameSec = (settings.keyframeInterval > 0)
                        ? (int)((settings.keyframeInterval / rate) + 0.5)
                        : 1;
    if (iFrameSec < 1) iFrameSec = 1;

    AMediaFormat* fmt = AMediaFormat_new();
    AMediaFormat_setString(fmt, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_WIDTH, w);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_HEIGHT, h);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_COLOR_FORMAT,
                          kColorFormatYUV420SemiPlanar);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_BIT_RATE, bitrate);
    AMediaFormat_setFloat(fmt, AMEDIAFORMAT_KEY_FRAME_RATE, (float)rate);
    AMediaFormat_setInt32(fmt, AMEDIAFORMAT_KEY_I_FRAME_INTERVAL, iFrameSec);

    media_status_t cfg = AMediaCodec_configure(
        codec, fmt, nullptr, nullptr, AMEDIACODEC_CONFIGURE_FLAG_ENCODE);
    AMediaFormat_delete(fmt);
    if (cfg != AMEDIA_OK) {
        logError("VideoWriter") << "encoder configure failed (" << mime << ")";
        AMediaCodec_delete(codec);
        return false;
    }
    if (AMediaCodec_start(codec) != AMEDIA_OK) {
        logError("VideoWriter") << "encoder start failed";
        AMediaCodec_delete(codec);
        return false;
    }

    // The encoder reads the ByteBuffer with its own row stride / plane height
    // (e.g. Tensor/Exynos aligns 1080 -> 1088); writing tightly packed shears
    // the image. The exact layout is resolved from the first input buffer's
    // capacity in appendPlatform (getInputFormat would report it but is API 28+;
    // the capacity route stays on the minSdk-26 baseline). Start with a 16-byte
    // aligned guess.
    int32_t yStride = (w + 15) & ~15;
    int32_t sliceHeight = h;

    int fd = ::open(fullPath.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        logError("VideoWriter") << "cannot open output file " << fullPath;
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        return false;
    }
    AMediaMuxer* muxer = AMediaMuxer_new(fd, AMEDIAMUXER_OUTPUT_FORMAT_MPEG_4);
    if (!muxer) {
        logError("VideoWriter") << "muxer create failed";
        ::close(fd);
        AMediaCodec_stop(codec);
        AMediaCodec_delete(codec);
        return false;
    }

    auto* pd = new VideoWriterPlatformData();
    pd->codec = codec;
    pd->muxer = muxer;
    pd->fd = fd;
    pd->width = w;
    pd->height = h;
    pd->yStride = yStride;
    pd->sliceHeight = sliceHeight;
    pd->nv12.resize((size_t)yStride * sliceHeight * 3 / 2);
    platform_ = pd;
    return true;
}

// ---------------------------------------------------------------------------
// appendPlatform
// ---------------------------------------------------------------------------
bool VideoWriter::appendPlatform(const unsigned char* rgba, double timeSec) {
    VideoWriterPlatformData* pd = platform_;
    if (!pd || !pd->codec || pd->failed) return false;

    // Wait (briefly) for an input buffer — offline writers can outrun the codec.
    ssize_t inIdx = -1;
    for (int spins = 0; spins < 1000; ++spins) {
        inIdx = AMediaCodec_dequeueInputBuffer(pd->codec, 10000);
        if (inIdx >= 0) break;
        if (!drainOutput(pd, false)) return false;   // make room
    }
    if (inIdx < 0) {
        logError("VideoWriter") << "no encoder input buffer (timeout)";
        pd->failed = true;
        return false;
    }

    size_t cap = 0;
    uint8_t* in = AMediaCodec_getInputBuffer(pd->codec, inIdx, &cap);
    if (!in) {
        logError("VideoWriter") << "null input buffer";
        pd->failed = true;
        return false;
    }

    // Resolve the encoder's real input row stride once, from the buffer
    // capacity: cap ~= yStride * sliceHeight * 3/2. Assume the height is not
    // padded (sliceHeight == height, true on Tensor/Exynos) and round to the
    // nearest stride. The capacity can be a few bytes short of the full padded
    // plane, so we never require an exact size — we queue min(need, cap).
    if (!pd->layoutResolved) {
        int est = (int)std::llround((double)cap / (1.5 * (double)pd->height));
        int ys = (est >= pd->width) ? est : ((pd->width + 15) & ~15);
        ys = (ys + 1) & ~1;  // keep even
        pd->yStride = ys;
        pd->sliceHeight = pd->height;
        pd->nv12.resize((size_t)ys * pd->height * 3 / 2);
        pd->layoutResolved = true;
        logNotice("VideoWriter")
            << "encoder input stride=" << ys << " slice-height=" << pd->height
            << " (cap=" << cap << ")";
    }

    rgbaToNV12(rgba, pd->width, pd->height, pd->yStride, pd->sliceHeight,
               pd->nv12.data());
    size_t qsize = pd->nv12.size() < cap ? pd->nv12.size() : cap;
    memcpy(in, pd->nv12.data(), qsize);

    int64_t ptsUs = (int64_t)(timeSec * 1e6);
    if (AMediaCodec_queueInputBuffer(pd->codec, inIdx, 0, qsize, ptsUs, 0)
            != AMEDIA_OK) {
        logError("VideoWriter") << "queueInputBuffer failed";
        pd->failed = true;
        return false;
    }

    return drainOutput(pd, false);
}

// ---------------------------------------------------------------------------
// closePlatform - signal EOS, flush the tail, finalize and release.
// ---------------------------------------------------------------------------
void VideoWriter::closePlatform() {
    VideoWriterPlatformData* pd = platform_;
    if (!pd) return;

    if (pd->codec && !pd->failed) {
        // Queue an end-of-stream marker on an empty input buffer.
        ssize_t inIdx = -1;
        for (int spins = 0; spins < 1000 && inIdx < 0; ++spins) {
            inIdx = AMediaCodec_dequeueInputBuffer(pd->codec, 10000);
            if (inIdx < 0) drainOutput(pd, false);
        }
        if (inIdx >= 0) {
            AMediaCodec_queueInputBuffer(pd->codec, inIdx, 0, 0, 0,
                                         kFlagEndOfStream);
            drainOutput(pd, true);
        }
    }

    if (pd->muxer) {
        if (pd->muxerStarted) AMediaMuxer_stop(pd->muxer);
        AMediaMuxer_delete(pd->muxer);
    }
    if (pd->codec) {
        AMediaCodec_stop(pd->codec);
        AMediaCodec_delete(pd->codec);
    }
    if (pd->fd >= 0) ::close(pd->fd);

    delete pd;
    platform_ = nullptr;
}

} // namespace trussc

#endif // __ANDROID__
