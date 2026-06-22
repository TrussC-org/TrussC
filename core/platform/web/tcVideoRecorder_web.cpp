// =============================================================================
// tcVideoRecorder_web.cpp - Web/Emscripten stub
// =============================================================================
//
// Placeholder so VideoWriter (and ScreenRecorder) links under Emscripten.
// Recording is not implemented: openPlatform() returns false.
//
// Note for a future backend: the VideoWriter model (feed CPU RGBA frames) does
// not map onto the web build, because the default WebGPU backend cannot read
// back FBO/swapchain pixels synchronously (see tcFbo_web.cpp). A real web
// recorder would instead live entirely in JS, capturing the <canvas> directly
// via canvas.captureStream() + MediaRecorder (webm) or WebCodecs (mp4), driven
// over EM_JS — a different mechanism from the native backends, and live-only
// (no deterministic offline export). Tracked as a follow-up.
// =============================================================================

#if defined(__EMSCRIPTEN__)

#include "TrussC.h"

namespace trussc {

struct VideoWriterPlatformData {};

bool VideoWriter::openPlatform(const std::string&, int, int, float,
                               const VideoRecordSettings&) {
    logWarning("VideoWriter") << "video recording is not implemented on web yet";
    return false;
}

bool VideoWriter::appendPlatform(const unsigned char*, double) { return false; }

void VideoWriter::closePlatform() {}

} // namespace trussc

#endif // __EMSCRIPTEN__
