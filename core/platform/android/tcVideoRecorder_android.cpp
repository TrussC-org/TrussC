// =============================================================================
// tcVideoRecorder_android.cpp - Android stub
// =============================================================================
//
// Placeholder so VideoWriter (and ScreenRecorder, which uses it) links on
// Android. Recording is not implemented yet: openPlatform() reports the gap and
// returns false, so callers get a clean "not supported" instead of a broken
// file. A real backend would use the NDK media stack (AMediaCodec + AMediaMuxer,
// API 21+, link libmediandk) and convert the RGBA8 top-down frames to YUV420 —
// glReadPixels readback already provides the frames. Tracked as a follow-up.
// =============================================================================

#if defined(__ANDROID__)

#include "TrussC.h"

namespace trussc {

struct VideoWriterPlatformData {};

bool VideoWriter::openPlatform(const std::string&, int, int, float,
                               const VideoRecordSettings&) {
    logWarning("VideoWriter") << "video recording is not implemented on Android yet";
    return false;
}

bool VideoWriter::appendPlatform(const unsigned char*, double) { return false; }

void VideoWriter::closePlatform() {}

} // namespace trussc

#endif // __ANDROID__
