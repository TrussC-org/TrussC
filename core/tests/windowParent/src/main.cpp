// WindowSettings::setParentWindow: the value, the weak hold, and what happens
// when the parent is closed or gone at creation.
//
// Default mode is headless (bound test windows). `--display` opens real windows
// (daily Linux run under Xvfb) and, on X11, reads WM_TRANSIENT_FOR back from the
// server. The macOS child-window and Windows owner paths need a device run.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <cstdio>
#include <string>
using namespace tc;

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
// Xlib's headers define macros (None, Bool, Status) that clash with TrussC, so
// the one call this test needs is declared by hand.
extern "C" int XGetTransientForHint(const void* display, unsigned long window, unsigned long* prop_return);
#define PARENT_TEST_X11 1
#endif

namespace {
int failures = 0;
void check(const char* label, bool okay) {
    std::printf("windowParent: %s: %s\n", label, okay ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!okay) ++failures;
}

// Counts the "parent window" warnings logged while it is alive.
struct WarningCounter {
    int count = 0;
    EventListener listener = getLogger().onLog.listen([this](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.find("parent window") != std::string::npos) ++count;
    });
};

int runHeadlessChecks() {
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto parent = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*parent, 101);
    internal::retainRuntimeWindow(parent);

    // The value round-trips, and both forms chain like the other setters.
    WindowSettings settings;
    check("no parent by default", settings.parentWindow.expired() && internal::resolveParentWindow(settings).id == 0);
    WindowSettings& chained = settings.setSize(100, 80).setParentWindow(parent).setTitle("palette");
    check("setParentWindow chains and keeps the other settings",
          &chained == &settings && settings.width == 100 && settings.title == "palette");
    check("settings hold the parent given as shared_ptr", settings.parentWindow.lock() == parent);
    WindowSettings byReference;
    byReference.setParentWindow(*parent);
    check("settings hold the parent given by reference", byReference.parentWindow.lock() == parent);
    WindowSettings copy = settings;
    check("a copy of the settings keeps the parent", copy.parentWindow.lock() == parent);

    // The settings never extend the parent's life.
    const long useCount = parent.use_count();
    WindowSettings more = settings;
    more.setParentWindow(parent);
    check("settings do not add an owner", parent.use_count() == useCount);

    {
        WarningCounter warnings;
        check("an open parent resolves to its native handle", internal::resolveParentWindow(settings).id == 101);
        check("an open parent logs no warning", warnings.count == 0);
        settings.setParentWindow(nullptr);
        check("an empty pointer clears the parent", settings.parentWindow.expired() && internal::resolveParentWindow(settings).id == 0);
        check("no parent logs no warning", warnings.count == 0);
    }

    // A closed parent that is still held, then one that is gone.
    WarningCounter warnings;
    internal::WindowRequestAccess::teardown(*parent);
    check("closed parent: no handle", internal::resolveParentWindow(copy).id == 0);
    check("closed parent: warns once", warnings.count == 1);
    std::weak_ptr<Window> gone = parent;
    parent.reset();
    check("settings did not keep the closed parent alive", gone.expired());
    check("gone parent: settings see it expired", copy.parentWindow.expired() && more.parentWindow.expired());
    check("gone parent: no handle", internal::resolveParentWindow(copy).id == 0);
    check("gone parent: warns", warnings.count == 2);

    warnings.listener.disconnect();
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    return failures ? 1 : 0;
}

// --- display mode -----------------------------------------------------------
std::shared_ptr<Window> child, closing, orphanParent, orphan, afterClosed, afterGone;
int frame = 0;

#ifdef PARENT_TEST_X11
unsigned long hintOf(const std::shared_ptr<Window>& w) {
    unsigned long parent = 0;
    const auto handle = internal::WindowRequestAccess::binding(*w).handle;
    const unsigned long xwin = (unsigned long)(uintptr_t)sapp_window_x11_get_window(handle);
    if (!XGetTransientForHint(sapp_x11_get_display(), xwin, &parent)) return 0;
    return parent;
}
unsigned long xwinOf(Window& w) {
    return (unsigned long)(uintptr_t)sapp_window_x11_get_window(internal::WindowRequestAccess::binding(w).handle);
}
#endif

struct MainApp : App {
    WarningCounter warnings;
    void setup() override {
        WindowSettings s; s.setSize(64, 64);
        // A child of the launch window.
        child = createWindow(WindowSettings(s).setParentWindow(*getWindow()));
        check("child of the launch window is created", child && child->isOpen());
#ifdef PARENT_TEST_X11
        check("X11: WM_TRANSIENT_FOR names the launch window", child && hintOf(child) == xwinOf(*getWindow()));
#endif
        check("a valid parent logs no warning", warnings.count == 0);

        // A window that is about to close, and a child of a window that closes first.
        closing = createWindow(s);
        orphanParent = createWindow(s);
        orphan = createWindow(WindowSettings(s).setParentWindow(orphanParent));
        check("child of a secondary window is created", orphan && orphan->isOpen());
        closing->close();
        orphanParent->close();
    }
    void update() override {
        ++frame;
        if (!afterClosed && !closing->isOpen() && !orphanParent->isOpen()) {
            WindowSettings s; s.setSize(64, 64);
            afterClosed = createWindow(WindowSettings(s).setParentWindow(closing));   // closed, still held
            check("closed parent: window is still created", afterClosed && afterClosed->isOpen());
            check("closed parent: one warning", warnings.count == 1);
            check("parent closed first: the child stays open", orphan->isOpen());
            std::weak_ptr<Window> weak = closing;
            WindowSettings gone = WindowSettings(s).setParentWindow(closing);
            closing.reset();
            check("settings did not keep the closed parent alive", weak.expired());
            afterGone = createWindow(gone);
            check("gone parent: window is still created", afterGone && afterGone->isOpen());
            check("gone parent: two warnings", warnings.count == 2);
#ifdef PARENT_TEST_X11
            check("X11: a window without a live parent has no WM_TRANSIENT_FOR",
                  afterClosed && afterGone && hintOf(afterClosed) == 0 && hintOf(afterGone) == 0);
#endif
            exitApp();
        } else if (frame > 600) {
            check("windows closed within 600 frames", false);
            exitApp();
        }
    }
    void cleanup() override {
        child.reset(); closing.reset(); orphanParent.reset(); orphan.reset(); afterClosed.reset(); afterGone.reset();
        warnings.listener.disconnect();
    }
};
}

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || std::string(argv[1]) != "--display") return runHeadlessChecks();
    if (!internal::supportsSecondaryWindows()) { std::puts("SKIP: windowParent --display needs secondary windows"); return 0; }
    WindowSettings settings; settings.setSize(160, 120);
    return tcCoreTest::runDisplayTest<MainApp>(settings, failures);
}
