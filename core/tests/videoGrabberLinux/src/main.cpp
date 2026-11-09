// Headless checks for Linux UVC MJPEG decoding and device enumeration (#1369).
#include "../../common/tcCoreTest.h"
#include <cstdio>

#if defined(__linux__) && !defined(__ANDROID__)
#include <TrussC.h>
#include "../../common/tcDisplayTest.h"
#include "../../../platform/linux/tcVideoGrabberLinux.h"
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
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

void appendJpeg(void* context, void* data, int size) {
    auto& bytes = *static_cast<std::vector<unsigned char>*>(context);
    const auto* first = static_cast<unsigned char*>(data);
    bytes.insert(bytes.end(), first, first + size);
}

void jpegChecks() {
    constexpr int width = 32, height = 32;
    std::array<unsigned char, width * height * 3> rgb{};
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const int offset = (y * width + x) * 3;
            rgb[offset] = 32 + x * 5;
            rgb[offset + 1] = 40 + y * 4;
            rgb[offset + 2] = 160 - x * 3;
        }
    }
    std::vector<unsigned char> jpeg;
    check("encode baseline JPEG in memory", stbi_write_jpg_to_func(appendJpeg, &jpeg,
        width, height, 3, rgb.data(), 95) != 0);
    if (jpeg.empty()) return;

    // Strip complete DHT segments, preserving the encoded scan and other headers.
    std::vector<unsigned char> withoutDht(jpeg.begin(), jpeg.begin() + 2);
    std::vector<unsigned char> encodedDht;
    size_t pos = 2;
    while (pos + 4 <= jpeg.size()) {
        const auto marker = jpeg[pos + 1];
        if (marker == 0xda) break;
        const size_t segmentSize = 2 + (size_t(jpeg[pos + 2]) << 8) + jpeg[pos + 3];
        if (segmentSize > jpeg.size() - pos) { check("encoder segment length", false); return; }
        auto& target = marker == 0xc4 ? encodedDht : withoutDht;
        target.insert(target.end(), jpeg.begin() + pos, jpeg.begin() + pos + segmentSize);
        pos += segmentSize;
    }
    const size_t scanOffset = withoutDht.size();
    withoutDht.insert(withoutDht.end(), jpeg.begin() + pos, jpeg.end());
    check("default tables match the encoder's standard DHT", encodedDht.size() == sizeof(mjpegDefaultDht) &&
        std::equal(encodedDht.begin(), encodedDht.end(), mjpegDefaultDht));

    const unsigned char app[] = {0xff, 0xe0, 0, 6, 0xff, 0xc4, 0xff, 0xda};
    const size_t maxFrameSize = withoutDht.size() + sizeof(app) + 32;
    MjpegDhtBuffer scratch;
    check("allocate scratch from driver buffer length", scratch.allocate(maxFrameSize));
    auto original = scratch.prepare(jpeg);
    check("JPEG with DHT passes through by address and size",
        original.data() == jpeg.data() && original.size() == jpeg.size());
    auto repaired = scratch.prepare(withoutDht);
    check("missing DHT inserts exactly 420 bytes after SOI",
        repaired.data() != withoutDht.data() && repaired.size() == withoutDht.size() + 420 &&
        repaired[0] == 0xff && repaired[1] == 0xd8 &&
        std::equal(encodedDht.begin(), encodedDht.end(), repaired.begin() + 2) &&
        std::equal(withoutDht.begin() + 2, withoutDht.end(), repaired.begin() + 422));
    int decodedWidth = 0, decodedHeight = 0, channels = 0;
    auto* decoded = stbi_load_from_memory(repaired.data(), static_cast<int>(repaired.size()),
        &decodedWidth, &decodedHeight, &channels, 4);
    check("repaired JPEG decodes to expected dimensions", decoded && decodedWidth == width && decodedHeight == height);
    if (decoded && decodedWidth == width && decodedHeight == height) {
        bool colorsMatch = true;
        for (int i = 0; i < width * height; ++i) {
            for (int channel = 0; channel < 3; ++channel)
                colorsMatch &= std::abs(int(decoded[i * 4 + channel]) - int(rgb[i * 3 + channel])) <= 8;
            colorsMatch &= decoded[i * 4 + 3] == 255;
        }
        check("known gradient colors and alpha within JPEG tolerance", colorsMatch);
    }
    stbi_image_free(decoded);
    const auto* storage = repaired.data();
    bool reused = true;
    for (int i = 0; i < 10; ++i) reused &= scratch.prepare(withoutDht).data() == storage;
    check("successive frames reuse scratch storage", reused);
    check("DHT frames still pass through after scratch allocation", scratch.prepare(jpeg).data() == jpeg.data());
    MjpegDhtBuffer otherGrabber;
    check("allocate separate grabber scratch", otherGrabber.allocate(maxFrameSize));
    check("each grabber owns separate storage", otherGrabber.prepare(withoutDht).data() != storage);

    // Markers in an APP payload must not be mistaken for header markers.
    auto withApp = withoutDht;
    withApp.insert(withApp.begin() + 2, std::begin(app), std::end(app));
    check("embedded DHT/SOS bytes in payload are skipped", scratch.prepare(withApp).size() == withApp.size() + 420);
    bool increasingReuses = true;
    auto growingFrame = withApp;
    for (; growingFrame.size() <= maxFrameSize; growingFrame.push_back(0)) {
        const auto result = scratch.prepare(growingFrame);
        increasingReuses &= result.data() == storage && result.size() == growingFrame.size() + sizeof(mjpegDefaultDht);
    }
    check("increasing frames through capacity reuse setup allocation", increasingReuses);
    const auto oversized = scratch.prepare(growingFrame);
    check("frame exceeding capacity passes through by address and size",
        oversized.data() == growingFrame.data() && oversized.size() == growingFrame.size());
    check("smaller frame still uses setup allocation", scratch.prepare(withoutDht).data() == storage);
    MjpegDhtBuffer unallocated;
    const auto unchanged = unallocated.prepare(withoutDht);
    check("prepare without scratch passes through without allocating",
        unchanged.data() == withoutDht.data() && unchanged.size() == withoutDht.size());
    check("scratch size addition overflow rejected", !unallocated.allocate((std::numeric_limits<size_t>::max)()));

    bool truncatedPasses = true;
    const size_t scanHeaderEnd = scanOffset + 2 + (size_t(withoutDht[scanOffset + 2]) << 8) + withoutDht[scanOffset + 3];
    for (size_t count = 0; count < scanHeaderEnd; ++count) {
        std::span<const unsigned char> prefix(withoutDht.data(), count);
        const auto result = scratch.prepare(prefix);
        truncatedPasses &= result.data() == prefix.data() && result.size() == count;
    }
    check("all truncated header prefixes pass through", truncatedPasses);
    for (const std::vector<unsigned char>& invalid : {
        std::vector<unsigned char>{0, 0, 0xff, 0xda, 0, 2},
        {0xff, 0xd8, 0xff, 0xe0, 0, 1}, // Invalid segment length.
        {0xff, 0xd8, 0xff, 0xe0, 0xff, 0xff}, // Length exceeds bytesused.
        {0xff, 0xd8, 0xff, 0xda, 0, 1}, // Invalid SOS length.
        {0xff, 0xd8, 0xff, 0xc4, 0xff, 0xff}, // Invalid DHT length.
        {0xff, 0xd8, 0xff, 0xc4, 0, 2, 0xff, 0xda, 0, 9},
        {0xff, 0xd8, 0xff, 0xd9}, // No scan.
        {0xff, 0xd8, 0xff, 0x00}, // Stuffed byte outside scan.
        {0xff, 0xd8, 0xff, 0xd0}, // Restart outside scan.
        {0xff, 0xd8, 0x11, 0xff, 0xda, 0, 2}}) {
        const auto result = scratch.prepare(invalid);
        check("malformed header passes through unchanged", result.data() == invalid.data() && result.size() == invalid.size());
    }
    auto fillAndTem = withoutDht;
    fillAndTem.insert(fillAndTem.begin() + 2, {0xff, 0x01, 0xff});
    check("marker fill and standalone TEM accepted", scratch.prepare(fillAndTem).size() == fillAndTem.size() + 420);
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
    jpegChecks();
    capabilityChecks();
    deviceNameChecks();
    return failures ? 1 : 0;
#else
    (void)argc; (void)argv;
    std::puts("SKIP: Linux V4L2 helpers");
    return 77;
#endif
}
