#pragma once

// Internal Linux capture helpers, shared with headless tests.
#include <linux/videodev2.h>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
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
    const auto& pixel = result.format.fmt.pix;
    if (first == V4L2_PIX_FMT_YUYV &&
        (pixel.pixelformat != first || pixel.width != width || pixel.height != height || result.fps < requiredFps) &&
        mjpgFps > 0 && mjpgFps >= requiredFps) {
        result.reason = "YUYV negotiated a different format/size or insufficient fps; falling back to MJPG";
        return attempt(V4L2_PIX_FMT_MJPEG);
    }
    return true;
}

// JPEG Annex K default DC/AC Huffman tables, luminance and chrominance.
// One DHT segment: marker + length (418) + four table definitions.
inline constexpr unsigned char mjpegDefaultDht[] = {
    0xff, 0xc4, 0x01, 0xa2, 0x00, 0x00, 0x01, 0x05, 0x01, 0x01, 0x01, 0x01,
    0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x02,
    0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a, 0x0b, 0x10, 0x00, 0x02,
    0x01, 0x03, 0x03, 0x02, 0x04, 0x03, 0x05, 0x05, 0x04, 0x04, 0x00, 0x00,
    0x01, 0x7d, 0x01, 0x02, 0x03, 0x00, 0x04, 0x11, 0x05, 0x12, 0x21, 0x31,
    0x41, 0x06, 0x13, 0x51, 0x61, 0x07, 0x22, 0x71, 0x14, 0x32, 0x81, 0x91,
    0xa1, 0x08, 0x23, 0x42, 0xb1, 0xc1, 0x15, 0x52, 0xd1, 0xf0, 0x24, 0x33,
    0x62, 0x72, 0x82, 0x09, 0x0a, 0x16, 0x17, 0x18, 0x19, 0x1a, 0x25, 0x26,
    0x27, 0x28, 0x29, 0x2a, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x43,
    0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a, 0x53, 0x54, 0x55, 0x56, 0x57,
    0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66, 0x67, 0x68, 0x69, 0x6a, 0x73,
    0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a, 0x83, 0x84, 0x85, 0x86, 0x87,
    0x88, 0x89, 0x8a, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98, 0x99, 0x9a,
    0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7, 0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4,
    0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
    0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda,
    0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7, 0xe8, 0xe9, 0xea, 0xf1, 0xf2,
    0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa, 0x01, 0x00, 0x03, 0x01,
    0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0a,
    0x0b, 0x11, 0x00, 0x02, 0x01, 0x02, 0x04, 0x04, 0x03, 0x04, 0x07, 0x05,
    0x04, 0x04, 0x00, 0x01, 0x02, 0x77, 0x00, 0x01, 0x02, 0x03, 0x11, 0x04,
    0x05, 0x21, 0x31, 0x06, 0x12, 0x41, 0x51, 0x07, 0x61, 0x71, 0x13, 0x22,
    0x32, 0x81, 0x08, 0x14, 0x42, 0x91, 0xa1, 0xb1, 0xc1, 0x09, 0x23, 0x33,
    0x52, 0xf0, 0x15, 0x62, 0x72, 0xd1, 0x0a, 0x16, 0x24, 0x34, 0xe1, 0x25,
    0xf1, 0x17, 0x18, 0x19, 0x1a, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x35, 0x36,
    0x37, 0x38, 0x39, 0x3a, 0x43, 0x44, 0x45, 0x46, 0x47, 0x48, 0x49, 0x4a,
    0x53, 0x54, 0x55, 0x56, 0x57, 0x58, 0x59, 0x5a, 0x63, 0x64, 0x65, 0x66,
    0x67, 0x68, 0x69, 0x6a, 0x73, 0x74, 0x75, 0x76, 0x77, 0x78, 0x79, 0x7a,
    0x82, 0x83, 0x84, 0x85, 0x86, 0x87, 0x88, 0x89, 0x8a, 0x92, 0x93, 0x94,
    0x95, 0x96, 0x97, 0x98, 0x99, 0x9a, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
    0xa8, 0xa9, 0xaa, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7, 0xb8, 0xb9, 0xba,
    0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7, 0xc8, 0xc9, 0xca, 0xd2, 0xd3, 0xd4,
    0xd5, 0xd6, 0xd7, 0xd8, 0xd9, 0xda, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
    0xe8, 0xe9, 0xea, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf8, 0xf9, 0xfa,
};
static_assert(sizeof(mjpegDefaultDht) == 420);

class MjpegDhtBuffer {
public:
    bool allocate(size_t maxFrameSize) {
        if (maxFrameSize > (std::numeric_limits<size_t>::max)() - sizeof(mjpegDefaultDht))
            return false;
        capacity_ = maxFrameSize + sizeof(mjpegDefaultDht);
        bytes_ = std::make_unique<unsigned char[]>(capacity_);
        return true;
    }

    // Malformed headers and frames with DHT pass through unchanged.
    std::span<const unsigned char> prepare(std::span<const unsigned char> frame) {
        if (frame.size() < 2 || frame[0] != 0xff || frame[1] != 0xd8) return frame;
        bool hasDht = false;
        size_t pos = 2;
        while (pos < frame.size()) {
            if (frame[pos++] != 0xff) return frame;
            while (pos < frame.size() && frame[pos] == 0xff) ++pos; // Marker fill bytes.
            if (pos == frame.size()) return frame;
            const auto marker = frame[pos++];
            if (marker == 0x01) continue; // TEM has no length.
            if (marker == 0x00 || marker == 0xd8 || marker == 0xd9 ||
                (marker >= 0xd0 && marker <= 0xd7)) return frame;
            if (frame.size() - pos < 2) return frame;
            const size_t length = (size_t(frame[pos]) << 8) | frame[pos + 1];
            if (length < 2 || length > frame.size() - pos) return frame;
            pos += length;
            if (marker == 0xc4) hasDht = true;
            if (marker != 0xda) continue;
            if (hasDht) return frame;

            if (capacity_ < sizeof(mjpegDefaultDht) || frame.size() > capacity_ - sizeof(mjpegDefaultDht))
                return frame;
            const size_t size = frame.size() + sizeof(mjpegDefaultDht);
            std::memcpy(bytes_.get(), frame.data(), 2);
            std::memcpy(bytes_.get() + 2, mjpegDefaultDht, sizeof(mjpegDefaultDht));
            std::memcpy(bytes_.get() + 2 + sizeof(mjpegDefaultDht), frame.data() + 2, frame.size() - 2);
            return {bytes_.get(), size};
        }
        return frame; // No SOS found.
    }

private:
    std::unique_ptr<unsigned char[]> bytes_;
    size_t capacity_ = 0;
};

} // namespace trussc::internal
