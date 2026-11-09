#pragma once

#include <cmath>
#include <string>
#include <vector>

namespace trussc::internal {

struct GrabberFrameRateRange {
    double minFrameRate;
    double maxFrameRate;
};

struct GrabberFormat {
    int width;
    int height;
    std::vector<GrabberFrameRateRange> frameRates;
};

// macOS and Windows use the closest size first. At equal size distance,
// prefer a mode supporting the requested fps; otherwise keep enumeration order.
// Rounded range endpoints accommodate nominal rates such as 29.97 and 60.00024.
inline int selectGrabberFormat(const std::vector<GrabberFormat>& formats,
                               int width, int height, int desiredFrameRate) {
    int bestIndex = -1;
    double bestDistance = 0;
    bool bestFpsOk = false;
    for (std::size_t i = 0; i < formats.size(); ++i) {
        const auto& format = formats[i];
        bool fpsOk = desiredFrameRate <= 0;
        for (const auto& range : format.frameRates) {
            if (std::floor(range.minFrameRate) <= desiredFrameRate &&
                std::ceil(range.maxFrameRate) >= desiredFrameRate) fpsOk = true;
        }
        const double dx = double(format.width) - width;
        const double dy = double(format.height) - height;
        const double distance = dx * dx + dy * dy;
        if (bestIndex < 0 || distance < bestDistance ||
            (distance == bestDistance && fpsOk && !bestFpsOk)) {
            bestIndex = static_cast<int>(i);
            bestDistance = distance;
            bestFpsOk = fpsOk;
        }
    }
    return bestIndex;
}

// Returns deviceId (the /dev/videoN number on Linux), not vector position.
// Empty or missing IDs must never fall back to the default camera.
template<class Device>
inline int resolveGrabberUniqueID(const std::vector<Device>& devices,
                                  const std::string& uniqueId) {
    if (uniqueId.empty()) return -1;
    for (const auto& device : devices) {
        if (device.uniqueId == uniqueId) return device.deviceId;
    }
    return -1;
}

} // namespace trussc::internal
