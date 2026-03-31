// =============================================================================
// Android VideoGrabber implementation (stub)
// =============================================================================
// TODO: Implement using Android Camera2 NDK API (ACameraManager)
// =============================================================================

#ifdef __ANDROID__

#include "TrussC.h"

#pragma message("STUB: tcVideoGrabber_android.cpp — needs Camera2 NDK implementation")

namespace trussc {

bool VideoGrabber::setupPlatform() {
    logWarning("VideoGrabber") << "Not yet implemented on Android";
    return false;
}

void VideoGrabber::closePlatform() {}
void VideoGrabber::updatePlatform() {}
void VideoGrabber::updateDelegatePixels() {}

std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    return {};
}

bool VideoGrabber::checkResizeNeeded() { return false; }
void VideoGrabber::getNewSize(int& width, int& height) { (void)width; (void)height; }
void VideoGrabber::clearResizeFlag() {}

bool VideoGrabber::checkCameraPermission() {
    // TODO: Check android.permission.CAMERA via JNI
    return false;
}

void VideoGrabber::requestCameraPermission() {
    // TODO: Request permission via JNI
    logWarning("VideoGrabber") << "Camera permission request not yet implemented on Android";
}

} // namespace trussc

#endif // __ANDROID__
