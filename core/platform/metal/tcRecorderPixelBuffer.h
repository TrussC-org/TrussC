#pragma once

#include <CoreVideo/CoreVideo.h>
#include "tc/video/tcRecorderFrame.h"

namespace trussc::internal {

inline RecorderFrameLayout recorderPixelBufferLayout(CVPixelBufferRef buffer) {
    // Bound (abi): the native buffer must expose the packed BGRA8 layout used by the take. Overflow drops the recorder frame and is counted.
    if (CVPixelBufferIsPlanar(buffer) ||
        CVPixelBufferGetPixelFormatType(buffer) != kCVPixelFormatType_32BGRA)
        return {};
    return {CVPixelBufferGetWidth(buffer), CVPixelBufferGetHeight(buffer),
            CVPixelBufferGetBytesPerRow(buffer), CVPixelBufferGetDataSize(buffer),
            RecorderPixelFormat::BGRA8};
}

} // namespace trussc::internal
