// Headless checks for Linux capture format selection and device enumeration (#1369, #1384).
// Native DHT-less decoding is covered by jpegDecode.
#include "../../common/tcCoreTest.h"
#include <cstdio>

#if defined(__linux__) && !defined(__ANDROID__)
#include <TrussC.h>
#include "../../common/tcDisplayTest.h"
#include "../../../platform/linux/tcVideoGrabberLinux.h"
#include <array>
#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {
using namespace trussc::internal;
int failures = 0;
void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}

v4l2_frmivalenum discreteInterval(uint32_t numerator, uint32_t denominator) {
    v4l2_frmivalenum interval{};
    interval.type = V4L2_FRMIVAL_TYPE_DISCRETE;
    interval.discrete = {numerator, denominator};
    return interval;
}

std::vector<LinuxCaptureMode> c525Modes() {
    std::vector<LinuxCaptureMode> modes;
    struct SizeRate { uint32_t width, height, numerator, denominator; };
    for (const auto& size : std::vector<SizeRate>{
        {640, 480, 1, 30}, {800, 448, 1, 30},
        {800, 600, 1, 24}, {864, 480, 1, 24}, {960, 544, 1, 20},
        {960, 720, 1, 15}, {1024, 576, 1, 15}, {1184, 656, 1, 15},
        {1280, 720, 1, 10}, {1392, 768, 1, 10}, {1280, 960, 2, 15},
        {1504, 832, 2, 15}, {1600, 896, 2, 15}, {1712, 960, 1, 5},
        {1792, 1008, 1, 5}, {1920, 1080, 1, 5}}) {
        modes.push_back({V4L2_PIX_FMT_MJPEG, size.width, size.height, {discreteInterval(1, 30)}});
        modes.push_back({V4L2_PIX_FMT_YUYV, size.width, size.height,
            {discreteInterval(1, 5), discreteInterval(size.numerator, size.denominator)}});
    }
    return modes;
}

void selectionChecks() {
    const auto modes = c525Modes();
    struct Request { uint32_t width, height; int fps; uint32_t expected; };
    for (const auto& request : std::vector<Request>{
        {640, 480, 0, V4L2_PIX_FMT_YUYV}, {640, 480, 30, V4L2_PIX_FMT_YUYV},
        {1280, 720, 0, V4L2_PIX_FMT_MJPEG}, {1280, 720, 10, V4L2_PIX_FMT_YUYV},
        {1920, 1080, 0, V4L2_PIX_FMT_MJPEG}, {800, 600, 24, V4L2_PIX_FMT_YUYV},
        {800, 600, 30, V4L2_PIX_FMT_MJPEG}, {1280, 960, 7, V4L2_PIX_FMT_YUYV},
        {1280, 960, 8, V4L2_PIX_FMT_MJPEG}}) {
        check("C525 size/fps selects expected format", selectLinuxCaptureFormat(modes,
            request.width, request.height, request.fps) == request.expected);
    }
    std::vector<LinuxCaptureMode> single{{V4L2_PIX_FMT_YUYV, 640, 480, {discreteInterval(1, 30)}}};
    check("YUYV-only camera", selectLinuxCaptureFormat(single, 640, 480, 0) == V4L2_PIX_FMT_YUYV);
    check("YUYV-only camera with fps", selectLinuxCaptureFormat(single, 640, 480, 30) == V4L2_PIX_FMT_YUYV);
    single[0].fourcc = V4L2_PIX_FMT_MJPEG;
    check("MJPG-only camera", selectLinuxCaptureFormat(single, 640, 480, 0) == V4L2_PIX_FMT_MJPEG);
    single.push_back({V4L2_PIX_FMT_YUYV, 320, 240, {discreteInterval(1, 60)}});
    check("size absent from YUYV", selectLinuxCaptureFormat(single, 640, 480, 30) == V4L2_PIX_FMT_MJPEG);
    single[1].width = 640;
    single[1].height = 480;
    v4l2_frmivalenum range{};
    range.type = V4L2_FRMIVAL_TYPE_STEPWISE;
    range.stepwise = {{1, 60}, {1, 5}, {1, 60}};
    single[1].intervals = {range};
    check("stepwise fastest interval wins", selectLinuxCaptureFormat(single, 640, 480, 60) == V4L2_PIX_FMT_YUYV);
    check("stepwise fps above maximum rejected", selectLinuxCaptureFormat(single, 640, 480, 61) == V4L2_PIX_FMT_MJPEG);
    single[1].intervals[0].type = V4L2_FRMIVAL_TYPE_CONTINUOUS;
    check("continuous interval", selectLinuxCaptureFormat(single, 640, 480, 0) == V4L2_PIX_FMT_YUYV);
    single[1].intervals.clear();
    check("unknown YUYV intervals preserve MJPG first", selectLinuxCaptureFormat(single, 640, 480, 0) == V4L2_PIX_FMT_MJPEG);
    check("no enumerated formats preserve existing behavior", selectLinuxCaptureFormat({}, 640, 480, 0) == V4L2_PIX_FMT_MJPEG);
}

