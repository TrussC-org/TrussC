// =============================================================================
// tcVideoGrabber_linux.cpp - Linux V4L2 implementation
// =============================================================================
// Uses Video4Linux2 (V4L2) for webcam capture.
// Supports YUYV and MJPEG formats, converts to RGBA.
// =============================================================================

#ifdef __linux__

#include "TrussC.h"
#include "tcVideoGrabberLinux.h"
#include "tc/utils/tcSimd.h"
#include <filesystem>
#include <algorithm>

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cerrno>
#include <cstring>
#include <thread>
#include <atomic>
#include <chrono>
#include <limits>

// For MJPEG decoding
#define STB_IMAGE_IMPLEMENTATION_ALREADY_DONE
#include <stb/stb_image.h>

namespace trussc {

static int xioctl(int fd, unsigned long request, void* arg);

// =============================================================================
// Platform data structure
// =============================================================================
struct VideoGrabberPlatformData {
    int fd = -1;

    // Memory-mapped buffers
    struct Buffer {
        void* start = nullptr;
        size_t length = 0;
    };
    Buffer* buffers = nullptr;
    unsigned int bufferCount = 0;

    // Back buffer for double buffering
    unsigned char* backBuffer = nullptr;
    int bufferWidth = 0;
    int bufferHeight = 0;
    size_t rgbaBytes = 0;
    internal::MjpegDhtBuffer mjpegScratch;

    // Resize notification
    std::atomic<bool> needsResize{false};
    int newWidth = 0;
    int newHeight = 0;

    ~VideoGrabberPlatformData() {
        if (fd != -1) {
            auto type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            xioctl(fd, VIDIOC_STREAMOFF, &type);
        }
        for (unsigned int i = 0; buffers && i < bufferCount; ++i) {
            if (buffers[i].start && buffers[i].start != MAP_FAILED)
                munmap(buffers[i].start, buffers[i].length);
        }
        delete[] buffers;
        delete[] backBuffer;
        if (fd != -1) ::close(fd);
    }

    // Format info
    uint32_t pixelFormat = 0;
};

// =============================================================================
// Helper functions
// =============================================================================

static bool checkedRgbaBytes(int64_t width, int64_t height, size_t& bytes) {
    if (width <= 0 || height <= 0) return false;
    if (uint64_t(width) > std::numeric_limits<size_t>::max() / 4 / uint64_t(height))
        return false;
    // Bound (gpu): 16384 per axis matches the image decoder's fallback texture limit; larger negotiated frames fail setup.
    if (width > 16384 || height > 16384) return false;
    bytes = size_t(width) * size_t(height) * 4;
    return true;
}

static int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

// Decode MJPEG to RGBA using stb_image
static bool decodeMJPEGtoRGBA(const unsigned char* src, size_t srcSize,
                               unsigned char* dst, int width, int height, size_t rgbaBytes,
                               internal::MjpegDhtBuffer& scratch) {
    const auto jpeg = scratch.prepare({src, srcSize});
    // Bound (decoder ABI): stb_image takes an int byte count.
    if (jpeg.size() > size_t((std::numeric_limits<int>::max)())) return false;
    int w, h, channels;
    unsigned char* decoded = stbi_load_from_memory(jpeg.data(), static_cast<int>(jpeg.size()), &w, &h, &channels, 4);

    if (!decoded) {
        return false;
    }

    if (w == width && h == height) {
        memcpy(dst, decoded, rgbaBytes);
    }

    stbi_image_free(decoded);
    return w == width && h == height;
}

// =============================================================================
// Capture thread function
// =============================================================================

// Only what the kernel says outright is a loss: ENODEV after the camera is
// unplugged. Other errors just end the capture thread, as before.
static void reportIfGone(const std::shared_ptr<internal::GrabberSharedState>& state, const char* call) {
    if (errno != ENODEV) return;
    state->reportLost(std::string(call) + ": " + std::strerror(ENODEV));
}

static void captureThreadFunc(std::shared_ptr<internal::GrabberSharedState> state) {
    auto* data = static_cast<VideoGrabberPlatformData*>(state->platformData.get());
    while (!state->stopped.load()) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->fd, &fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int r = select(data->fd + 1, &fds, nullptr, nullptr, &tv);

        if (r == -1) {
            if (errno == EINTR) continue;
            reportIfGone(state, "select");
            break;
        }

        if (r == 0) {
            // Timeout
            continue;
        }

        // Dequeue buffer
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;

        if (xioctl(data->fd, VIDIOC_DQBUF, &buf) == -1) {
            if (errno == EAGAIN) continue;
            reportIfGone(state, "VIDIOC_DQBUF");
            break;
        }

        // Stamp arrival time on the capture thread, before any processing.
        // Stays accurate even when the app's main loop stalls.
        uint64_t arrivalUs = (uint64_t)std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();

        // Convert frame to RGBA
        if (data->backBuffer && buf.index < data->bufferCount) {
            const unsigned char* src = (const unsigned char*)data->buffers[buf.index].start;

            bool converted = false;
            if (data->pixelFormat == V4L2_PIX_FMT_YUYV) {
                // A YUYV frame holds 2 bytes per pixel; skip a short buffer.
                if (buf.bytesused < data->rgbaBytes / 2) {
                    xioctl(data->fd, VIDIOC_QBUF, &buf);
                    continue;
                }
                simd::yuyvToRgba(src, data->backBuffer, data->rgbaBytes / 4);
                converted = true;
            } else if (data->pixelFormat == V4L2_PIX_FMT_MJPEG) {
                converted = decodeMJPEGtoRGBA(src, buf.bytesused, data->backBuffer,
                                              data->bufferWidth, data->bufferHeight, data->rgbaBytes, data->mjpegScratch);
            }

            // A frame that didn't convert (e.g. an MJPEG of another size) is not published.
            if (converted) {
                state->notifyFrame(data->backBuffer, data->bufferWidth,
                                   data->bufferHeight, arrivalUs);

                // Publish under the State lock, including pointer/size checks.
                {
                    std::lock_guard<std::mutex> lock(state->mtx);
                    if (state->pixels && state->width == data->bufferWidth &&
                        state->height == data->bufferHeight) {
                        std::memcpy(state->pixels.get(), data->backBuffer,
                                    data->rgbaBytes);
                        state->dirty.store(true);
                    }
                }
                state->frameQueue.push(data->backBuffer, data->bufferWidth,
                                       data->bufferHeight, arrivalUs);
            }
        }

        // Re-queue buffer
        if (xioctl(data->fd, VIDIOC_QBUF, &buf) == -1) {
            reportIfGone(state, "VIDIOC_QBUF");
            break;
        }
    }
}

