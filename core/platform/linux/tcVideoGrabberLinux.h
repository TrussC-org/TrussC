#pragma once

// Internal Linux capture helpers, shared with headless tests.
#include <linux/videodev2.h>
#include <charconv>
#include <string_view>

namespace trussc::internal {

inline bool isVideoCaptureDevice(const v4l2_capability& caps) {
    const auto effective = (caps.capabilities & V4L2_CAP_DEVICE_CAPS)
        ? caps.device_caps : caps.capabilities;
    return (effective & V4L2_CAP_VIDEO_CAPTURE) != 0;
}

// IDs remain the numeric suffix of /dev/videoN, rather than list positions.
inline int videoDeviceNumber(std::string_view name) {
    if (!name.starts_with("video")) return -1;
    name.remove_prefix(5);
    if (name.empty() || name.front() < '0' || name.front() > '9') return -1;
    if (name.size() > 1 && name.front() == '0') return -1;
    int number = -1;
    const auto result = std::from_chars(name.data(), name.data() + name.size(), number);
    return result.ec == std::errc{} && result.ptr == name.data() + name.size() ? number : -1;
}

} // namespace trussc::internal
