// =============================================================================
// appRoot — the running App is getRootNode() (#255)
//
// The main window's root is a weak reference, so the App can't register
// itself from its constructor (weak_from_this() is empty until it returns).
// Whoever creates the App through a shared_ptr registers it instead:
//   - runApp(): the setup callback buildAppDescriptor() installs makes the
//     App and registers it; the cleanup callback frees it, and the root with
//     it. Driven here without sapp_run(): the test calls the setup, update
//     and cleanup callbacks the way _setup_cb / _frame_cb / _cleanup_cb do.
//     Inside the App's constructor getRootNode() is not the App yet, and
//     App::setSize() resizes no window and warns; from setup() it goes to
//     the main window as before (the App's own size then follows through
//     windowResized, which never comes without a window).
//   - runHeadlessApp(): the App is owned by a shared_ptr, registered while it
//     runs (in setup() and update()), so setup() can addChild(), and the root
//     is cleared when the run ends.
// (The hot reload host's registration is checked by hotReloadLifecycle, a
// secondary window's by Window::setApp() in nodeRemoval.)
//
// Headless: no window, no GPU. Exit code = number of failures.
// =============================================================================

#include <TrussC.h>

#include <cstdio>
#include <memory>
#include <string>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-72s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// Empty, not merely expired: an expired reference would still name the
// freed App's control block.
static bool rootIsEmpty() {
    const weak_ptr<Node>& root = internal::mainWindowContext().rootNode;
    const weak_ptr<Node> none;
    return !root.owner_before(none) && !none.owner_before(root);
}

// ---------------------------------------------------------------------------
// runApp()'s setup / cleanup callbacks
// ---------------------------------------------------------------------------

// Warnings logged with the App's constructor-time setSize() text.
static int g_ctorSizeWarnings = 0;

static const App* g_windowedApp = nullptr;
static bool g_rootInCtor = true;
static int g_warningsFromCtor = -1, g_warningsFromSetup = -1;
static float g_widthAfterCtor = 0, g_heightAfterCtor = 0;
static float g_widthAfterSetup = 0;
static bool g_setupRan = false;

class WindowedApp : public App {
public:
    WindowedApp() {
        g_windowedApp = this;
        g_rootInCtor = getRootNode() == this;
        const int before = g_ctorSizeWarnings;
        setSize(320, 240);
        g_warningsFromCtor = g_ctorSizeWarnings - before;
        g_widthAfterCtor = getWidth();
        g_heightAfterCtor = getHeight();
    }
    void setup() override {
        g_setupRan = true;
        const int before = g_ctorSizeWarnings;
        setSize(640, 480);
        g_warningsFromSetup = g_ctorSizeWarnings - before;
        g_widthAfterSetup = getWidth();
    }
};

static void runAppCallbacks() {
    EventListener logListener = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning &&
            e.message.find("setSize() in the App's constructor") != string::npos) {
            ++g_ctorSizeWarnings;
        }
    });

    WindowSettings settings;
    (void)buildAppDescriptor<WindowedApp>(settings);
    check("runApp: no root before the setup callback", getRootNode() == nullptr);

    internal::appSetupFunc();
    check("runApp: the setup callback made the App", g_windowedApp != nullptr);
    check("runApp: the App is getRootNode() once it is made",
          g_windowedApp != nullptr && getRootNode() == g_windowedApp);
    check("runApp: getRootNode() is not the App yet inside its constructor", !g_rootInCtor);
    check("runApp: setSize() in the constructor warns once", g_warningsFromCtor == 1);
    check("runApp: ...and resizes no window, only the App's own size",
          g_widthAfterCtor == 320 && g_heightAfterCtor == 240);

    internal::appUpdateFunc();   // first update: setup()
    check("runApp: setup() ran in the first update", g_setupRan);
    check("runApp: setSize() in setup() does not warn", g_warningsFromSetup == 0);
    check("runApp: ...and goes to the main window (the App's size is left to windowResized)",
          g_widthAfterSetup == 320);

    internal::appCleanupFunc();
    check("runApp: getRootNode() is null once the cleanup callback freed the App",
          getRootNode() == nullptr);
}

// ---------------------------------------------------------------------------
// runHeadlessApp()
// ---------------------------------------------------------------------------

static bool g_rootInSetup = false;
static bool g_childAdded = false;
static bool g_rootInUpdate = false;
static bool g_rootInCleanup = false;

class HeadlessApp : public App {
public:
    void setup() override {
        g_rootInSetup = getRootNode() == this;
        auto child = make_shared<Node>();
        addChild(child);
        g_childAdded = getChildCount() == 1 && child->getParent().get() == this;
    }
    void update() override {
        g_rootInUpdate = getRootNode() == this;
        requestExit();
    }
    void cleanup() override {
        g_rootInCleanup = getRootNode() == this;
    }
};

static void runHeadless() {
    runHeadlessApp<HeadlessApp>(HeadlessSettings().setFps(1000));
    check("runHeadlessApp: the App is getRootNode() in setup()", g_rootInSetup);
    check("runHeadlessApp: setup() can addChild() (the App is shared-owned)", g_childAdded);
    check("runHeadlessApp: the App is getRootNode() in update()", g_rootInUpdate);
    check("runHeadlessApp: the App is getRootNode() in cleanup()", g_rootInCleanup);
    check("runHeadlessApp: the root is cleared when the run ends",
          getRootNode() == nullptr && rootIsEmpty());
}

int main() {
    runAppCallbacks();
    runHeadless();

    std::printf("\n%s  (%d failure%s)\n", g_fail ? "FAILED" : "PASSED",
                g_fail, g_fail == 1 ? "" : "s");
    std::fflush(stdout);
    return g_fail ? 1 : 0;
}
