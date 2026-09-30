// =============================================================================
// appRoot — the running App is getRootNode() (#255)
//
// The main window's root is a weak reference, so the App can't register
// itself from its constructor (weak_from_this() is empty until it returns).
// Whoever creates the App through a shared_ptr registers it instead:
//   - runApp(): the setup callback buildAppDescriptor() installs makes the
//     App and registers it; the cleanup callback frees it, and the root with
//     it. Driven here without sapp_run(): the test calls the two callbacks
//     the way _setup_cb / _cleanup_cb do.
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

static const App* g_windowedApp = nullptr;

class WindowedApp : public App {
public:
    WindowedApp() { g_windowedApp = this; }
};

static void runAppCallbacks() {
    WindowSettings settings;
    (void)buildAppDescriptor<WindowedApp>(settings);
    check("runApp: no root before the setup callback", getRootNode() == nullptr);

    internal::appSetupFunc();
    check("runApp: the setup callback made the App", g_windowedApp != nullptr);
    check("runApp: the App is getRootNode() once it is made",
          g_windowedApp != nullptr && getRootNode() == g_windowedApp);

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
