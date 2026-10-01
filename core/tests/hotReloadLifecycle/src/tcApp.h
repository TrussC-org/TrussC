#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// hotReloadLifecycle guest App
//
// Deliberately minimal: the regressions this test guards live in what the App
// BASE class touches on construction/destruction (AudioEngine listeners, the
// window-context root slot, Event COW listener lists). The extra update
// listener widens the churn on the global events() singleton.
//
// It also registers MCP tools, a status entry and a status image, each
// capturing `this` — the usual app pattern. On reload the host must remove
// them before this App is deleted, or the old build's handlers stay listed
// and callable with a dangling `this` (#227). The host must also SEE them: a
// Windows guest DLL used to register into its own copy of the registry (#249).
// guest_probe answers with the registration owner this generation was created
// under, so the host can tell the new generation answered, not an old one;
// guest_deferred defers its reply the way screenshot tools do; the control
// tools are registered from guest code the way apps do (in setup()).
//
// writeSharedState / readSharedState / sharedInstances / queueFromWorker /
// attachApp (tcApp.cpp) are guest code the cycles call through the vtable:
// settings app code writes and the core loop reads, state the host sets that
// guest code reads, the process-wide singletons and GPU caches guest code
// reaches, work it queues for the main thread, and the secondary windows'
// double-attach guard (guest setApp() adds, the host's close() removes), all
// of which a Windows guest DLL used to keep its own copy of (#249).
// addGuestChild makes a node in guest code (make_shared, so its control block
// is the guest's) for the window contexts' weak references the host must
// drop before it unloads the guest (#255).
//
// draw/exit (tcApp.cpp) only run in the windowed modes (`--app`,
// `--reload-check`; see main.cpp); the lifecycle cycles never call them.
// setup() runs in all: the cycles set cycleOnly and run it through the App's
// first update, to check its audio hooks are subscribed right after it
// (#426); it then only records the audio listeners it sees and skips the
// window work. In a window, setup() attaches tcxNodeInspector (which sets up
// tcxImGui): two addon singletons that live in the guest image and listen on
// the host's events, so a reload must not leave the previous generation's
// copies listening (#416). This also checks the guest target for addon include
// directories and for linking the addon archives.
// =============================================================================

// What guest code sees of state the host set (readSharedState).
struct GuestView {
    bool pixelPerfect = false;
    int sglMaxVertices = 0;
    bool fontSamplerReady = false;
    const void* windowContext = nullptr;
};

// Where guest code finds the one-per-process singletons and GPU caches
// (sharedInstances): each must be the host's instance, not one of its own.
struct GuestInstances {
    const void* audioEngine = nullptr;
    const void* screenRecorder = nullptr;
    const void* asyncScheduler = nullptr;
    const void* beepManager = nullptr;
    const void* consoleRunning = nullptr;
    const void* pbrPipeline = nullptr;
    const void* pointPipeline = nullptr;
    const void* fboShared = nullptr;
    const void* fboSharedMip = nullptr;
    const void* iblBake = nullptr;
    const void* fontCache = nullptr;
    const void* fontSamplers = nullptr;
    const void* nodeCount = nullptr;
    const void* textureCount = nullptr;
    const void* fboCount = nullptr;
    uint64_t asyncOwner = 0;   // a fresh AsyncScheduler owner token
    uint64_t timerId = 0;      // an id the guest's callAfter() handed out
};

// What the guest App's own hotReloadUnload listener saw (#416): how often it
// fired, and whether this App was still the main window's root then.
struct UnloadProbe {
    int fired = 0;
    bool appWasRoot = false;
};

class tcApp : public App {
public:
    tcApp() {
        updateListener_ = events().update.listen([this]() { ticks_++; });
        // App code using hotReloadUnload, as the reference shows
        unloadListener_ = events().hotReloadUnload.listen([this]() {
            if (!unloadProbe) return;
            unloadProbe->fired++;
            unloadProbe->appWasRoot = getRootNode() == static_cast<Node*>(this);
        });
        // The owner the host tagged this generation with (an identity only)
        const uint64_t generation = (uint64_t)(uintptr_t)mcp::detail::registrationOwner();
        mcp::tool("guest_probe", "hotReloadLifecycle guest tool")
            .bind(std::function<json()>([this, generation]() -> json {
                return json{{"ticks", ticks_}, {"generation", generation}};
            }));
        // The producer reaches the App through `this`, as app tools do: it
        // must never run once the App is deleted (hotReloadLifecycle unloads
        // the guest while one is pending).
        mcp::tool("guest_deferred", "hotReloadLifecycle guest tool answering after the frame")
            .bind(std::function<json()>([this, generation]() -> json {
                mcp::deferToolResultUntilAfterFrame([this, generation]() -> json {
                    return json{{"deferred", true}, {"generation", generation}, {"ticks", ticks_}};
                });
                return json(nullptr);  // replaced by the deferred result
            }));
        mcp::status("guest_status", std::function<double()>([this]() { return (double)ticks_; }));
        mcp::statusImage("guest_image", [this]() { (void)ticks_; return Pixels(); });
        mcp::registerControlTools();
    }

    void setup() override;
    void draw() override;
    void exit() override;

    // Defined in tcApp.cpp (guest only), virtual so the host's calls run the
    // guest's code.
    virtual void writeSharedState();
    virtual GuestView readSharedState();
    virtual GuestInstances sharedInstances();
    // runOnMainThread(++*ran) from a worker thread of the guest's own.
    virtual void queueFromWorker(std::atomic<int>* ran);
    // window.setApp(app) as app code calls it; true if the window took it.
    virtual bool attachApp(Window& window, std::shared_ptr<App> app);
    // Whether guest code sees `app` in the secondary windows' double-attach
    // guard (internal::attachedApps()).
    virtual bool seesAttached(const App* app);
    // A node made with make_shared in guest code, added as this App's child.
    virtual std::shared_ptr<Node> addGuestChild();
    // NodeInspector::setToggleKey() as app code calls it: the guest's
    // inspector singleton listens on the host's events (#416).
    virtual void useInspectorToggleKey();

    // Set by the lifecycle cycles before the first update: setup() records
    // what it sees and skips the window / ImGui work (no window there).
    bool cycleOnly = false;
    int setupCalls = 0;
    long audioOutHooksInSetup = -1;   // AudioEngine audioOut listeners in setup()
    long audioInHooksInSetup = -1;
    UnloadProbe* unloadProbe = nullptr;   // set by the cycles (host-owned)

private:
    EventListener updateListener_;
    EventListener unloadListener_;
    int ticks_ = 0;
};