// =============================================================================
// VideoGrabber platform methods
// =============================================================================

bool VideoGrabber::setupPlatform() {
    auto data = new VideoGrabberPlatformData();
    state_->platformData = std::shared_ptr<VideoGrabberPlatformData>(data);

    // Open device
    std::string devicePath = "/dev/video" + std::to_string(deviceId_);
    data->fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);

    if (data->fd == -1) {
        logError("VideoGrabber") << "Failed to open " << devicePath;
        state_->platformData.reset();
        return false;
    }

    // Query capabilities
    struct v4l2_capability cap;
    if (xioctl(data->fd, VIDIOC_QUERYCAP, &cap) == -1) {
        logError("VideoGrabber") << "Failed to query capabilities";
        state_->platformData.reset();
        return false;
    }

    if (!internal::isVideoCaptureDevice(cap)) {
        logError("VideoGrabber") << "Device does not support video capture";
        state_->platformData.reset();
        return false;
    }

    deviceName_ = std::string((char*)cap.card);
    logNotice("VideoGrabber") << "Device: " << deviceName_;

    // Set format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = state_->requestedWidth;
    fmt.fmt.pix.height = state_->requestedHeight;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;  // Try MJPEG first
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(data->fd, VIDIOC_S_FMT, &fmt) == -1) {
        // Try YUYV
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (xioctl(data->fd, VIDIOC_S_FMT, &fmt) == -1) {
            logError("VideoGrabber") << "Failed to set format";
            state_->platformData.reset();
            return false;
        }
    }

    if (!checkedRgbaBytes(fmt.fmt.pix.width, fmt.fmt.pix.height, data->rgbaBytes) ||
        (fmt.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV && fmt.fmt.pix.width % 2 != 0)) {
        logError("VideoGrabber") << "Invalid negotiated frame size";
        state_->platformData.reset();
        return false;
    }

    state_->width = fmt.fmt.pix.width;
    state_->height = fmt.fmt.pix.height;
    data->bufferWidth = state_->width;
    data->bufferHeight = state_->height;
    data->pixelFormat = fmt.fmt.pix.pixelformat;

    char fourcc[5] = {};
    std::memcpy(fourcc, &data->pixelFormat, 4);
    logNotice("VideoGrabber") << "Format: " << state_->width << "x" << state_->height
                                << " (" << fourcc << ")";

    // Set frame rate if specified
    if (desiredFrameRate_ > 0) {
        struct v4l2_streamparm parm = {};
        parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        parm.parm.capture.timeperframe.numerator = 1;
        parm.parm.capture.timeperframe.denominator = desiredFrameRate_;
        xioctl(data->fd, VIDIOC_S_PARM, &parm);
    }

    // Request buffers
    struct v4l2_requestbuffers req = {};
    req.count = 4;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (xioctl(data->fd, VIDIOC_REQBUFS, &req) == -1) {
        logError("VideoGrabber") << "Failed to request buffers";
        state_->platformData.reset();
        return false;
    }

    data->bufferCount = req.count;
    data->buffers = new VideoGrabberPlatformData::Buffer[req.count];

    // Map buffers
    size_t maxBufferLength = 0;
    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(data->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            logError("VideoGrabber") << "Failed to query buffer";
            state_->platformData.reset();
            return false;
        }

        data->buffers[i].length = buf.length;
        maxBufferLength = std::max(maxBufferLength, data->buffers[i].length);
        data->buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, data->fd, buf.m.offset);

        if (data->buffers[i].start == MAP_FAILED) {
            logError("VideoGrabber") << "Failed to mmap buffer";
            state_->platformData.reset();
            return false;
        }
    }

    // Bound (driver buffer length): scratch holds the largest VIDIOC_QUERYBUF buffer plus the DHT block.
    if (data->pixelFormat == V4L2_PIX_FMT_MJPEG && !data->mjpegScratch.allocate(maxBufferLength)) {
        logError("VideoGrabber") << "MJPEG scratch size overflow";
        state_->platformData.reset();
        return false;
    }

    // Allocate back buffer
    data->backBuffer = new unsigned char[data->rgbaBytes];

    // Queue buffers
    for (unsigned int i = 0; i < data->bufferCount; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(data->fd, VIDIOC_QBUF, &buf) == -1) {
            logError("VideoGrabber") << "Failed to queue buffer";
        }
    }

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (xioctl(data->fd, VIDIOC_STREAMON, &type) == -1) {
        logError("VideoGrabber") << "Failed to start streaming";
        state_->platformData.reset();
        return false;
    }

    // Start capture thread
    state_->keptThreads.start(state_->captureThread, [state = state_] {
        captureThreadFunc(state);
    });

    logNotice("VideoGrabber") << "Started capturing";
    return true;
}

