// All Window handles have the same ownership; an earlier Window can close first.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool okay) {
    std::printf("mainWindowAttach: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
std::shared_ptr<Window> earlier, later;
int earlierExits = 0, laterFrames = 0;
struct Later : App {
    Texture texture;
    Fbo fbo;
    bool uploaded = false;
    void setup() override { check("later size seeded", getWidth() == getWindowWidth()); }
    void draw() override {
        if (earlier && !earlier->isOpen()) {
            if (!uploaded) {
                Pixels pixels;
                pixels.allocate(16, 16, 4);
                texture.allocate(pixels);
                fbo.allocate(16, 16);
                uploaded = texture.isAllocated() && fbo.isAllocated();
                check("later uploads texture and Fbo after earlier close", uploaded);
            }
            fbo.begin(); clear(Color(0, 1, 0)); fbo.end();
            clear(Color(0, 0, 1));
            fbo.draw(0, 0);
            if (++laterFrames == 3) getWindow()->close();
        }
    }
    void cleanup() override {
        check("earlier close did not terminate later", earlierExits == 1 && laterFrames == 3 && uploaded);
        if (failures) internal::appExitCode() = 1;
    }
};
struct Earlier : App {
    void setup() override {
        earlier = internal::WindowRequestAccess::retainHostOwner(*getWindow());
        later = createWindow(WindowSettings{}.setSize(120, 90).setTitle("Later"));
        check("later created", bool(later));
        if (later) later->setApp<Later>();
        else exitApp(1);
    }
    void update() override { if (later && later->getApp()) getWindow()->close(); }
    void exit() override { ++earlierExits; check("earlier App detached before exit", !getWindow()); }
};
struct Empty : App {};
int processExits = 0, processCleanups = 0;
void processVerdict() {
    const bool okay = processExits == 1 && processCleanups == 1;
    std::printf("mainWindowAttach: process ending exactly once: %s\n", okay ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!okay) std::_Exit(1);
}
struct ProcessExitApp : App {
    void update() override { getWindow()->setApp<Empty>(); std::exit(0); }
    void exit() override { ++processExits; }
    void cleanup() override { ++processCleanups; }
};
struct ThrowConstructor : App {
    ThrowConstructor() { throw std::runtime_error("constructor test"); }
};
}
#include "c2ProcessChecks.h"
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--display")
        return runApp<Earlier>(WindowSettings{}.setSize(160, 120).setTitle("Earlier"));
    if (argc > 1 && std::string(argv[1]) == "--constructor-failure")
        return runApp<ThrowConstructor>(WindowSettings{}.setSize(128, 128));
    if (argc > 1 && (std::string(argv[1]) == "--exit-in-callback" ||
                     std::string(argv[1]) == "--process-exit")) {
        // Register before Runtime so its destructor runs before this verdict.
        std::atexit(processVerdict);
        if (std::string(argv[1]) == "--exit-in-callback")
            return runApp<ProcessExitApp>(WindowSettings{}.setSize(128, 128));
        getMainThreadId();
        internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
        auto window = internal::prepareLaunchWindow(WindowSettings{});
        internal::WindowRequestAccess::bindForTests(*window, 101);
        window->setApp<ProcessExitApp>(); internal::applyPendingAppForTests(*window);
        window->setApp<Empty>();
        std::exit(0);
    }
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto a = std::make_shared<Window>(), b = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*a, 401);
    internal::WindowRequestAccess::bindForTests(*b, 402);
    internal::retainRuntimeWindow(a); internal::retainRuntimeWindow(b);
    check("ambiguous omission resolves no Window", !internal::resolveWindow());
    a->setApp<Empty>(); b->setApp<Empty>();
    internal::applyPendingAppForTests(*a); internal::applyPendingAppForTests(*b);
    auto* rawA = a.get(); auto* rawB = b.get();
    std::weak_ptr<Window> weakA = a;
    a.reset();
    check("Runtime owns Window after external release", !weakA.expired() && rawA->isOpen());
    internal::WindowRequestAccess::teardown(*rawA);
    check("earlier closes while later remains open", rawB->isOpen() && internal::resolveWindow() == rawB);
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    check("shutdown retires all backings", !rawB->isOpen());
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    b->close();
    check("old handle stays closed across Runtime epochs", !b->isOpen());
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    c2::run();
    return failures ? 1 : 0;
}
