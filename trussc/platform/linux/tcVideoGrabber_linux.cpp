// =============================================================================
// tcVideoGrabber_linux.cpp - Linux V4L2 implementation
// =============================================================================
// Uses Video4Linux2 (V4L2) for webcam capture.
// Supports YUYV and MJPEG formats, converts to RGBA.
// =============================================================================

#ifdef __linux__

#include "TrussC.h"

#include <linux/videodev2.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <cstring>
#include <thread>
#include <atomic>

// For MJPEG decoding
#define STB_IMAGE_IMPLEMENTATION_ALREADY_DONE
#include <stb/stb_image.h>

namespace trussc {

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

    // Capture thread
    std::thread captureThread;
    std::atomic<bool> running{false};
    std::atomic<bool> bufferReady{false};

    // Resize notification
    std::atomic<bool> needsResize{false};
    int newWidth = 0;
    int newHeight = 0;

    // Format info
    uint32_t pixelFormat = 0;

    // Pointer to VideoGrabber members
    unsigned char* targetPixels = nullptr;
    std::atomic<bool>* pixelsDirty = nullptr;
    std::mutex* mutex = nullptr;
};

// =============================================================================
// Helper functions
// =============================================================================

static int xioctl(int fd, unsigned long request, void* arg) {
    int r;
    do {
        r = ioctl(fd, request, arg);
    } while (r == -1 && errno == EINTR);
    return r;
}

// Convert YUYV (YUV422) to RGBA
static void convertYUYVtoRGBA(const unsigned char* src, unsigned char* dst, int width, int height) {
    for (int i = 0; i < width * height; i += 2) {
        int y0 = src[0];
        int u  = src[1];
        int y1 = src[2];
        int v  = src[3];
        src += 4;

        // YUV to RGB conversion
        int c = y0 - 16;
        int d = u - 128;
        int e = v - 128;

        int r = (298 * c + 409 * e + 128) >> 8;
        int g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        int b = (298 * c + 516 * d + 128) >> 8;

        dst[0] = (r < 0) ? 0 : (r > 255) ? 255 : r;
        dst[1] = (g < 0) ? 0 : (g > 255) ? 255 : g;
        dst[2] = (b < 0) ? 0 : (b > 255) ? 255 : b;
        dst[3] = 255;
        dst += 4;

        c = y1 - 16;
        r = (298 * c + 409 * e + 128) >> 8;
        g = (298 * c - 100 * d - 208 * e + 128) >> 8;
        b = (298 * c + 516 * d + 128) >> 8;

        dst[0] = (r < 0) ? 0 : (r > 255) ? 255 : r;
        dst[1] = (g < 0) ? 0 : (g > 255) ? 255 : g;
        dst[2] = (b < 0) ? 0 : (b > 255) ? 255 : b;
        dst[3] = 255;
        dst += 4;
    }
}

// Decode MJPEG to RGBA using stb_image
static bool decodeMJPEGtoRGBA(const unsigned char* src, size_t srcSize,
                               unsigned char* dst, int width, int height) {
    int w, h, channels;
    unsigned char* decoded = stbi_load_from_memory(src, srcSize, &w, &h, &channels, 4);

    if (!decoded) {
        return false;
    }

    if (w == width && h == height) {
        memcpy(dst, decoded, width * height * 4);
    }

    stbi_image_free(decoded);
    return true;
}

// =============================================================================
// Capture thread function
// =============================================================================
static void captureThreadFunc(VideoGrabberPlatformData* data) {
    while (data->running) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(data->fd, &fds);

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        int r = select(data->fd + 1, &fds, nullptr, nullptr, &tv);

        if (r == -1) {
            if (errno == EINTR) continue;
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
            break;
        }

        // Convert frame to RGBA
        if (data->backBuffer && buf.index < data->bufferCount) {
            const unsigned char* src = (const unsigned char*)data->buffers[buf.index].start;

            if (data->pixelFormat == V4L2_PIX_FMT_YUYV) {
                convertYUYVtoRGBA(src, data->backBuffer, data->bufferWidth, data->bufferHeight);
            } else if (data->pixelFormat == V4L2_PIX_FMT_MJPEG) {
                decodeMJPEGtoRGBA(src, buf.bytesused, data->backBuffer,
                                  data->bufferWidth, data->bufferHeight);
            }

            // Copy to target buffer
            if (data->targetPixels && data->mutex) {
                std::lock_guard<std::mutex> lock(*data->mutex);
                memcpy(data->targetPixels, data->backBuffer,
                       data->bufferWidth * data->bufferHeight * 4);
            }

            if (data->pixelsDirty) {
                data->pixelsDirty->store(true);
            }
        }

        // Re-queue buffer
        if (xioctl(data->fd, VIDIOC_QBUF, &buf) == -1) {
            break;
        }
    }
}

// =============================================================================
// VideoGrabber platform methods
// =============================================================================

