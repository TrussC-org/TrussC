// Linux device enumeration and opt-in UVC checks (#1369).
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
                std::to_string(sizes[sizeIndex].second) + ".png";
            check("save captured PNG", bool(pixels.save(deviceOutput / name)));
            std::printf("Saved %s; inspect scene, colors and orientation manually.\n", name.c_str());
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
    const std::array<std::pair<int, int>, 3> sizes{{{640, 480}, {1280, 720}, {1920, 1080}}};
    size_t sizeIndex = 0;
    int frameCount = 0;
    bool finished = false;
    std::chrono::steady_clock::time_point started;
    void startSize() {
        grabber.setDeviceID(deviceNumber);
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
            check("all three camera sizes saved", deviceCompleted);
        });
    }
    capabilityChecks();
    deviceNameChecks();
    return failures ? 1 : 0;
#else
    (void)argc; (void)argv;
    std::puts("SKIP: Linux V4L2 helpers");
    return 77;
#endif
}
