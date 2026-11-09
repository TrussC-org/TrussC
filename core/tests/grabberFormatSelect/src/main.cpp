#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcVideoGrabberTestAccess.h"
#include <cstdio>

namespace {
using namespace tc;
using internal::GrabberFormat;
using internal::selectGrabberFormat;
using Access = internal::VideoGrabberTestAccess;
int failures = 0;
void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    failures += !ok;
}
} // namespace

TC_CORE_TEST_MAIN() {
    std::vector<GrabberFormat> modes{
        {1920, 1080, {{30, 30}}},
        {1280, 720, {{60, 60}}},
        {1280, 720, {{30, 30}}}
    };
    check("closest size, unspecified fps prefers the higher rate", selectGrabberFormat(modes, 640, 480, -1) == 1);
    check("desired 60 fps breaks size tie", selectGrabberFormat(modes, 640, 480, 60) == 1);
    check("desired 30 fps breaks size tie", selectGrabberFormat(modes, 640, 480, 30) == 2);
    check("size wins over desired fps", selectGrabberFormat(modes, 1920, 1080, 60) == 0);
    check("exact size and fps", selectGrabberFormat(modes, 1280, 720, 30) == 2);
    check("unsupported fps keeps first size tie", selectGrabberFormat(modes, 640, 480, 120) == 1);
    check("empty list fails", selectGrabberFormat({}, 640, 480, 30) == -1);
    modes = {{640, 480, {{15, 29.97}, {60.00024, 60.00024}}}, {640, 480, {{90, 90}}}};
    check("nominal 30 fps uses rounded endpoint", selectGrabberFormat(modes, 640, 480, 30) == 0);
    check("nominal 60 fps uses rounded endpoint", selectGrabberFormat(modes, 640, 480, 60) == 0);
    check("all ranges participate", selectGrabberFormat(modes, 640, 480, 90) == 1);
    modes = {{600, 480, {}}, {640, 440, {{30, 60}}}};
    check("equal distance in different dimensions uses fps", selectGrabberFormat(modes, 640, 480, 60) == 1);
    check("equal distance without fps prefers the higher max fps", selectGrabberFormat(modes, 640, 480, 0) == 1);
    modes = {{1920, 1080, {{5, 5}}}, {1920, 1080, {{30, 30}}}};
    check("same size without fps picks 30 over 5", selectGrabberFormat(modes, 1920, 1080, 0) == 1);
    check("same size, requested 5 fps still picks 5", selectGrabberFormat(modes, 1920, 1080, 5) == 0);
    modes = {{1280, 720, {{30, 30}}}, {1280, 720, {{30, 30}}}};
    check("equal size and max fps keeps enumeration order", selectGrabberFormat(modes, 1280, 720, -1) == 0);

    modes = {{540, 380, {}}, {480, 480, {}}};
    check("size distance is Euclidean", selectGrabberFormat(modes, 640, 480, -1) == 0);

    VideoGrabber camera;
    Access::setReopenName(camera, "old camera");
    camera.setDeviceID(7);
    camera.setDeviceUniqueID("port-camera");
    check("unique ID setter wins and forgets remembered name",
          Access::usesUniqueId(camera) && Access::uniqueId(camera) == "port-camera" && Access::reopenName(camera).empty());
    VideoGrabber moved(std::move(camera));
    check("move preserves unique ID selection", Access::usesUniqueId(moved) && Access::uniqueId(moved) == "port-camera");
    camera = std::move(moved);
    check("move assignment preserves unique ID selection", Access::usesUniqueId(camera) && Access::uniqueId(camera) == "port-camera");
    camera.setDeviceID(9);
    check("index setter wins", !Access::usesUniqueId(camera) && camera.getDeviceID() == 9);
    camera.setDeviceUniqueID("");
    check("empty unique ID remains an explicit selection", Access::usesUniqueId(camera) && Access::uniqueId(camera).empty());
    if (VideoGrabber::checkCameraPermission()) {
        int errorCount = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
            if (event.level == LogLevel::Error && event.message.find("No camera with unique ID") != std::string::npos) ++errorCount;
        });
        camera.setDeviceUniqueID("trussc-test-missing-camera-id-276");
        check("missing ID setup fails with an error", !camera.setup(640, 480) && !camera.isInitialized() && errorCount == 1);
        check("retry keeps explicit ID and fails with an error", !camera.setup() && errorCount == 2);
    }
    return failures ? 1 : 0;
}
