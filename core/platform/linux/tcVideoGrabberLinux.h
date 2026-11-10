#pragma once

// Internal Linux capture helpers, shared with headless tests.
#include <linux/videodev2.h>
#include <algorithm>
#include <charconv>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

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

// One enumerated size, with discrete intervals or a continuous/stepwise range.
struct LinuxCaptureMode {
    uint32_t fourcc;
    uint32_t width;
    uint32_t height;
    std::vector<v4l2_frmivalenum> intervals;
};

inline double captureFps(v4l2_fract interval) {
    return interval.numerator && interval.denominator
        ? double(interval.denominator) / interval.numerator : 0;
}

inline v4l2_fract fastestCaptureInterval(std::span<const LinuxCaptureMode> modes, uint32_t fourcc,
                                         uint32_t width, uint32_t height) {
    v4l2_fract fastest{};
    for (const auto& mode : modes) {
        if (mode.fourcc != fourcc || mode.width != width || mode.height != height) continue;
        for (const auto& interval : mode.intervals) {
            const auto candidate = interval.type == V4L2_FRMIVAL_TYPE_DISCRETE
                ? interval.discrete : interval.stepwise.min;
            if (captureFps(candidate) > captureFps(fastest)) fastest = candidate;
        }
    }
    return fastest;
}

inline double maxCaptureFps(std::span<const LinuxCaptureMode> modes, uint32_t fourcc,
                            uint32_t width, uint32_t height) {
    return captureFps(fastestCaptureInterval(modes, fourcc, width, height));
}

// Unknown YUYV capabilities retain the old MJPG-first behavior.
inline uint32_t selectLinuxCaptureFormat(std::span<const LinuxCaptureMode> modes,
                                         uint32_t width, uint32_t height, int requestedFps) {
    const double yuyvFps = maxCaptureFps(modes, V4L2_PIX_FMT_YUYV, width, height);
    const double requiredFps = requestedFps > 0 ? requestedFps
        : maxCaptureFps(modes, V4L2_PIX_FMT_MJPEG, width, height);
    return yuyvFps > 0 && yuyvFps >= requiredFps ? V4L2_PIX_FMT_YUYV : V4L2_PIX_FMT_MJPEG;
}

// Enumerate only the requested size, including sizes in a stepwise/continuous range.
// The injected ioctl also lets headless tests exercise the real negotiation path.
template<class Ioctl>
std::vector<LinuxCaptureMode> enumerateLinuxCaptureModes(Ioctl&& call, uint32_t width, uint32_t height) {
    std::vector<LinuxCaptureMode> modes;
    v4l2_fmtdesc format{};
    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    for (; call(VIDIOC_ENUM_FMT, &format) == 0; ++format.index) {
        if (format.pixelformat != V4L2_PIX_FMT_YUYV && format.pixelformat != V4L2_PIX_FMT_MJPEG) continue;
        v4l2_frmsizeenum size{};
        size.pixel_format = format.pixelformat;
        for (; call(VIDIOC_ENUM_FRAMESIZES, &size) == 0; ++size.index) {
            bool matches = size.type == V4L2_FRMSIZE_TYPE_DISCRETE
                ? size.discrete.width == width && size.discrete.height == height
                : width >= size.stepwise.min_width && width <= size.stepwise.max_width &&
                  height >= size.stepwise.min_height && height <= size.stepwise.max_height &&
                  (size.type == V4L2_FRMSIZE_TYPE_CONTINUOUS ||
                   (size.stepwise.step_width && size.stepwise.step_height &&
                    (width - size.stepwise.min_width) % size.stepwise.step_width == 0 &&
                    (height - size.stepwise.min_height) % size.stepwise.step_height == 0));
            if (matches) {
                LinuxCaptureMode mode{format.pixelformat, width, height, {}};
                v4l2_frmivalenum interval{};
                interval.pixel_format = mode.fourcc;
                interval.width = width;
                interval.height = height;
                while (call(VIDIOC_ENUM_FRAMEINTERVALS, &interval) == 0) {
                    mode.intervals.push_back(interval);
                    if (interval.type != V4L2_FRMIVAL_TYPE_DISCRETE) break;
                    ++interval.index;
                }
                modes.push_back(std::move(mode));
            }
            if (size.type != V4L2_FRMSIZE_TYPE_DISCRETE) break;
        }
    }
    return modes;
}

struct LinuxCaptureNegotiation {
    v4l2_format format{};
    double fps = 0;
    const char* reason = "";
};

template<class Ioctl>
bool negotiateLinuxCapture(Ioctl&& call, std::span<const LinuxCaptureMode> modes,
                           uint32_t width, uint32_t height, int requestedFps,
                           LinuxCaptureNegotiation& result) {
    const auto first = selectLinuxCaptureFormat(modes, width, height, requestedFps);
    result.reason = first == V4L2_PIX_FMT_YUYV
        ? (requestedFps > 0 ? "YUYV advertises requested fps" : "YUYV advertised max fps >= MJPG max fps")
        : "YUYV size/fps unavailable or slower than MJPG";
    auto attempt = [&](uint32_t fourcc) {
        result.format = {};
        result.format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        auto& pixel = result.format.fmt.pix;
        pixel.width = width;
        pixel.height = height;
        pixel.pixelformat = fourcc;
        pixel.field = V4L2_FIELD_NONE;
        if (call(VIDIOC_S_FMT, &result.format) != 0) return false;
        v4l2_streamparm parm{};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        const v4l2_fract target = requestedFps > 0 ? v4l2_fract{1, uint32_t(requestedFps)}
            : fastestCaptureInterval(modes, pixel.pixelformat, width, height);
        result.fps = 0;
        if (captureFps(target) > 0) {
            parm.parm.capture.timeperframe = target;
            if (call(VIDIOC_S_PARM, &parm) == 0)
                result.fps = captureFps(parm.parm.capture.timeperframe);
        }
        parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        if (call(VIDIOC_G_PARM, &parm) == 0)
            result.fps = captureFps(parm.parm.capture.timeperframe);
        // S_FMT returns the negotiated format; read it again after S_PARM.
        auto actual = result.format;
        if (call(VIDIOC_G_FMT, &actual) == 0) result.format = actual;
        return true;
    };
    if (!attempt(first)) {
        result.reason = "preferred format rejected; trying alternate format";
        return attempt(first == V4L2_PIX_FMT_YUYV ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV);
    }
    const double mjpgFps = maxCaptureFps(modes, V4L2_PIX_FMT_MJPEG, width, height);
    const double requiredFps = requestedFps > 0 ? requestedFps : mjpgFps;
    const bool mjpgOffersSize = std::any_of(modes.begin(), modes.end(), [&](const auto& mode) {
        return mode.fourcc == V4L2_PIX_FMT_MJPEG && mode.width == width && mode.height == height;
    });
    const auto& pixel = result.format.fmt.pix;
    if (first == V4L2_PIX_FMT_YUYV &&
        (pixel.pixelformat != first || pixel.width != width || pixel.height != height || result.fps < requiredFps) &&
        mjpgOffersSize) {
        result.reason = "YUYV negotiated a different format/size or insufficient fps; falling back to MJPG";
        return attempt(V4L2_PIX_FMT_MJPEG);
    }
    return true;
}

} // namespace trussc::internal