void enumerationChecks() {
    for (auto sizeType : {V4L2_FRMSIZE_TYPE_DISCRETE, V4L2_FRMSIZE_TYPE_STEPWISE, V4L2_FRMSIZE_TYPE_CONTINUOUS}) {
        auto call = [sizeType](unsigned long request, void* arg) {
            if (request == VIDIOC_ENUM_FMT) {
                auto& format = *static_cast<v4l2_fmtdesc*>(arg);
                if (format.index >= 3) return -1;
                format.pixelformat = format.index == 0 ? V4L2_PIX_FMT_RGB24
                    : format.index == 1 ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV;
            } else if (request == VIDIOC_ENUM_FRAMESIZES) {
                auto& size = *static_cast<v4l2_frmsizeenum*>(arg);
                if (size.index != 0) return -1;
                size.type = sizeType;
                if (sizeType == V4L2_FRMSIZE_TYPE_DISCRETE) size.discrete = {640, 480};
                else size.stepwise = {320, 1280, 16, 240, 960, 8};
            } else if (request == VIDIOC_ENUM_FRAMEINTERVALS) {
                auto& interval = *static_cast<v4l2_frmivalenum*>(arg);
                if (interval.index >= 2) return -1;
                if (interval.pixel_format == V4L2_PIX_FMT_YUYV) {
                    interval.type = V4L2_FRMIVAL_TYPE_STEPWISE;
                    interval.stepwise = {{1, 30}, {1, 5}, {1, 30}};
                } else {
                    interval.type = V4L2_FRMIVAL_TYPE_DISCRETE;
                    interval.discrete = {1, interval.index == 0 ? 5u : 30u};
                }
            } else return -1;
            return 0;
        };
        const auto modes = enumerateLinuxCaptureModes(call, 640, 480);
        check("enumerate supported exact size and skip other formats", modes.size() == 2);
        check("enumerated discrete and stepwise intervals select YUYV",
            selectLinuxCaptureFormat(modes, 640, 480, 30) == V4L2_PIX_FMT_YUYV);
        const auto offset = enumerateLinuxCaptureModes(call, 641, 480);
        check("size range honors steps or continuous sizes",
            offset.size() == (sizeType == V4L2_FRMSIZE_TYPE_CONTINUOUS ? 2u : 0u));
        check("out of range size absent", enumerateLinuxCaptureModes(call, 1920, 1080).empty());
    }
}

struct MockCaptureDriver {
    v4l2_format actual{};
    v4l2_fract interval{};
    std::vector<uint32_t> attempts;
    bool lowerYuyvFps = false;
    bool resizeYuyv = false;
    bool substituteYuyv = false;
    bool rejectMjpg = false;
    bool rejectRate = false;
    int operator()(unsigned long request, void* arg) {
        if (request == VIDIOC_S_FMT) {
            auto& format = *static_cast<v4l2_format*>(arg);
            attempts.push_back(format.fmt.pix.pixelformat);
            if (rejectMjpg && format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG) return -1;
            if (format.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV) {
                if (resizeYuyv) format.fmt.pix.width = 320;
                if (substituteYuyv) format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
            }
            actual = format;
            interval = {1, 5};
        } else if (request == VIDIOC_G_FMT) {
            *static_cast<v4l2_format*>(arg) = actual;
        } else if (request == VIDIOC_S_PARM) {
            if (rejectRate) return -1;
            auto& parm = *static_cast<v4l2_streamparm*>(arg);
            interval = lowerYuyvFps && actual.fmt.pix.pixelformat == V4L2_PIX_FMT_YUYV
                ? v4l2_fract{1, 15} : parm.parm.capture.timeperframe;
            parm.parm.capture.timeperframe = interval;
        } else if (request == VIDIOC_G_PARM) {
            static_cast<v4l2_streamparm*>(arg)->parm.capture.timeperframe = interval;
        } else return -1;
        return 0;
    }
};

