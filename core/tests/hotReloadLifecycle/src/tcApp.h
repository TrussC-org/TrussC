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
//
// setup/draw/exit (tcApp.cpp) only run in `--app` mode (see main.cpp); the
// lifecycle cycles never call them. They use tcxImGui so the guest target is
// checked for addon include directories and for linking the addon archive.
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
};

class tcApp : public App {
public:
    tcApp() {
        updateListener_ = events().update.listen([this]() { ticks_++; });
        // The owner the host tagged this generation with (an identity only)
        const uint64_t generation = (uint64_t)(uintptr_t)mcp::detail::registrationOwner();
        mcp::tool("guest_probe", "hotReloadLifecycle guest tool")
            .bind(std::function<json()>([this, generation]() -> json {
                return json{{"ticks", ticks_}, {"generation", generation}};
            }));
        mcp::tool("guest_deferred", "hotReloadLifecycle guest tool answering after the frame")
            .bind(std::function<json()>([generation]() -> json {
                mcp::deferToolResultUntilAfterFrame([generation]() -> json {
                    return json{{"deferred", true}, {"generation", generation}};
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

private:
    EventListener updateListener_;
    int ticks_ = 0;
};