void VideoGrabber::closePlatform() {
    // close() joins without holding the pixel lock. The State then releases
    // the stream, mappings and fd; a self-stop leaves them with the worker.
}

void VideoGrabber::updatePlatform() {
    // Nothing special needed - capture thread handles everything
}

void VideoGrabber::updateDelegatePixels() {
    // Callbacks read pixels directly from the State under its lock.
}

std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    std::vector<VideoDeviceInfo> devices;

    // by-path identifies a camera by its USB port, even for identical models.
    std::vector<std::filesystem::path> byPathLinks;
    std::error_code pathError;
    for (const auto& entry : std::filesystem::directory_iterator("/dev/v4l/by-path", pathError)) {
        byPathLinks.push_back(entry.path());
    }
    std::sort(byPathLinks.begin(), byPathLinks.end());

    // Enumerate actual nodes and sort by numeric suffix (video2 before video10).
    std::vector<std::pair<int, std::filesystem::path>> nodes;
    std::error_code deviceError;
    for (const auto& entry : std::filesystem::directory_iterator("/dev", deviceError)) {
        const int number = internal::videoDeviceNumber(entry.path().filename().string());
        if (number >= 0) nodes.emplace_back(number, entry.path());
    }
    std::sort(nodes.begin(), nodes.end());
    for (const auto& [number, node] : nodes) {
        std::string path = node.string();
        int fd = open(path.c_str(), O_RDWR);
        if (fd == -1) continue;

        struct v4l2_capability cap;
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            if (internal::isVideoCaptureDevice(cap)) {
                VideoDeviceInfo info;
                info.deviceId = number;
                info.deviceName = std::string((char*)cap.card);
                info.uniqueId = path;
                for (const auto& link : byPathLinks) {
                    std::error_code linkError;
                    if (std::filesystem::equivalent(link, path, linkError)) {
                        info.uniqueId = link.string();
                        break;
                    }
                }
                devices.push_back(info);
            }
        }
        ::close(fd);
    }

    return devices;
}

bool VideoGrabber::checkResizeNeeded() {
    if (!state_->platformData) return false;
    auto data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    return data->needsResize.load();
}

void VideoGrabber::getNewSize(int& width, int& height) {
    if (!state_->platformData) return;
    auto data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    width = data->newWidth;
    height = data->newHeight;
}

void VideoGrabber::clearResizeFlag() {
    if (!state_->platformData) return;
    auto data = static_cast<VideoGrabberPlatformData*>(state_->platformData.get());
    data->needsResize = false;
}

// =============================================================================
// Permission methods (always granted on Linux)
// =============================================================================

bool VideoGrabber::checkCameraPermission() {
    // Linux doesn't have camera permission dialogs
    // Access is controlled by /dev/video* permissions (video group)
    return true;
}

void VideoGrabber::requestCameraPermission() {
    // Nothing to do on Linux
}

} // namespace trussc

#endif // __linux__
