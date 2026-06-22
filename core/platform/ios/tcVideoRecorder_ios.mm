// =============================================================================
// tcVideoRecorder_ios.mm - iOS stub
// =============================================================================
//
// Placeholder so VideoWriter (and ScreenRecorder) links on iOS. Recording is
// not implemented yet: openPlatform() returns false.
//
// A real backend is straightforward: AVAssetWriter works on iOS just like the
// macOS path (tcVideoRecorder_mac.mm), so this can largely reuse that code once
// an iOS device is available to verify on. Tracked as a follow-up.
// =============================================================================

#if defined(__APPLE__)
#import <TargetConditionals.h>
#if TARGET_OS_IOS

#include "TrussC.h"

namespace trussc {

struct VideoWriterPlatformData {};

bool VideoWriter::openPlatform(const std::string&, int, int, float,
                               const VideoRecordSettings&) {
    logWarning("VideoWriter") << "video recording is not implemented on iOS yet";
    return false;
}

bool VideoWriter::appendPlatform(const unsigned char*, double) { return false; }

void VideoWriter::closePlatform() {}

} // namespace trussc

#endif // TARGET_OS_IOS
#endif // __APPLE__