void negotiationChecks() {
    const auto modes = c525Modes();
    for (int fps : {0, 30}) {
        for (int behavior = 0; behavior < 5; ++behavior) {
            MockCaptureDriver driver;
            driver.lowerYuyvFps = behavior == 1;
            driver.resizeYuyv = behavior == 2;
            driver.substituteYuyv = behavior == 3;
            driver.rejectRate = behavior == 4;
            LinuxCaptureNegotiation result;
            check("mock negotiation succeeds", negotiateLinuxCapture(driver, modes, 640, 480, fps, result));
            const bool fallback = behavior != 0;
            check("mock format attempts", driver.attempts == (fallback
                ? std::vector<uint32_t>{V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG}
                : std::vector<uint32_t>{V4L2_PIX_FMT_YUYV}));
            check("negotiated dimensions restored for MJPG", result.format.fmt.pix.width == 640 && result.format.fmt.pix.height == 480);
            check("negotiated format used", result.format.fmt.pix.pixelformat == (fallback ? V4L2_PIX_FMT_MJPEG : V4L2_PIX_FMT_YUYV));
            check("negotiated fps read back", result.fps == (driver.rejectRate ? 5 : 30));
            check("fallback log explains negotiation", !fallback || std::string_view(result.reason).find("falling back") != std::string_view::npos);
        }
    }
    MockCaptureDriver driver;
    driver.rejectMjpg = true;
    LinuxCaptureNegotiation result;
    check("old MJPG S_FMT failure still tries YUYV", negotiateLinuxCapture(driver, {}, 640, 480, 0, result) &&
        driver.attempts == std::vector<uint32_t>{V4L2_PIX_FMT_MJPEG, V4L2_PIX_FMT_YUYV});
    const std::vector<LinuxCaptureMode> slowerMjpg{
        {V4L2_PIX_FMT_YUYV, 640, 480, {discreteInterval(1, 30)}},
        {V4L2_PIX_FMT_MJPEG, 640, 480, {discreteInterval(1, 15)}}};
    for (bool wrongSize : {true, false}) {
        driver = {};
        driver.resizeYuyv = wrongSize;
        driver.rejectRate = !wrongSize;
        check(wrongSize ? "wrong YUYV size falls back to slower MJPG"
                        : "rejected YUYV rate falls back to slower MJPG",
            negotiateLinuxCapture(driver, slowerMjpg, 640, 480, 30, result) &&
            driver.attempts == std::vector<uint32_t>{V4L2_PIX_FMT_YUYV, V4L2_PIX_FMT_MJPEG} &&
            result.format.fmt.pix.pixelformat == V4L2_PIX_FMT_MJPEG &&
            result.format.fmt.pix.width == 640 && result.format.fmt.pix.height == 480);
    }
}

void capabilityChecks() {
    v4l2_capability caps{};
    caps.capabilities = V4L2_CAP_VIDEO_CAPTURE;
    check("legacy capture device accepted", isVideoCaptureDevice(caps));
    caps.capabilities = V4L2_CAP_META_CAPTURE;
    caps.device_caps = V4L2_CAP_VIDEO_CAPTURE;
    check("device_caps ignored without DEVICE_CAPS", !isVideoCaptureDevice(caps));
    caps.capabilities = V4L2_CAP_DEVICE_CAPS | V4L2_CAP_VIDEO_CAPTURE | V4L2_CAP_META_CAPTURE;
    caps.device_caps = V4L2_CAP_META_CAPTURE;
    check("metadata node rejected despite whole-device capture capability", !isVideoCaptureDevice(caps));
    caps.device_caps = V4L2_CAP_VIDEO_CAPTURE;
    check("per-node capture accepted", isVideoCaptureDevice(caps));
    caps.capabilities = V4L2_CAP_DEVICE_CAPS;
    check("per-node capture authoritative", isVideoCaptureDevice(caps));
    caps.device_caps = 0;
    check("node without capture rejected", !isVideoCaptureDevice(caps));
}