bool VideoGrabber::setupPlatform() {
    auto data = new VideoGrabberPlatformData();
    platformHandle_ = data;

    // Open device
    std::string devicePath = "/dev/video" + std::to_string(deviceId_);
    data->fd = open(devicePath.c_str(), O_RDWR | O_NONBLOCK);

    if (data->fd == -1) {
        logError("VideoGrabber") << "Failed to open " << devicePath;
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    // Query capabilities
    struct v4l2_capability cap;
    if (xioctl(data->fd, VIDIOC_QUERYCAP, &cap) == -1) {
        logError("VideoGrabber") << "Failed to query capabilities";
        ::close(data->fd);
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        logError("VideoGrabber") << "Device does not support video capture";
        ::close(data->fd);
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    deviceName_ = std::string((char*)cap.card);
    logNotice("VideoGrabber") << "Device: " << deviceName_;

    // Set format
    struct v4l2_format fmt = {};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = requestedWidth_;
    fmt.fmt.pix.height = requestedHeight_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;  // Try MJPEG first
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (xioctl(data->fd, VIDIOC_S_FMT, &fmt) == -1) {
        // Try YUYV
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (xioctl(data->fd, VIDIOC_S_FMT, &fmt) == -1) {
            logError("VideoGrabber") << "Failed to set format";
            ::close(data->fd);
            delete data;
            platformHandle_ = nullptr;
            return false;
        }
    }

    width_ = fmt.fmt.pix.width;
    height_ = fmt.fmt.pix.height;
    data->bufferWidth = width_;
    data->bufferHeight = height_;
    data->pixelFormat = fmt.fmt.pix.pixelformat;

    logNotice("VideoGrabber") << "Format: " << width_ << "x" << height_
                                << " (" << (char*)&data->pixelFormat << ")";

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
        ::close(data->fd);
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    data->bufferCount = req.count;
    data->buffers = new VideoGrabberPlatformData::Buffer[req.count];

    // Map buffers
    for (unsigned int i = 0; i < req.count; i++) {
        struct v4l2_buffer buf = {};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (xioctl(data->fd, VIDIOC_QUERYBUF, &buf) == -1) {
            logError("VideoGrabber") << "Failed to query buffer";
            // Cleanup
            for (unsigned int j = 0; j < i; j++) {
                munmap(data->buffers[j].start, data->buffers[j].length);
            }
            delete[] data->buffers;
            ::close(data->fd);
            delete data;
            platformHandle_ = nullptr;
            return false;
        }

        data->buffers[i].length = buf.length;
        data->buffers[i].start = mmap(nullptr, buf.length, PROT_READ | PROT_WRITE,
                                       MAP_SHARED, data->fd, buf.m.offset);

        if (data->buffers[i].start == MAP_FAILED) {
            logError("VideoGrabber") << "Failed to mmap buffer";
            for (unsigned int j = 0; j < i; j++) {
                munmap(data->buffers[j].start, data->buffers[j].length);
            }
            delete[] data->buffers;
            ::close(data->fd);
            delete data;
            platformHandle_ = nullptr;
            return false;
        }
    }

    // Allocate back buffer
    data->backBuffer = new unsigned char[width_ * height_ * 4];

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
        // Cleanup
        for (unsigned int i = 0; i < data->bufferCount; i++) {
            munmap(data->buffers[i].start, data->buffers[i].length);
        }
        delete[] data->buffers;
        delete[] data->backBuffer;
        ::close(data->fd);
        delete data;
        platformHandle_ = nullptr;
        return false;
    }

    // Start capture thread
    data->running = true;
    data->captureThread = std::thread(captureThreadFunc, data);

    logNotice("VideoGrabber") << "Started capturing";
    return true;
}

void VideoGrabber::closePlatform() {
    if (!platformHandle_) return;

    auto data = static_cast<VideoGrabberPlatformData*>(platformHandle_);

    // Stop capture thread
    data->running = false;
    if (data->captureThread.joinable()) {
        data->captureThread.join();
    }

    // Stop streaming
    if (data->fd != -1) {
        enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        xioctl(data->fd, VIDIOC_STREAMOFF, &type);
    }

    // Unmap buffers
    if (data->buffers) {
        for (unsigned int i = 0; i < data->bufferCount; i++) {
            if (data->buffers[i].start && data->buffers[i].start != MAP_FAILED) {
                munmap(data->buffers[i].start, data->buffers[i].length);
            }
        }
        delete[] data->buffers;
    }

    // Free back buffer
    if (data->backBuffer) {
        delete[] data->backBuffer;
    }

    // Close device
    if (data->fd != -1) {
        ::close(data->fd);
    }

    delete data;
    platformHandle_ = nullptr;
}

void VideoGrabber::updatePlatform() {
    // Nothing special needed - capture thread handles everything
}

void VideoGrabber::updateDelegatePixels() {
    if (!platformHandle_) return;

    auto data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    data->targetPixels = pixels_;
    data->pixelsDirty = &pixelsDirty_;
    data->mutex = &mutex_;
}

std::vector<VideoDeviceInfo> VideoGrabber::listDevicesPlatform() {
    std::vector<VideoDeviceInfo> devices;

    // Scan /dev/video*
    for (int i = 0; i < 10; i++) {
        std::string path = "/dev/video" + std::to_string(i);
        int fd = open(path.c_str(), O_RDWR);
        if (fd == -1) continue;

        struct v4l2_capability cap;
        if (xioctl(fd, VIDIOC_QUERYCAP, &cap) == 0) {
            if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE) {
                VideoDeviceInfo info;
                info.deviceId = i;
                info.deviceName = std::string((char*)cap.card);
                info.uniqueId = path;
                devices.push_back(info);
            }
        }
        ::close(fd);
    }

    return devices;
}

bool VideoGrabber::checkResizeNeeded() {
    if (!platformHandle_) return false;
    auto data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    return data->needsResize.load();
}

void VideoGrabber::getNewSize(int& width, int& height) {
    if (!platformHandle_) return;
    auto data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
    width = data->newWidth;
    height = data->newHeight;
}

void VideoGrabber::clearResizeFlag() {
    if (!platformHandle_) return;
    auto data = static_cast<VideoGrabberPlatformData*>(platformHandle_);
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
