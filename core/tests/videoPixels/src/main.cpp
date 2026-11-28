#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include "../../common/tcVideoGrabberTestAccess.h"
#include <cstdio>
#include <type_traits>

using namespace tc;

namespace {
static_assert(std::is_same_v<decltype(std::declval<VideoPlayer&>().getPixels()), const Pixels&>);
static_assert(std::is_same_v<decltype(std::declval<const VideoPlayer&>().getPixels()), const Pixels&>);
static_assert(std::is_same_v<decltype(std::declval<VideoPlayerBase&>().getPixels()), const Pixels&>);
static_assert(std::is_same_v<decltype(std::declval<VideoGrabber&>().getPixels()), const Pixels&>);
static_assert(std::is_same_v<decltype(std::declval<const VideoGrabber&>().getPixels()), const Pixels&>);

int failures = 0;
void check(const char* label, bool passed) {
    std::printf("%s: %s\n", label, passed ? "PASS" : "FAIL");
    if (!passed) ++failures;
}

#if defined(__linux__) && !defined(__ANDROID__)
// The existing synthetic NV12 draw seam invokes this override while NV12 is active.
// The display check supplies allocated planes; the headless check supplies empty ones.
class NV12Probe : public VideoPlayer {
public:
    enum class FirstAccess { Pixels, MutableTexture, ConstTexture };
    FirstAccess firstAccess = FirstAccess::Pixels;
    bool planesAllocated = false;
    int* warnings = nullptr;
    void draw(float, float, float, float) const override {
        const int before = *warnings;
        check("NV12 hasTexture checks both planes", hasTexture() == planesAllocated);
        check("NV12 hasTexture does not warn", *warnings == before);
        auto& mutableSelf = const_cast<NV12Probe&>(*this);
        switch (firstAccess) {
        case FirstAccess::Pixels:
            check("pixels first remains empty", !getPixels().isAllocated());
            check("pixels first warns immediately", *warnings == 1);
            break;
        case FirstAccess::MutableTexture:
            check("non-const texture first remains empty", !mutableSelf.getTexture().isAllocated());
            check("non-const texture first warns immediately", *warnings == 1);
            break;
        case FirstAccess::ConstTexture:
            check("const texture first remains empty", !getTexture().isAllocated());
            check("const texture first warns immediately", *warnings == 1);
            break;
        }
        check("protected NV12 accessors", isNV12Mode() &&
              getTextureY().isAllocated() == planesAllocated &&
              getTextureUV().isAllocated() == planesAllocated);
        check("NV12 pixels remain empty", !getPixels().isAllocated());
        check("NV12 texture remains empty", !getTexture().isAllocated());
        check("non-const NV12 texture remains empty", !mutableSelf.getTexture().isAllocated());
        check("repeated NV12 pixels remain empty", !getPixels().isAllocated());
        check("other accessors do not warn again", *warnings == 1);
    }
};

void checkNV12(bool allocatePlanes) {
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.level == LogLevel::Warning && event.message.find("getPixels()/getTexture() is empty") != std::string::npos) {
            ++warnings;
            check("NV12 warning gives Fbo path", event.message.find("fbo.readPixels()") != std::string::npos);
        }
    });
    auto probe = [&](NV12Probe& video) {
        Texture yPlane, uvPlane;
        if (allocatePlanes) {
            Pixels pixels;
            pixels.allocate(2, 2, 4);
            yPlane.allocate(pixels);
            uvPlane.allocate(pixels);
            check("allocate both planes", yPlane.isAllocated() && uvPlane.isAllocated());
        }
        video.planesAllocated = allocatePlanes;
        video.warnings = &warnings;
        internal::VideoPlayerPlatformAccess::drawNV12ForTests(video, nullptr,
            std::move(yPlane), std::move(uvPlane), 0, 0, 1, 1);
    };
    for (auto access : {NV12Probe::FirstAccess::Pixels,
                        NV12Probe::FirstAccess::MutableTexture,
                        NV12Probe::FirstAccess::ConstTexture}) {
        warnings = 0;
        NV12Probe video;
        video.firstAccess = access;
        probe(video);
        check("first NV12 access warns once", warnings == 1);
        probe(video);
        check("second NV12 call does not warn", warnings == 1);
    }
}

class NV12CheckApp : public App {
public:
    void draw() override {
        checkNV12(true);
        exitApp();
    }
};
#endif

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (argc > 1 && std::strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.width = 64;
        settings.height = 64;
        return tcCoreTest::runDisplayTest<NV12CheckApp>(settings, failures);
    }
#endif
    VideoPlayer player;
    check("player pixels empty before load", !player.getPixels().isAllocated());
    player.close();
    check("player pixels empty after close", !player.getPixels().isAllocated());
    VideoGrabber grabber;
    check("grabber pixels empty before setup", !grabber.getPixels().isAllocated());
    internal::VideoGrabberTestAccess::prepare(grabber);
    check("grabber RGBA storage has dimensions", grabber.getPixels().getWidth() == 1 &&
          grabber.getPixels().getHeight() == 1 && grabber.getPixels().getChannels() == 4);
    grabber.close();
    check("grabber pixels empty after close", !grabber.getPixels().isAllocated());
#if defined(__linux__) && !defined(__ANDROID__)
    checkNV12(false);
#endif
    return failures ? 1 : 0;
}