void deviceNameChecks() {
    std::vector<std::pair<int, std::string>> nodes;
    for (const auto* name : {"video19", "video2", "video10", "video0", "video35", "video1", "video2147483647"}) {
        const int number = videoDeviceNumber(name);
        check("numeric video suffix parsed", number >= 0);
        nodes.emplace_back(number, name);
    }
    std::sort(nodes.begin(), nodes.end());
    const std::vector<int> expected{0, 1, 2, 10, 19, 35, 2147483647};
    bool matches = nodes.size() == expected.size();
    for (size_t i = 0; i < nodes.size(); ++i) matches &= nodes[i].first == expected[i];
    check("nodes sorted numerically, IDs remain suffixes including above nine", matches);
    for (const auto* name : {"video00", "video01", "video0010"})
        check("multi-digit leading zero suffix rejected", videoDeviceNumber(name) == -1);
    for (const auto* name : {"video", "video-1", "video+2", "video 2", "video1x", "video2.bak", "Video2",
        "v4l", "/dev/video1", "video2147483648", "video9999999999999999999999999"})
        check("non-node or unrepresentable suffix rejected", videoDeviceNumber(name) == -1);
}
// Opt-in camera/display check; normal test runs stay headless.
std::filesystem::path deviceOutput;
int deviceNumber = 0;
bool deviceCompleted = false;
class DeviceCheck : public tc::App {
public:
    void setup() override {
        for (const auto& device : grabber.listDevices())
            std::printf("deviceId=%d name=%s uniqueId=%s\n", device.deviceId,
                device.deviceName.c_str(), device.uniqueId.c_str());
        tc::fs::create_directories(deviceOutput);
        startSize();
    }
    void update() override {
        if (finished) return;
        grabber.update();
        std::vector<tc::GrabberFrame> frames;
        grabber.getQueuedFrames(frames);
        frameCount += static_cast<int>(frames.size());
        if (!frames.empty() && frameCount >= 30) {
            const auto& pixels = frames.back().pixels;
            check("camera negotiated requested dimensions", pixels.getWidth() == sizes[sizeIndex].first &&
                pixels.getHeight() == sizes[sizeIndex].second);
            const auto name = "c525-" + std::to_string(sizes[sizeIndex].first) + "x" +
                std::to_string(sizes[sizeIndex].second) + "-fps" + std::to_string(frameRates[sizeIndex]) + ".png";
            check("save captured PNG", bool(pixels.save(deviceOutput / name)));
            const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();
            std::printf("Saved %s; received %d queued frames in %.2f s (%.2f fps including startup); inspect live colors and orientation.\n",
                name.c_str(), frameCount, elapsed, frameCount / elapsed);
            grabber.close();
            if (++sizeIndex == sizes.size()) {
                deviceCompleted = true;
                finish();
            } else startSize();
        } else if (std::chrono::steady_clock::now() - started > std::chrono::seconds(30)) {
            check("camera delivered frames before safety timeout", false);
            finish();
        }
    }
    void draw() override { if (!finished) grabber.draw(0, 0, 640, 480); }
    void cleanup() override { grabber.close(); }
private:
    tc::VideoGrabber grabber;
    const std::array<std::pair<int, int>, 4> sizes{{{640, 480}, {1280, 720}, {1280, 720}, {1920, 1080}}};
    const std::array<int, 4> frameRates{0, 0, 10, 0};
    size_t sizeIndex = 0;
    int frameCount = 0;
    bool finished = false;
    std::chrono::steady_clock::time_point started;
    void startSize() {
        grabber.setDeviceID(deviceNumber);
        grabber.setDesiredFrameRate(frameRates[sizeIndex]);
        grabber.setFrameQueueSize(2);
        frameCount = 0;
        started = std::chrono::steady_clock::now();
        if (!grabber.setup(sizes[sizeIndex].first, sizes[sizeIndex].second)) {
            check("open camera", false);
            finish();
        }
    }
    void finish() { finished = true; tc::exitApp(); }
};
} // namespace
#endif

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (argc >= 3 && std::string_view(argv[1]) == "--device-check") {
        deviceOutput = tc::fs::absolute(argv[2]);
        if (argc >= 4) deviceNumber = std::atoi(argv[3]);
        tc::WindowSettings settings;
        settings.setSize(640, 480);
        return tcCoreTest::runDisplayTest<DeviceCheck>(settings, failures, [] {
            check("all camera requests saved", deviceCompleted);
        });
    }
    selectionChecks();
    enumerationChecks();
    negotiationChecks();
    capabilityChecks();
    deviceNameChecks();
    return failures ? 1 : 0;
#else
    (void)argc; (void)argv;
    std::puts("SKIP: Linux V4L2 helpers");
    return 77;
#endif
}
