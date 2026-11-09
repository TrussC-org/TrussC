#pragma once

#include <TrussC.h>

#include <memory>
#include <string>

// =============================================================================
// tcVideoGrabberTestAccess.h - the test seam of VideoGrabber (friend struct)
// =============================================================================
// VideoGrabber exposes no new public methods for tests. The friend struct is
// defined once, here, because core tests are linked together into
// allCoreTests and a struct defined differently in two of them would break
// the one-definition rule.
// =============================================================================

namespace trussc::internal {

struct VideoGrabberTestAccess {
    static GrabberSharedState* sharedState(const VideoGrabber& grabber) {
        return grabber.state_.get();
    }
    static std::shared_ptr<GrabberSharedState> captureState(VideoGrabber& grabber) {
        return grabber.state_;
    }
    // A grabber that looks open: what a backend leaves after a successful setup().
    static void prepare(VideoGrabber& grabber) {
        grabber.initialized_ = true;
        grabber.state_->width = 1;
        grabber.state_->height = 1;
        grabber.state_->pixels = std::make_unique<unsigned char[]>(4);
    }
    // What a backend does when the OS reports the camera lost (any thread).
    static void reportLost(VideoGrabber& grabber, const std::string& reason) {
        grabber.state_->reportLost(reason);
    }
    static const std::string& reopenName(const VideoGrabber& grabber) { return grabber.reopenName_; }
    static void setReopenName(VideoGrabber& grabber, const std::string& name) { grabber.reopenName_ = name; }
    static bool usesUniqueId(const VideoGrabber& grabber) { return grabber.useDeviceUniqueId_; }
    static const std::string& uniqueId(const VideoGrabber& grabber) { return grabber.deviceUniqueId_; }
    static int lastWidth(const VideoGrabber& grabber) { return grabber.lastWidth_; }
    static int lastHeight(const VideoGrabber& grabber) { return grabber.lastHeight_; }
};

} // namespace trussc::internal
