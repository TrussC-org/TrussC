// =============================================================================
// hotReloadLifecycle — regression test for the hot reload guest lifecycle
// =============================================================================
//
// Reproduces, without any window or runtime rebuild, the crash class where
// guest-library code leaks into host-owned state across reloads:
//
//   1. A function-local-static singleton first constructed by GUEST code
//      registers its exit destructor against the guest image's __dso_handle;
//      unloading an old guest then destroys an engine the host still uses
//      (macOS: recursive_mutex EINVAL on the next listen).
//   2. shared_ptr control blocks / std::function invokers allocated by an old
//      guest stay reachable from host state (e.g. Event<T>'s COW listener-list
//      snapshot) — unmapping the image turns their release into a jump into
//      unmapped memory (SEGV), or a crash in exit-time finalizers on Linux,
//      where dlclose often keeps images mapped and defers destructors to exit.
//
// The App base class traverses exactly that surface (the AudioEngine listener
// subscribe on its first setup(), Event COW list churn), so the test is just
// the real GuestLibrary driven through load -> create -> first update ->
// destroy -> unload cycles. The guest binary is identical each cycle — the bugs depend on
// image lifetime, not on the code changing — and GuestLibrary already loads
// each cycle from a fresh unique temp copy, exactly like a real reload.
//
// MCP registrations (#227, #249): the guest App registers tools, a status
// entry and a status image capturing `this`, and grants Control access. Each
// cycle checks guest registrations are gone after unload, while the grant,
// core tools and a tool the host registered itself stay. The registry must be the
// host's on every platform: on Windows the guest is a DLL that compiles its own
// copy of any header-inline state, so the MCP state is defined non-inline in
// TrussC.lib (tcMCP.cpp) and the guest imports it from the host.
//
// Each cycle also goes through the MCP HTTP server, the way a client reaches a
// running app (the requests are answered here by pumping processHttpQueue(),
// as the frame loop does): tools/list, a call to the guest's tool that must be
// answered by THIS generation, a guest tool that defers its reply (the
// deferral state must be shared too), the host's tc_get_status reading the
// guest's status entry, and a core control tool the guest granted, which must
// inject into the host's input dispatch. The unload itself happens while
// deferred replies wait for the afterFrame drain, as a reload does in a real
// frame: those whose producers run guest code (guest_deferred, which reaches
// the App through `this`, and tc_get_status_image on the guest's getter) must
// come back as an error, and a host tool's deferred reply must still answer.
// After unload the guest tool must be gone from the server as well.
//
// Settings and registries app code and the core loop share (#249): guest code
// calls setFps(), redraw(), setTouchAsMouse(), the clip / fov setters,
// setDataPathRoot() and registerGlyph() and installs the overlay queries, and
// the host must see each; guest code must in turn see what the host set
// (pixelPerfect, the sokol_gl budget, the bitmap-font sampler, the window
// context it is ticking), and node ids must keep counting across generations.
//
// Process-wide singletons and GPU caches (#249): the AudioEngine, the screen
// recorder, the async scheduler (and its owner numbering), the beep manager,
// the console state, the PBR / point pipelines, the FBO, IBL-bake and font
// caches, and the node / texture / FBO debug counters guest code reaches must
// be the host's instances, and the ids the guest's callAfter() hands out must
// continue the host's sequence. The GPU caches are the costly ones: nothing
// frees what they hold, so a guest with its own copy built a new set of
// sokol_gl contexts, shaders and atlases in the host's pools every reload, and
// FBO drawing stopped after a few reloads. Guest code's
// setBeepVolume() and mcp::alert() must reach the host (the alert over HTTP,
// through tc_get_alerts), and work a guest worker thread queues with
// runOnMainThread() must run when the host drains the main-thread queue.
// An App guest code attaches to a window and the host releases (as the
// platform close() does) must be released in guest code's view too: the
// double-attach guard is one set per process, not a copy per module.
//
// On Linux and macOS all of this holds either way, since the host uses (and so
// contains) every definition checked here; on Windows it fails if any of that
// state is header-inline again.
//
// The App's audio hooks (#426): a new generation's App is not subscribed by
// its constructor; its first tree update (handleUpdate(), as the host's frame
// does) runs setup() in guest code and only then subscribes audioOut() /
// audioIn(), once, through App's override of Node's post-setup hook. The
// unload detaches them again.
//
// Node references (#255), on every platform: the host makes the guest's App
// the main window's root (getRootNode(), a weak reference), and before it
// unloads the guest it resets the weak references every window context keeps
// to nodes (hover, grab, selection, the main root), which here name a node
// guest code made.
//
// The host's source watcher (#305) picks the files it watches by extension,
// whatever the case: .CPP / .H / .Hpp / .MM count like .cpp / .h / .hpp / .mm,
// other extensions do not (checkWatcherExtensions()).
//
// Guest singletons (#416): the host fires events().hotReloadUnload before it
// unloads a generation, while the App is still the main window's root (the
// guest App's own listener records it). Each cycle's guest uses
// tcxNodeInspector's toggle key, so the inspector singleton of that
// generation's image listens on the host's keyPressed / exit /
// hotReloadUnload; after the unload the host's listener counts are back to
// what they were before the generation was loaded, every cycle.
//
// Exit code: 0 = survived all cycles (including process exit), non-zero or a
// crash = regression. The test itself never enters TC_RUN_APP: no sokol loop,
// no GPU, no file watcher, no cmake rebuild — CI-safe on every desktop
// platform.
//
// `hotReloadLifecycle --app` instead runs the real hot reload host
// (TC_RUN_APP): a window, the file watcher and rebuilds, with the guest's
// tcxNodeInspector panel (drawn through tcxImGui) — edit src/tcApp.cpp while
// it runs to try a reload by hand. CI only builds that path, which still
// links the host loop and the guest's addon code on every desktop platform.
//
// `hotReloadLifecycle --reload-check` runs the same windowed host and reloads
// the guest twice by itself (Host::reload(): the cmake rebuild of the guest
// target, then the swap), checking each generation (#416): the host's
// listener counts on onRender (the inspector's frame driver and tcxImGui's
// render), rawEvent, the mouse / key events, afterFrame, exit and
// hotReloadUnload are the same as the first generation's, so the Hierarchy
// is drawn once; a press over the panel is taken by ImGui, and a press away
// from it, after a reload that happened while the cursor was over the panel,
// is not. It needs a display (e.g. Xvfb) and cmake; daily Linux CI runs it.
// =============================================================================

#include "tcApp.h"
#include "impl/httplib.h"

#ifndef TC_HOT_RELOAD_BUILD
// The cmake source scan keys the host/guest split off TC_HOT_RELOAD appearing
// in a .cpp: tcApp.cpp carries the real macro; this reference is inside an
// #ifndef so it never compiles, but keeps the intent greppable.
#error "hotReloadLifecycle must be configured as a hot reload (host/guest) build"
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;

// A JSON-RPC request POSTed to the MCP server from a worker thread while this
// (main) thread plays the frame loop's part (awaitPosts()). Everything the
// worker touches lives in this shared block, not in a caller's frame: past the
// deadline the worker is detached and may still finish later (name lookup and
// each send / recv have their own timeouts, the whole request has none), so it
// must not write into a returned frame or hand its late reply to the next
// request.
struct Exchange {
    std::string body;
    std::string reply;
    std::atomic<bool> done{false};
};
struct PendingPost {
    std::shared_ptr<Exchange> ex;
    std::thread worker;
};

static PendingPost startPost(int port, const std::string& body) {
    PendingPost p;
    p.ex = std::make_shared<Exchange>();
    p.ex->body = body;
    auto ex = p.ex;
    p.worker = std::thread([ex, port] {
        // Connect to the listener's actual address. "localhost" tries ::1 first
        // on Windows, adding a fallback delay for an IPv4 listener.
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(10);
        auto r = cli.Post("/mcp", ex->body, "application/json");
        if (r && r->status == 200) ex->reply = r->body;
        ex->done = true;
    });
    return p;
}

// Pump the frame loop's part until every post is answered (or 20 s pass):
// processHttpQueue() answers or stashes the requests, drainDeferredResponses()
// is the afterFrame step deferred tools wait for. `beforeDrain`, if given,
// runs once `deferred` replies are stashed and before the first drain: where
// a hot reload happens in a real frame (processHttpQueue() -> host poll ->
// reload, then the afterFrame drain). Returns each reply body, "" on a
// transport failure / timeout.
static std::vector<std::string> awaitPosts(std::vector<PendingPost>& posts, size_t deferred = 0,
                                           const std::function<void()>& beforeDrain = nullptr) {
    auto allDone = [&posts] {
        for (auto& p : posts) {
            if (!p.ex->done) return false;
        }
        return true;
    };
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    bool hooked = !beforeDrain;
    while (!allDone() && std::chrono::steady_clock::now() < deadline) {
        mcp::processHttpQueue();
        if (!hooked && mcp::internal::deferredResponses().size() >= deferred) {
            beforeDrain();
            hooked = true;
        }
        if (hooked) mcp::internal::drainDeferredResponses();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    std::vector<std::string> replies;
    for (auto& p : posts) {
        if (p.ex->done) {
            p.worker.join();
            replies.push_back(p.ex->reply);
        } else {
            p.worker.detach();
            replies.push_back("");
        }
    }
    return replies;
}

static std::string mcpPost(int port, const std::string& body) {
    std::vector<PendingPost> posts;
    posts.push_back(startPost(port, body));
    return awaitPosts(posts)[0];
}

static std::string toolCallBody(const std::string& name, const json& args) {
    static int id = 0;
    json req = {{"jsonrpc", "2.0"}, {"id", ++id}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", args}}}};
    return req.dump();
}

// A tools/call reply body -> the tool's content json (the text block, parsed),
// or a discarded json on failure. `error` receives the JSON-RPC error message.
static json toolContent(const std::string& body, std::string* error = nullptr) {
    json reply = json::parse(body, nullptr, false);
    if (reply.is_discarded() || !reply.is_object()) return json::value_t::discarded;
    if (reply.contains("error")) {
        if (error && reply["error"].is_object()) *error = reply["error"].value("message", "");
        return json::value_t::discarded;
    }
    try {
        return json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>());
    } catch (...) {
        return json::value_t::discarded;
    }
}

static json callTool(int port, const std::string& name, const json& args, std::string* error = nullptr) {
    return toolContent(mcpPost(port, toolCallBody(name, args)), error);
}

static bool listsTool(int port, const std::string& name) {
    json reply = json::parse(mcpPost(port, R"({"jsonrpc":"2.0","id":0,"method":"tools/list"})"),
                             nullptr, false);
    if (!reply.is_object() || !reply.contains("result") || !reply["result"]["tools"].is_array()) return false;
    for (auto& t : reply["result"]["tools"]) {
        if (t.is_object() && t.value("name", "") == name) return true;
    }
    return false;
}

// The host's own input dispatch (what the frame loop wires to the App). The
// guest-registered tc_key_press must end up here.
static std::atomic<int> g_hostKeyPresses{0};

// Locate the guest library near the executable, mirroring Host::init()'s
// layout knowledge: exe = <proj>/bin/<name>[.app/Contents/MacOS/<name>],
// guest = <proj>/<buildDir>[/<config>]/libguest.{dylib,so}|guest.dll.
static std::string findGuestLibrary() {
#ifdef __APPLE__
    const char* guestName = "libguest.dylib";
#elif defined(_WIN32)
    const char* guestName = "guest.dll";
#else
    const char* guestName = "libguest.so";
#endif
    fs::path exe = fs::canonical(getExecutablePath());
    fs::path proj = exe.parent_path();
#ifdef __APPLE__
    for (int i = 0; i < 4 && proj.has_parent_path(); ++i) proj = proj.parent_path();
#else
    proj = proj.parent_path();
#endif
    const char* buildDirs[] = {"build-macos", "build-windows", "build-linux",
                               "build", "xcode", "vs"};
    // Same rule as the host: multi-config generators use <buildDir>/<config>/.
    const bool multiConfig = TC_HOT_RELOAD_MULTI_CONFIG != 0 && *TC_HOT_RELOAD_CONFIG;
    std::vector<fs::path> candidates;
    if (*TC_HOT_RELOAD_BUILD_DIR) candidates.push_back((proj / TC_HOT_RELOAD_BUILD_DIR).lexically_normal());
    for (const char* b : buildDirs) candidates.push_back(proj / b);
    for (const auto& candidate : candidates) {
        fs::path p = candidate;
        if (multiConfig) p /= TC_HOT_RELOAD_CONFIG;
        p /= guestName;
        if (fs::exists(p)) return p.string();
    }
    return "";
}

// The load -> create -> check -> unload cycles. Returns 0 or the failing
// check's exit code (after unloading the guest, so nothing it registered
// outlives its App while the caller shuts the server down).
static int runCycles(const std::string& guestPath, int port) {
    using namespace trussc::hot_reload;

    auto hasStatus = [](const char* name) {
        for (auto& e : mcp::internal::statusRegistry()) if (e.name == name) return true;
        return false;
    };
    auto hasStatusImage = [](const char* name) {
        for (auto& e : mcp::internal::statusImageRegistry()) if (e.name == name) return true;
        return false;
    };

    // The host's events the guest's inspector singleton listens on (#416)
    auto hostListenerCounts = [] {
        return std::vector<size_t>{events().keyPressed.listenerCount(), events().exit.listenerCount(),
                                   events().hotReloadUnload.listenerCount()};
    };

    internal::createMainWindow(WindowSettings{});
    internal::prepareMainAppBindings();
    auto* mainHandle = internal::mainWindow();
    internal::WindowRequestAccess::openMain(*mainHandle); // no native window in this mode
    const auto mainId = internal::WindowRequestAccess::creationId(*mainHandle);
    struct MainOwnerReset { ~MainOwnerReset() { internal::destroyMainWindow(); } } mainOwnerReset;
    const int kCycles = 5;
    uint64_t prevAppId = 0;
    for (int i = 1; i <= kCycles; ++i) {
        const std::vector<size_t> countsBeforeLoad = hostListenerCounts();
        UnloadProbe unloadProbe;
        GuestLibrary lib;
        auto fail = [&](int code, const std::string& what) {
            std::printf("hotReloadLifecycle: FAIL - %s (cycle %d)\n", what.c_str(), i);
            lib.unload();
            return code;
        };
        if (!lib.load(guestPath)) return fail(2, "load failed");
        // App construction and its first update (setup(), then the audio
        // hooks) walk the hazardous surface: first-touch of the AudioEngine
        // singleton, listener registration on host-owned Events.
        App* app = lib.create();
        if (!app || app->getWindow() != mainHandle || internal::currentWindow() != mainHandle ||
            internal::WindowRequestAccess::creationId(*mainHandle) != mainId || !internal::attachedApps().count(app))
            return fail(69, "reload must use shared main Window attachment and retain identity");
        if (!app) return fail(3, "create failed");
        // Node ids are unique per process: a new generation's nodes (its App
        // first) must not count from 0 again, as a Windows guest DLL's own
        // counter did.
        if (i > 1 && app->getInstanceId() <= prevAppId) {
            return fail(26, "the guest's node ids restarted instead of continuing the process-wide sequence");
        }
        prevAppId = app->getInstanceId();
        const auto boundListenerCounts = hostListenerCounts();
        if (!static_cast<tcApp*>(app)->seesMainWindow(mainHandle)) return fail(69, "guest sees different main Window state");
        // The host makes the guest's App the main window's root
        // (getRootNode()), held weakly (#255): the App's constructor can't
        // register itself, weak_from_this() being empty until it returns.
        if (internal::mainWindowContext().rootNode.lock().get() != app) {
            return fail(38, "the guest's App is not the main window's root (getRootNode())");
        }
        // The first update runs setup() (guest code; it records the audio
        // listeners it sees) and then subscribes the App's audio hooks. A
        // hook subscribed by the constructor, or before setup(), would already
        // be counted in setup(), and the attach would add none.
        long audioOutBase = -1, audioInBase = -1;   // listeners before this App's
        {
            auto* cycleApp = static_cast<tcApp*>(app);
            cycleApp->cycleOnly = true;   // setup() skips the window / ImGui work
            cycleApp->unloadProbe = &unloadProbe;
            app->handleUpdate(0, 0);
            app->handleUpdate(0, 0);
            auto& engine = AudioEngine::getInstance();
            if (cycleApp->setupCalls != 1) return fail(40, "the guest App's setup() did not run exactly once");
            if (cycleApp->audioOutHooksInSetup < 0 ||
                engine.audioOut.listenerCount() != (size_t)cycleApp->audioOutHooksInSetup + 1 ||
                engine.audioIn.listenerCount() != (size_t)cycleApp->audioInHooksInSetup + 1) {
                return fail(40, "the guest App's audio hooks were not subscribed exactly once, after setup() "
                                "(" + std::to_string(cycleApp->audioOutHooksInSetup) + " in setup(), " +
                                std::to_string(engine.audioOut.listenerCount()) + " after)");
            }
            audioOutBase = cycleApp->audioOutHooksInSetup;
            audioInBase = cycleApp->audioInHooksInSetup;
        }
        if (!(mcp::hasTool("guest_probe") && hasStatus("guest_status") && hasStatusImage("guest_image"))) {
            return fail(4, "guest MCP registrations not visible to the host");
        }

        // A guest singleton listening on the host's events (#416)
        static_cast<tcApp*>(app)->useInspectorToggleKey();
        if (events().keyPressed.listenerCount() != boundListenerCounts[0] + 1 ||
            events().hotReloadUnload.listenerCount() <= countsBeforeLoad[2]) {
            return fail(42, "the guest's NodeInspector::setToggleKey() did not listen on the host's events");
        }

        // Through the HTTP server, as a client sees the running app
        const uint64_t generation = (uint64_t)(uintptr_t)lib.mcpOwner;
        if (!listsTool(port, "guest_probe") || !listsTool(port, "host_probe")) {
            return fail(7, "tools/list over HTTP lacks guest_probe / host_probe");
        }
        json probe = callTool(port, "guest_probe", json::object());
        if (!probe.is_object() || probe.value("generation", (uint64_t)0) != generation) {
            return fail(8, "guest_probe over HTTP was not answered by this generation: " +
                           (probe.is_discarded() ? std::string("no reply") : probe.dump()));
        }
        // A deferral the server did not see comes back as the handler's own
        // (null) return value instead of the deferred result.
        json deferred = callTool(port, "guest_deferred", json::object());
        if (!deferred.is_object() || !deferred.value("deferred", false) ||
            deferred.value("generation", (uint64_t)0) != generation) {
            return fail(9, "guest_deferred over HTTP did not return its deferred result: " +
                           (deferred.is_discarded() ? std::string("no reply") : deferred.dump()));
        }
        json status = callTool(port, "tc_get_status", json::object());
        bool statusListed = false;
        bool textListed = false;
        bool graphListed = false;
        if (status.is_object() && status["values"].is_array()) {
            for (auto& v : status["values"]) {
                if (!v.is_object()) continue;
                const auto name = v.value("name", "");
                statusListed |= name == "guest_status" && v["value"] == probe["ticks"];
                textListed |= name == "guest_text" && v["value"] == std::to_string(probe["ticks"].get<int>());
                graphListed |= name == "guest_graph" && v["value"] == probe["ticks"] && v.value("mode", "") == "graph";
            }
        }
        if (!statusListed || !textListed || !graphListed)
            return fail(11, "tc_get_status over HTTP does not report the guest's numeric, string and graph getters");
        const int pressesBefore = g_hostKeyPresses.load();
        json pressed = callTool(port, "tc_key_press", json{{"key", 65}});
        json released = callTool(port, "tc_key_release", json{{"key", 65}});
        if (pressed.is_discarded() || g_hostKeyPresses.load() != pressesBefore + 1) {
            return fail(12, "the guest-registered tc_key_press did not reach the host's key dispatch");
        }
        if (released.is_discarded()) return fail(25, "the guest-registered tc_key_release did not answer");

        // Settings and registries shared by app code and the core loop (#249).
        // The host resets them first, so each generation must write its own.
        auto* guest = static_cast<tcApp*>(app);
        setFps(internal::VSYNC);
        internal::mainLoop().redrawCount = 0;
        setTouchAsMouse(true);
        setNearClip(0.0f);
        setFarClip(0.0f);
        setDefaultScreenFovDeg(45.0f);
        setDataPathRoot("data");
        bitmapfont::internal::registry().clear();
        internal::overlayHoveredQuery() = nullptr;
        internal::overlayFocusedQuery() = nullptr;
        setBeepVolume(0.5f);
        {
            std::lock_guard<std::mutex> lock(mcp::internal::alertMutex());
            mcp::internal::alertQueue().clear();
        }
        guest->writeSharedState();
        const FpsSettings fps = getFpsSettings();
        const bool overlaySeen = isOverlayHovered() && isOverlayFocused();
        // Drop the guest's queries before its App goes (its code stays mapped,
        // but the host should not call into a destroyed generation).
        internal::overlayHoveredQuery() = nullptr;
        internal::overlayFocusedQuery() = nullptr;
        if (fps.updateFps != 37.0f || fps.drawFps != 37.0f || !fps.synced) {
            return fail(14, "setFps() from the guest did not reach the host's loop");
        }
        if (internal::mainLoop().redrawCount != 3) return fail(15, "redraw() from the guest did not reach the host's loop");
        if (getTouchAsMouse()) return fail(16, "setTouchAsMouse() from the guest did not reach the host");
        if (getNearClip() != 0.25f || getFarClip() != 750.0f || std::abs(getDefaultScreenFovDeg() - 30.0f) > 0.0001f) {
            return fail(17, "setNearClip / setFarClip / setDefaultScreenFov from the guest did not reach the host");
        }
        if (getDataPathRoot() != fs::path("guest-data-root")) {
            return fail(18, "setDataPathRoot() from the guest did not reach the host");
        }
        if (!bitmapfont::findRegistered(0xE000)) {
            return fail(19, "a glyph the guest registered is not in the host's registry");
        }
        if (!overlaySeen) return fail(20, "the overlay queries the guest installed are not seen by the host");
        if (getBeepVolume() != 0.37f) return fail(27, "setBeepVolume() from the guest did not reach the host's beep manager");
        json alerts = callTool(port, "tc_get_alerts", json::object());
        bool alertSeen = false;
        if (alerts.is_object() && alerts["alerts"].is_array()) {
            for (auto& a : alerts["alerts"]) {
                alertSeen |= a.is_object() && a.value("text", "") == "hotReloadLifecycle guest alert";
            }
        }
        if (!alertSeen) return fail(28, "an mcp::alert() from the guest is not in the host's tc_get_alerts over HTTP");
        setFps(internal::VSYNC);
        setTouchAsMouse(true);
        setNearClip(0.0f);
        setFarClip(0.0f);
        setDefaultScreenFovDeg(45.0f);
        setDataPathRoot("data");
        setBeepVolume(0.5f);
        bitmapfont::internal::registry().clear();   // it points into the guest's image

        // And the other way: what guest code sees of state the host sets.
        internal::WindowContext secondary;
        secondary.isMain = false;
        const int grownBudget = internal::sglBudget().maxVertices * 2;
        const int savedBudget = internal::sglBudget().maxVertices;
        internal::pixelPerfectMode() = true;
        internal::sglBudget().maxVertices = grownBudget;
        internal::bitmapFontAtlas().initialized = true;   // flag only; no GPU here
        internal::WindowContext* prevCtx = internal::currentWindowCtx();
        internal::currentWindowCtx() = &secondary;
        mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        const GuestView seen = guest->readSharedState();
        mcp::setToolLevel(mcp::ToolLevel::Control);
        if (seen.toolLevel != mcp::ToolLevel::ReadOnly)
            return fail(38, "the guest does not see the host's MCP grant");
        internal::currentWindowCtx() = prevCtx;
        internal::pixelPerfectMode() = false;
        internal::sglBudget().maxVertices = savedBudget;
        internal::bitmapFontAtlas().initialized = false;
        if (!seen.pixelPerfect) return fail(21, "the guest does not see the host's pixelPerfect mode");
        if (seen.sglMaxVertices != grownBudget) return fail(22, "the guest does not see the host's sokol_gl budget");
        if (!seen.fontSamplerReady) return fail(23, "the guest does not see the host's bitmap-font sampler");
        if (seen.windowContext != &secondary) {
            return fail(24, "the guest does not see the window context the host is ticking");
        }

        // One instance per process: the singletons and GPU caches guest code
        // reaches must be the host's (none of these lookups touches the GPU).
        const uint64_t ownerBefore = internal::AsyncScheduler::newOwner();
        const uint64_t timerBefore = internal::nextNodeTimerId();
        const GuestInstances in = guest->sharedInstances();
        const uint64_t timerAfter = internal::nextNodeTimerId();
        const uint64_t ownerAfter = internal::AsyncScheduler::newOwner();
        const struct { const void* guest; const void* host; const char* what; } same[] = {
            {in.version, getVersion(), "getVersion() string"},
            {in.audioEngine, &AudioEngine::getInstance(), "AudioEngine::getInstance()"},
            {in.screenRecorder, &internal::globalScreenRecorder(), "screen recorder"},
            {in.asyncScheduler, &internal::AsyncScheduler::get(), "AsyncScheduler"},
            {in.beepManager, &internal::getManager(), "beep manager"},
            {in.consoleRunning, &console::internal::isRunning(), "console state"},
            {in.pbrPipeline, &internal::getPbrPipeline(), "PBR pipeline"},
            {in.pointPipeline, &internal::getPointPipeline(), "point pipeline"},
            {in.fboShared, &internal::fboSharedMap(), "FBO context cache (a new sokol_gl context per reload)"},
            {in.fboSharedMip, &internal::fboSharedMipMap(), "FBO mipmap pipeline cache"},
            {in.iblBake, &internal::iblBakeResources(), "IBL bake pipelines"},
            {in.fontCache, &internal::SharedFontCache::getInstance(), "font atlas cache"},
            {in.fontSamplers, &internal::fontSamplers(), "font samplers"},
            {in.nodeCount, &internal::nodeCount(), "getNodeCount() counter"},
            {in.textureCount, &internal::textureCount(), "getTextureCount() counter"},
            {in.fboCount, &internal::fboCount(), "getFboCount() counter"},
        };
        for (const auto& s : same) {
            if (s.guest != s.host) return fail(29, std::string("the guest has its own ") + s.what);
        }
        if (!(ownerBefore < in.asyncOwner && in.asyncOwner < ownerAfter)) {
            return fail(30, "AsyncScheduler::newOwner() in the guest does not continue the host's sequence");
        }
        if (!(timerBefore < in.timerId && in.timerId < timerAfter)) {
            return fail(34, "a timer id from the guest's callAfter() does not continue the host's sequence (one node could hold two timers with the same id)");
        }

        // Work a guest worker thread hands to runOnMainThread() runs when the
        // host drains the main-thread queue (as _frame_cb does), not before.
        std::atomic<int> ran{0};
        guest->queueFromWorker(&ran);
        const int ranOnWorker = ran.load();
        internal::drainMainThreadQueue();
        if (ranOnWorker != 0) {
            return fail(31, "runOnMainThread() on a guest worker thread ran there: the guest's main thread id is not the host's");
        }
        if (ran.load() != 1) {
            return fail(32, "work a guest worker thread queued with runOnMainThread() never reached the host's main-thread queue");
        }

        // A secondary window's App: guest code attaches it (Window::setApp()
        // is inline), the host releases it, and guest code must see the
        // release in the double-attach guard. The platform close() that
        // releases it is TrussC.lib code; the host's own setApp(nullptr)
        // releases it the same way without a native window. A guest with its
        // own guard never saw the release: the App stayed "attached" in the
        // guest's view ("already drives another window" for anything there).
        // An App runs once (#256: a closed App is not attached again), so the
        // second attach, as when an app reopens a window the user closed,
        // uses a new App. setApp() only takes an open window: the windows
        // get a stand-in native state, never dereferenced here and cleared
        // before ~Window() would close() it.
        {
            // These Apps are not the main context's root (only runApp() or the
            // host makes an App the root), so setApp() does not refuse them as
            // the running main App.
            auto sub = std::make_shared<App>();
            auto reopened = std::make_shared<App>();
            static int nativeStandIn = 0;
            Window first, second;
            first.native_ = &nativeStandIn;
            second.native_ = &nativeStandIn;
            const bool attached = guest->attachApp(first, sub);
            const bool guestSawAttach = attached && guest->seesAttached(sub.get());
            first.setApp(nullptr);
            internal::applyPendingAppForTests(first);   // the frame boundary
            const bool guestSawRelease = !guest->seesAttached(sub.get());
            const bool attachedNew = guest->attachApp(second, reopened);
            second.setApp(nullptr);
            internal::applyPendingAppForTests(second);   // the frame boundary
            first.native_ = nullptr;
            second.native_ = nullptr;
            if (!attached || !guestSawAttach) {
                return fail(33, "guest code could not attach an App to a window, or does not see it attached");
            }
            if (!guestSawRelease) {
                return fail(33, "guest code still sees an App the host released from its window as attached: the guest keeps its own double-attach guard");
            }
            if (!attachedNew) return fail(33, "guest code could not attach a new App to another window");
        }

        // Node references into the guest (#255): hover, grab and selection in
        // the main window's context and in a secondary window's (one the host
        // keeps across the reload) name a node guest code made. The host must
        // drop them, and the main root, before the guest goes: releasing the
        // last weak reference to a make_shared node runs code of the module
        // that created it. Left alone they would only expire when the App is
        // deleted; the check after the unload tells the two apart.
        Window keptWindow;
        {
            std::shared_ptr<Node> guestNode = guest->addGuestChild();
            for (internal::WindowContext* ctx : {&internal::mainWindowContext(), &keptWindow.context()}) {
                ctx->hoveredNode = guestNode;
                ctx->prevHoveredNode = guestNode;
                ctx->grabbedNode = guestNode;
                ctx->grabbedButton = 0;
                ctx->selectedNode = guestNode;
            }
        }

        // Destruction + unload: listener removal churns the COW lists, and the
        // (pre-fix) dlclose here is what armed/triggered both crashes. It
        // happens while three deferred replies wait for the afterFrame drain,
        // as a reload does in a real frame. The two whose producers run guest
        // code (guest_deferred reaches the App through `this`; the host's
        // tc_get_status_image runs the guest's getter) must be answered with
        // an error, never produced on the deleted App; the host's own deferred
        // tool must still answer.
        std::vector<PendingPost> posts;
        posts.push_back(startPost(port, toolCallBody("guest_deferred", json::object())));
        posts.push_back(startPost(port, toolCallBody("tc_get_status_image", json{{"name", "guest_image"}})));
        posts.push_back(startPost(port, toolCallBody("host_deferred", json::object())));
        bool unloadedWhileDeferred = false;
        auto replies = awaitPosts(posts, posts.size(), [&] {
            if (i == kCycles) {
                // Normal host shutdown ends the App before unloading it.
                // unload() must not repeat exit() / cleanup().
                app->exit();
                internal::detachAppAudio(*app);
                app->cleanup();
            }
            lib.unload();
            unloadedWhileDeferred = true;
        });
        if (!unloadedWhileDeferred) {
            lib.unload();
            return fail(35, "guest_deferred, tc_get_status_image and host_deferred did not all leave a deferred reply pending");
        }
        // Reset, not just expired: an expired reference still holds the guest's
        // control block.
        auto isReset = [](const std::weak_ptr<Node>& ref) {
            const std::weak_ptr<Node> none;
            return !ref.owner_before(none) && !none.owner_before(ref);
        };
        for (internal::WindowContext* ctx : {&internal::mainWindowContext(), &keptWindow.context()}) {
            if (!isReset(ctx->hoveredNode) || !isReset(ctx->prevHoveredNode) || !isReset(ctx->grabbedNode) ||
                ctx->grabbedButton != -1 || !isReset(ctx->selectedNode)) {
                return fail(39, std::string("unloading the guest left a reference to one of its nodes in the ") +
                                (ctx == &keptWindow.context() ? "secondary" : "main") +
                                " window's hover / grab / selection");
            }
        }
        if (!isReset(internal::mainWindowContext().rootNode)) {
            return fail(39, "unloading the guest left the main window's root pointing at its App");
        }
        if (unloadProbe.fired != 1 || !unloadProbe.appWasRoot) {
            return fail(43, "hotReloadUnload did not fire once before the guest's App and node references were released "
                            "(fired " + std::to_string(unloadProbe.fired) + " time(s))");
        }
        if (unloadProbe.exits != 1 || unloadProbe.cleanups != 1 ||
            !unloadProbe.audioDetachedInCleanup) {
            return fail(45, "guest unload did not run exit, detach audio, cleanup once in order");
        }
        if (hostListenerCounts() != countsBeforeLoad) {
            const std::vector<size_t> after = hostListenerCounts();
            return fail(44, "the unloaded generation left listeners on the host's events: keyPressed " +
                            std::to_string(countsBeforeLoad[0]) + " -> " + std::to_string(after[0]) + ", exit " +
                            std::to_string(countsBeforeLoad[1]) + " -> " + std::to_string(after[1]) +
                            ", hotReloadUnload " + std::to_string(countsBeforeLoad[2]) + " -> " +
                            std::to_string(after[2]));
        }
        if (AudioEngine::getInstance().audioOut.listenerCount() != (size_t)audioOutBase ||
            AudioEngine::getInstance().audioIn.listenerCount() != (size_t)audioInBase) {
            return fail(41, "the unloaded guest App's audio hooks are still subscribed");
        }
        for (size_t k = 0; k < 2; k++) {
            json cancelled = json::parse(replies[k], nullptr, false);
            const json expected = {{"jsonrpc", "2.0"},
                {"id", json::parse(posts[k].ex->body).at("id")},
                {"result", {{"content", {{{"type", "text"},
                    {"text", "the app code behind this reply was unloaded by a hot reload before the reply was produced"}}}},
                    {"isError", true}}}};
            if (cancelled != expected) {
                return fail(35, std::string("a deferred reply running guest code, pending when the guest was unloaded, "
                                            "was not answered with the unload error: ") +
                                (cancelled.is_discarded() ? std::string("no reply") : cancelled.dump()));
            }
        }
        json hostDeferred = toolContent(replies[2]);
        if (!hostDeferred.is_object() || !hostDeferred.value("deferred", false)) {
            return fail(36, "a host tool's deferred reply pending across the guest's unload did not survive it: " +
                            (hostDeferred.is_discarded() ? std::string("no reply") : hostDeferred.dump()));
        }
        if (mcp::hasTool("guest_probe") ||
            hasStatus("guest_status") || hasStatus("guest_text") ||
            hasStatus("guest_graph") || hasStatusImage("guest_image")) {
            return fail(5, "the destroyed guest's MCP registrations are still listed");
        }
        std::string callError;
        if (!callTool(port, "guest_probe", json::object(), &callError).is_discarded() ||
            callError.find("Tool not found") == std::string::npos) {
            return fail(13, "the destroyed guest's tool still answers over HTTP");
        }
        if (!mcp::hasTool("host_probe")) return fail(6, "the host's own tool was removed");
        if (!hasStatus("host_status") || !hasStatusImage("host_image"))
            return fail(6, "the host's own status registrations were removed");
        if (!mcp::hasTool("tc_key_press") || mcp::getToolLevel() != mcp::ToolLevel::Control)
            return fail(37, "core tools and the host grant must survive guest unload");
        std::printf("hotReloadLifecycle: cycle %d/%d ok\n", i, kCycles);
    }
    // Main scene swap while the guest image stays loaded: Window owns the App
    // and releases it at the swap. Registrations belong to the generation, not
    // to one App, so they are retired by the generation's unload (the lifetime
    // checks below cover the replacement keeping its own).
    {
        GuestLibrary lib;
        if (!lib.load(guestPath)) return 71;
        auto* raw = static_cast<tcApp*>(lib.create());
        if (!raw) return 71;
        raw->cycleOnly = true;
        raw->handleUpdate(0, 0);
        std::weak_ptr<App> outgoing = internal::mainApp();
        mainHandle->setApp(std::make_shared<App>());
        internal::applyPendingAppForTests(*mainHandle);
        if (!outgoing.expired()) {
            std::printf("hotReloadLifecycle: FAIL - main setApp retained the outgoing guest App\n");
            lib.unload();
            return 71;
        }
        lib.unload();
        if (mcp::hasTool("guest_probe") || hasStatus("guest_status") || hasStatusImage("guest_image")) {
            std::printf("hotReloadLifecycle: FAIL - unload left guest registrations behind\n");
            return 71;
        }
        std::printf("hotReloadLifecycle: main setApp releases the guest App; unload retires its registrations\n");
    }
    return 0;
}

// FileWatcher::init() watches the sources under src/ by extension, compared
// case-insensitively (#305). Returns 0 when it picked exactly the right files.
static int checkWatcherExtensions() {
    const fs::path dir = fs::temp_directory_path() / "tc_hotReloadLifecycle_watch";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "sub", ec);
    const std::vector<std::string> watched = {"a.cpp", "b.CPP", "c.h", "d.H", "e.Hpp",
                                              "f.mm", "sub/g.MM"};
    const std::vector<std::string> ignored = {"h.txt", "i.CPPX", "j.c", "k"};
    for (auto& n : watched) std::ofstream(dir / n) << "// x\n";
    for (auto& n : ignored) std::ofstream(dir / n) << "// x\n";

    trussc::hot_reload::FileWatcher w;
    w.init(internal::pathToUtf8(dir));
    int rc = 0;
    for (auto& n : watched) {
        bool found = false;
        for (auto& p : w.watchPaths) found = found || fs::equivalent(p, dir / n, ec);
        if (!found) {
            std::printf("hotReloadLifecycle: FAIL - watcher skipped %s\n", n.c_str());
            rc = 40;
        }
    }
    if (w.watchPaths.size() != watched.size()) {
        std::printf("hotReloadLifecycle: FAIL - watcher took %zu files, expected %zu\n",
                    w.watchPaths.size(), watched.size());
        rc = 40;
    }
    fs::remove_all(dir, ec);
    if (rc == 0) std::printf("hotReloadLifecycle: watcher extensions ok\n");
    return rc;
}

// The host's load failures go through the Logger (#311), so they reach the
// log file and onLog, not only stderr: a library that is missing (the copy to
// the temp path fails) and a file that is not a library (dlopen /
// LoadLibrary fails) each log one Error tagged [HotReload]. Returns 0 when
// both did.
static int checkLoadFailuresLogged() {
    std::mutex m;
    std::vector<std::string> errors;
    EventListener listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Error) return;
        std::lock_guard<std::mutex> lock(m);
        errors.push_back(e.message);
    });
    const fs::path dir = fs::temp_directory_path() / "tc_hotReloadLifecycle_loadfail";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);
    const fs::path notALibrary = dir / "notALibrary.so";
    std::ofstream(notALibrary) << "not a shared library\n";

    int rc = 0;
    auto expectOneError = [&](const char* name, const fs::path& path) {
        {
            std::lock_guard<std::mutex> lock(m);
            errors.clear();
        }
        trussc::hot_reload::GuestLibrary lib;
        const bool loaded = lib.load(internal::pathToUtf8(path));
        std::lock_guard<std::mutex> lock(m);
        if (loaded) {
            std::printf("hotReloadLifecycle: FAIL - %s loaded\n", name);
            rc = 50;
        } else if (errors.size() != 1 || errors[0].rfind("[HotReload] ", 0) != 0) {
            std::printf("hotReloadLifecycle: FAIL - %s logged %zu Error line(s)%s%s\n", name,
                        errors.size(), errors.empty() ? "" : ", first: ",
                        errors.empty() ? "" : errors[0].c_str());
            rc = 50;
        }
    };
    expectOneError("a missing library", dir / "missing.so");
    expectOneError("a file that is not a library", notALibrary);
    listener.disconnect();
    fs::remove_all(dir, ec);
    if (rc == 0) std::printf("hotReloadLifecycle: load failures logged ok\n");
    return rc;
}

// Preflight failures must stop before spawning cmake, emit exactly one Error
// per attempt, and leave the current guest intact. A child mode of this test
// stands in for cmake and records whether the build command was reached.
static int checkRebuildPreflight() {
    using trussc::hot_reload::Host;
    // The host normalizes candidates, so compare paths, not spellings.
    auto samePath = [](const std::string& a, const fs::path& b) {
        return fs::path(a).lexically_normal() == b.lexically_normal();
    };
    const fs::path dir = fs::temp_directory_path() / "tc_hotReloadLifecycle_preflight";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir / "custom-tree");
    const fs::path configured = (dir / "custom-tree").lexically_normal();
    const fs::path coreLib = dir / "core-lib";
    std::ofstream(coreLib) << "mtime probe\n";
    const auto hostTime = fs::last_write_time(getExecutablePath());
    fs::last_write_time(coreLib, hostTime + std::chrono::hours(1));
    std::ofstream(configured / "CMakeCache.txt")
        << "CMAKE_COMMAND:INTERNAL=" << getExecutablePath().string() << "\n";

    std::vector<std::string> errors;
    EventListener listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error) errors.push_back(e.message);
    });
    int rc = 0;
    auto expect = [&](bool condition, const char* what) {
        if (!condition) {
            std::printf("hotReloadLifecycle: FAIL - preflight %s\n", what);
            rc = 51;
        }
    };
    const std::string staleMessage =
        "[HotReload] the host executable is older than TrussC; rebuild and restart the app";
    Host host;
    host.projectDir = dir.string();
    host.coreLibPath = coreLib.string();
    expect(host.findBuildDirectory("custom-tree") && samePath(host.buildDir, configured),
           "did not resolve the configured directory against the project root");
    expect(host.findBuildDirectory(configured.string()) && samePath(host.buildDir, configured),
           "did not prefer configured directory");
    for (int attempt = 0; attempt < 2; ++attempt) {
        errors.clear();
        expect(!host.rebuildGuest(), "accepted a stale host");
        expect(errors.size() == 1 && errors[0] == staleMessage, "stale host did not log one Error");
    }
    // Initial failure must not add an 'Initial Guest build failed' Error.
    errors.clear();
    Host initial;
    initial.coreLibPath = coreLib.string();
    expect(!initial.init(), "initialized a stale host");
    expect(errors.size() == 1 && errors[0] == staleMessage, "initial stale host logged duplicate Errors");

    auto retainedPreflightApp = std::make_shared<App>();
    host.guest.app = retainedPreflightApp;
    App* runningApp = host.getApp();
    errors.clear();
    expect(!host.reload() && host.getApp() == runningApp && host.reloadCount == 0,
           "failed reload replaced the running guest");
    expect(errors.size() == 1 && errors[0] == staleMessage, "reload logged duplicate Errors");
    expect(!fs::exists(configured / "build-command-reached"), "stale host launched the build command");

    // Missing or unreadable core paths skip the mtime check silently. The
    // mock build command, rather than a real cmake build, proves it proceeds.
    for (const fs::path& probe : {dir / "missing-core", coreLib / "unreadable-child", coreLib}) {
        if (probe == coreLib) fs::last_write_time(coreLib, hostTime);
        host.coreLibPath = probe.string();
        errors.clear();
        expect(host.rebuildGuest() && errors.empty(), "missing or equal-mtime core blocked rebuilding");
        expect(fs::exists(configured / "build-command-reached"), "valid rebuild did not launch command");
        fs::remove(configured / "build-command-reached");
    }

    // Missing configured path falls back to the existing platform name list.
#ifdef __APPLE__
    const fs::path fallback = dir / "build-macos";
#elif defined(_WIN32)
    const fs::path fallback = dir / "build-windows";
#else
    const fs::path fallback = dir / "build-linux";
#endif
    fs::create_directories(fallback);
    std::ofstream(fallback / "CMakeCache.txt") << "# fallback\n";
    expect(host.findBuildDirectory((dir / "missing-configured").string()) &&
           samePath(host.buildDir, fallback), "configured path did not fall back");
    // A configured cache that disappears is re-scanned; the guest path follows.
    host.configuredBuildDir = (dir / "missing-configured").string();
    expect(host.findBuildDirectory() && samePath(host.buildDir, fallback) &&
           host.guestLibPath.rfind(host.buildDir, 0) == 0, "re-scan did not move the guest path");
    fs::remove(fallback / "CMakeCache.txt");
    errors.clear();
    expect(!host.findBuildDirectory((dir / "missing-configured").string()), "accepted missing caches");
    expect(errors.size() == 1 && errors[0].find(dir.string()) != std::string::npos,
           "missing build directory did not log one Error naming project root");
    for (const auto& candidate : host.buildDirsTried) {
        expect(errors.size() == 1 && errors[0].find(candidate) != std::string::npos,
               "missing build directory omitted a tried directory");
    }
    // A cache removed after initialization is caught before launching cmake.
    // The re-scan then finds no candidate and reports every directory it tried.
    host.configuredBuildDir = configured.string();
    host.buildDir = configured.string();
    fs::remove(configured / "CMakeCache.txt");
    errors.clear();
    expect(!host.reload() && host.getApp() == runningApp, "missing cache replaced running guest");
    expect(errors.size() == 1 && errors[0].find(configured.string()) != std::string::npos &&
           errors[0].find(fallback.lexically_normal().string()) != std::string::npos,
           "missing cache reload did not re-scan and log one Error");
    expect(!fs::exists(configured / "build-command-reached"), "missing cache launched build command");
    listener.disconnect();
    host.guest.app.reset();
    fs::remove_all(dir);
    if (rc == 0) std::printf("hotReloadLifecycle: rebuild preflight ok\n");
    return rc;
}

// --reload-check: the real windowed host, reloading the guest by itself from
// the host's afterFrame (#416). Input goes in the way _event_cb hands it on:
// the raw sokol event first (ImGui follows the cursor through rawEvent), then
// the press / release through the core events, whose `consumed` tells whether
// a listener before the App took it. Returns 0 when every generation passed.
static int g_reloadCheckResult = 65;
static bool g_reloadCheckFinished = false;
static std::string g_reloadCheckStopReason = "host stopped before completing the checks";

static void reloadCheckExit() {
    if (!g_reloadCheckFinished) {
        std::printf("hotReloadLifecycle --reload-check: FAIL - %s\n",
                    g_reloadCheckStopReason.c_str());
        std::fflush(stdout);
        std::_Exit(65);
    }
#ifdef __APPLE__
    // sapp_quit() exits with 0 on macOS instead of returning to main().
    std::_Exit(g_reloadCheckResult);
#endif
}

static int runReloadCheck() {
    // Register before entering the host: initialization can quit before the
    // first frame, and macOS never returns from TC_RUN_APP, even on failure.
    std::atexit(reloadCheckExit);
    getMainThreadId();
    EventListener errors = getLogger().onLog.listen([](LogEventArgs& e) {
        // Host preflight runs on the main thread; ignore worker-thread logs.
        if (isMainThread() && e.level == LogLevel::Error &&
            g_reloadCheckStopReason == "host stopped before completing the checks") {
            g_reloadCheckStopReason = e.message;
        }
    });
    const float kPanelX = 100, kPanelY = 100;    // inside the inspector's Hierarchy panel
    const float kAwayX = 900, kAwayY = 550;      // no panel there
    const int kGenerations = 3;
    const int kFramesPerGeneration = 12;

    auto hostListenerCounts = [] {
        auto& ev = events();
        return std::vector<size_t>{ev.onRender.listenerCount(),      ev.rawEvent.listenerCount(),
                                   ev.mousePressed.listenerCount(),  ev.mouseReleased.listenerCount(),
                                   ev.mouseMoved.listenerCount(),    ev.mouseDragged.listenerCount(),
                                   ev.mouseScrolled.listenerCount(), ev.keyPressed.listenerCount(),
                                   ev.keyReleased.listenerCount(),   ev.afterFrame.listenerCount(),
                                   ev.exit.listenerCount(),          ev.hotReloadUnload.listenerCount()};
    };
    auto countsText = [](const std::vector<size_t>& c) {
        std::string t;
        for (size_t v : c) t += (t.empty() ? "" : " ") + std::to_string(v);
        return t;
    };
    auto moveMouse = [](float x, float y) {
        // Raw sokol events are in framebuffer pixels (sokol_imgui divides by
        // the DPI scale), so a logical point is scaled up first.
        sapp_event ev = {};
        ev.type = SAPP_EVENTTYPE_MOUSE_MOVE;
        ev.mouse_x = x * sapp_dpi_scale();
        ev.mouse_y = y * sapp_dpi_scale();
        events().rawEvent.notify(ev);
    };
    auto pressTaken = [](float x, float y) {
        MouseEventArgs press;
        press.x = x;
        press.y = y;
        press.pos = press.globalPos = Vec2(x, y);
        press.button = MOUSE_BUTTON_LEFT;
        events().mousePressed.notify(press);
        MouseEventArgs release = press;
        events().mouseReleased.notify(release);
        return press.consumed;
    };

    std::vector<size_t> firstCounts;
    int frame = 0;
    int generation = 1;
    auto finish = [](int rc, const std::string& what) {
        if (rc != 0) std::printf("hotReloadLifecycle --reload-check: FAIL - %s\n", what.c_str());
        else std::printf("hotReloadLifecycle --reload-check: OK\n");
        std::fflush(stdout);
        g_reloadCheckResult = rc;
        g_reloadCheckFinished = true;
        sapp_quit();
    };
    bool done = false;
    EventListener step = events().afterFrame.listen([&] {
        if (done) return;
        const int f = ++frame - (generation - 1) * kFramesPerGeneration;
        const std::string gen = "generation " + std::to_string(generation);
        if (frame > kGenerations * kFramesPerGeneration + 60) {
            done = true;
            return finish(60, "timed out");
        }
        if (f == 3) {
            // Both Debug and Release guests may exist. Every generation must
            // load the host's configuration, including after a rebuild (#794).
            const auto& host = trussc::hot_reload::g_host;
            if (host.reloadCount != generation - 1) {
                done = true;
                return finish(70, gen + ": deferred reload did not complete at the boundary");
            }
            const fs::path expectedGuest = findGuestLibrary();
            if (fs::path(host.guestLibPath) != expectedGuest ||
                fs::path(host.guest.loadedPath).parent_path() != expectedGuest.parent_path()) {
                done = true;
                return finish(66, gen + ": guest configuration mismatch: " + host.guest.loadedPath);
            }
            std::printf("hotReloadLifecycle --reload-check: %s loaded %s (host config %s)\n",
                        gen.c_str(), host.guest.loadedPath.c_str(), TC_HOT_RELOAD_CONFIG);
            const std::vector<size_t> counts = hostListenerCounts();
            if (generation == 1) {
                firstCounts = counts;
                std::printf("hotReloadLifecycle --reload-check: host listener counts %s\n",
                            countsText(counts).c_str());
            } else if (counts != firstCounts) {
                done = true;
                return finish(61, gen + " has host listener counts " + countsText(counts) +
                                  ", the first had " + countsText(firstCounts) +
                                  " (onRender rawEvent mousePressed mouseReleased mouseMoved mouseDragged "
                                  "mouseScrolled keyPressed keyReleased afterFrame exit hotReloadUnload)");
            }
            moveMouse(kAwayX, kAwayY);
        } else if (f == 6) {
            if (pressTaken(kAwayX, kAwayY)) {
                done = true;
                return finish(62, gen + ": a press away from the panels was taken before the App");
            }
            moveMouse(kPanelX, kPanelY);
        } else if (f == 9) {
            if (!pressTaken(kPanelX, kPanelY)) {
                done = true;
                return finish(63, gen + ": a press over the inspector's panel was not taken by ImGui");
            }
        } else if (f == kFramesPerGeneration) {
            std::printf("hotReloadLifecycle --reload-check: %s ok\n", gen.c_str());
            std::fflush(stdout);
            if (generation == kGenerations) {
                done = true;
                return finish(0, "");
            }
            // Reload while the cursor is over the panel: a manager left
            // behind would keep taking presses everywhere.
            if (!trussc::hot_reload::g_host.reload()) {
                done = true;
                return finish(64, "Host::reload() failed after " + gen);
            }
            ++generation;
        }
    });

    // Registered before host startup: explicit-100 precedes the host App slot.
    std::string frameOrder;
    std::vector<EventListener> frameStages;
    for (auto* event : {&events().update, &events().draw}) {
        auto stage = [&](char marker, int priority) {
            frameStages.push_back(event->listen([&, marker] { frameOrder += marker; }, priority));
        };
        stage('c', -20); stage('d', 0); stage('p', 50); stage('b', 100);
        stage('z', EventPriority::AfterApp);
    }
    std::string touchOrder;
    auto touchStage = [&](char stage, int priority) {
        return events().touchPressed.listen([&, stage](auto&) { touchOrder += stage; }, priority);
    };
    auto touchConsumer = touchStage('c', -20);
    auto touchDefault = touchStage('d', 0);
    auto touchMiddle = touchStage('p', 50);
    auto touchTie = touchStage('b', 100);
    auto touchAfter = touchStage('z', EventPriority::AfterApp);
    auto touchCheck = events().afterFrame.listen([&] {
        if (done) return;
        auto* app = static_cast<tcApp*>(trussc::hot_reload::g_host.getApp());
        if (frame % kFramesPerGeneration == 4) {
            app->frameOrder = nullptr;
            if (frameOrder != "cdpbatlzcdpbatlz") {
                done = true;
                return finish(69, "host update/draw App/tree order failed: " + frameOrder);
            }
        }
        if (frame % kFramesPerGeneration != 3) return;
        frameOrder.clear(); app->frameOrder = &frameOrder;
        app->touchOrder = &touchOrder;
        touchOrder.clear();
        setTouchAsMouse(false);
        auto sendTouch = [](sapp_event_type type) {
            sapp_event event{};
            event.type = type; event.num_touches = 1;
            event.touches[0].identifier = 42; event.touches[0].changed = true;
            internal::_event_cb(&event);
        };
        sendTouch(SAPP_EVENTTYPE_TOUCHES_BEGAN);
        sendTouch(SAPP_EVENTTYPE_TOUCHES_MOVED);
        sendTouch(SAPP_EVENTTYPE_TOUCHES_ENDED);
        app->touchOrder = nullptr;
        setTouchAsMouse(true);
        if (touchOrder != "cdpbalz" || app->touchPresses != 1 ||
            app->touchMoves != 1 || app->touchReleases != 1) {
            done = true;
            return finish(67, "host touch order/forwarding failed: " + touchOrder);
        }
        // Protected host binding survives clear, without rebinding on reload.
        events().touchMoved.clear();
        sendTouch(SAPP_EVENTTYPE_TOUCHES_MOVED);
        if (app->touchMoves != 2) {
            done = true;
            return finish(68, "host touch binding lost after clear");
        }
    });

    WindowSettings settings;
    settings.setSize(960, 600);
    // runapp-verdict: atexit-verdict -- finish sets the result in-frame; macOS atexit propagates it.
    TC_RUN_APP(tcApp, settings);
    return done ? g_reloadCheckResult : 65;
}

// ---------------------------------------------------------------------------
// App lifetime / registration ownership checks (#239 PR-A review round 2).
// Headless: in-process guest factories and the real GuestLibrary, main Window
// and registry; no native window. Run in the default mode, standalone as
// --lifetime-checks (CI display sweep) and under ASan by the review harness.
// ---------------------------------------------------------------------------
namespace lifetime {

int failures = 0;
void check(const char* label, bool ok) {
    std::printf("hotReloadLifecycle: lifetime: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

bool hasStatusImageNamed(const char* name) {
    for (auto& e : mcp::internal::statusImageRegistry()) if (e.name == name) return true;
    return false;
}
bool hasStatusNamed(const char* name) {
    for (auto& e : mcp::internal::statusRegistry()) if (e.name == name) return true;
    return false;
}

// A capture whose destruction re-enters registration retirement, the way the
// last strong reference to an App does through the guest deleter's old path.
struct Reenter {
    explicit Reenter(const void* o) : owner(o) {}
    Reenter(const Reenter&) = delete;
    const void* owner;
    ~Reenter() { mcp::internal::removeRegistrationsOwnedBy(owner); }
};

struct GuestApp : App {
    bool cleaned = false;
    void cleanup() override { cleaned = true; }
};
App* createGuestApp() { return new GuestApp; }
void destroyGuestApp(App* app) { delete app; }

int newToolCalls = 0, keeperCalls = 0;
struct NextScene : App {
    NextScene() {
        mcp::tool("life_new_tool", "replacement tool").bind([] { ++newToolCalls; return "new"; });
        mcp::status("life_new_status", std::function<double()>([] { return 1.0; }));
    }
};

std::string callTool(const char* name) {
    return mcp::internal::Server::instance().processMessage(
        std::string(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")") + name +
        R"(","arguments":{}}})");
}

// A tools/call that ran the handler: the reply parses, carries no JSON-RPC
// "error" and no tool error, and has result content. `text` is that content.
struct CallResult {
    bool ok = false;
    std::string text;
};
CallResult callToolResult(const char* name) {
    CallResult out;
    json reply = json::parse(callTool(name), nullptr, false);
    if (reply.is_discarded() || !reply.is_object() || reply.contains("error") || !reply.contains("result")) return out;
    const json& result = reply["result"];
    if (result.value("isError", false)) return out;
    if (!result.contains("content") || !result["content"].is_array() || result["content"].empty()) return out;
    out.text = result["content"][0].value("text", "");
    out.ok = true;
    return out;
}

void openMain() {
    internal::createMainWindow(WindowSettings{});
    internal::prepareMainAppBindings();
    internal::WindowRequestAccess::openMain(*internal::mainWindow());
}

void swapMain(std::shared_ptr<App> next) {
    internal::mainWindow()->setApp(std::move(next));
    internal::applyPendingAppForTests(*internal::mainWindow());
}

// #1: registration retirement is reentrancy-safe for tools and resources.
void recursiveRetirement() {
    openMain();
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createGuestApp;
    lib.destroyApp = destroyGuestApp;
    lib.create();
    const void* owner = lib.mcpOwner;
    // One re-entering capture per registry (it dies with its only holder), and
    // entries that sort after it, so a nested retirement erases the very
    // entries the outer iteration would visit next.
    mcp::tool("life_a_reenter", "capture re-enters retirement")
        .bind([r = std::make_shared<Reenter>(owner)] { return 1; });
    mcp::tool("life_b_sibling", "erased by the nested retirement").bind([] { return 2; });
    mcp::tool("life_c_sibling", "erased by the nested retirement").bind([] { return 3; });
    mcp::resource("life://a", "a").bind([r = std::make_shared<Reenter>(owner)] { return std::string("a"); });
    mcp::resource("life://b", "b").bind([] { return std::string("b"); });
    lib.destroy();  // must not touch a freed map node / vector slot
    check("reentrant retirement removes every tool", !mcp::hasTool("life_a_reenter") &&
          !mcp::hasTool("life_b_sibling") && !mcp::hasTool("life_c_sibling"));
    check("reentrant retirement removes every resource",
          mcp::internal::Server::instance().processMessage(
              R"({"jsonrpc":"2.0","id":1,"method":"resources/list"})").find("life://") == std::string::npos);
    check("reentrant retirement leaves no tool answering",
          callTool("life_a_reenter").find("Tool not found") != std::string::npos);
    internal::destroyMainWindow();
}

// #1: the same for the status registry (a vector). Alone, so the nested
// retirement is the status hook's own, not one started from a tool's capture.
void recursiveStatusRetirement() {
    openMain();
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createGuestApp;
    lib.destroyApp = destroyGuestApp;
    lib.create();
    const void* owner = lib.mcpOwner;
    // The status registry is a vector in registration order: the re-entering
    // getter goes last, and its siblings own heap captures, so an in-place
    // erase would destroy them twice.
    mcp::status("life_a_status", std::function<double()>([h = std::make_shared<int>(1)] { return double(*h); }));
    mcp::status("life_b_status", std::function<double()>([h = std::make_shared<int>(2)] { return double(*h); }));
    mcp::status("life_z_status", std::function<double()>([r = std::make_shared<Reenter>(owner)] { return 3.0; }));
    lib.destroy();
    check("reentrant retirement removes every status entry", !hasStatusNamed("life_a_status") &&
          !hasStatusNamed("life_b_status") && !hasStatusNamed("life_z_status"));
    internal::destroyMainWindow();
}

// #1 (round 3): a capture destroyed during retirement registers again. The
// retiring owner refuses it (with a Warning naming it), so nothing it publishes
// outlives the App it captures; a later tools/call must not reach the freed App.
struct GuestWithValue : App { int value = 239; };
App* createGuestWithValue() { return new GuestWithValue; }
struct PublishOnDrop {
    std::function<void()> publish;
    explicit PublishOnDrop(std::function<void()> f) : publish(std::move(f)) {}
    ~PublishOnDrop() { publish(); }
};
bool resourceListed(const char* uri) {
    return mcp::internal::Server::instance().processMessage(
        R"({"jsonrpc":"2.0","id":1,"method":"resources/list"})").find(uri) != std::string::npos;
}

void lateRegistration() {
    openMain();
    std::vector<std::string> warnings;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Warning) warnings.push_back(e.message);
    });
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createGuestWithValue;
    lib.destroyApp = destroyGuestApp;
    auto* app = static_cast<GuestWithValue*>(lib.create());
    // Absence is recorded inside the publishing destructor, right after the
    // attempt and before the retirement sweeps resume: a registration that was
    // accepted and removed later by a sweep would still be seen here.
    std::map<std::string, bool> absentAtOnce;
    auto lateTool = [&absentAtOnce, app](const char* name) {
        mcp::tool(name, "published by a retiring capture").bind([app] { return app->value; });
        absentAtOnce[name] = !mcp::hasTool(name);
    };
    // resource -> tool
    mcp::resource("life://late", "late").bind([c = std::make_shared<PublishOnDrop>([=] {
        lateTool("life_late_tool_r");
    })] { return std::string("r"); });
    // status -> tool, resource and status image
    mcp::status("life_late_status_s", std::function<double()>([&absentAtOnce, c = std::make_shared<PublishOnDrop>([=, &absentAtOnce] {
        lateTool("life_late_tool_s");
        mcp::resource("life://late-s", "late-s").bind([app] { return std::to_string(app->value); });
        absentAtOnce["life://late-s"] = !resourceListed("life://late-s");
        mcp::statusImage("life_late_image_s", [app] { return Pixels(); });
        absentAtOnce["life_late_image_s"] = !hasStatusImageNamed("life_late_image_s");
    })] { return 1.0; }));
    // status image -> status (its container was already processed)
    mcp::statusImage("life_late_image", [&absentAtOnce, c = std::make_shared<PublishOnDrop>([&absentAtOnce] {
        mcp::status("life_late_status_i", std::function<double()>([] { return 2.0; }));
        absentAtOnce["life_late_status_i"] = !hasStatusNamed("life_late_status_i");
    })] { return Pixels(); });
    const void* retiringOwner = lib.mcpOwner;
    lib.destroy();
    auto warned = [&](const char* what) {
        for (auto& w : warnings)
            if (w.find(what) != std::string::npos && w.find("unloading") != std::string::npos) return true;
        return false;
    };
    for (const char* name : {"life_late_tool_r", "life_late_tool_s", "life://late-s", "life_late_image_s", "life_late_status_i"}) {
        auto it = absentAtOnce.find(name);
        check((std::string("late registration: refused at once: ") + name).c_str(), it != absentAtOnce.end() && it->second);
    }
    check("late registration: nothing remains after unload", !mcp::hasTool("life_late_tool_r") &&
          !mcp::hasTool("life_late_tool_s") && !resourceListed("life://late") &&
          !hasStatusNamed("life_late_status_i") && !hasStatusImageNamed("life_late_image_s"));
    check("late registration: each refusal warned and named",
          warned("life_late_tool_r") && warned("life_late_tool_s") && warned("life://late-s") && warned("life_late_image_s") && warned("life_late_status_i"));
    check("late registration: retirement finished and cleared the owner", lib.mcpOwner == nullptr &&
          !mcp::internal::ownerRetiring(retiringOwner));
    // Real GuestLibrary path: the App is gone; a call must be "Tool not found", not a callback.
    check("late registration: tools/call after unload is Tool not found",
          callTool("life_late_tool_r").find("Tool not found") != std::string::npos);
    internal::destroyMainWindow();
}

// #1 (round 4): a log listener that registers (a warning counter published as a
// status) must not turn the refusal Warning into unbounded recursion. The nested
// registration is refused and counted, the Warning plus one summary are logged,
// and retirement returns. The listener stops itself after kCap deliveries so a
// broken guard fails the check instead of overflowing the stack.
void lateFeedback() {
    openMain();
    constexpr int kCap = 64;
    int deliveries = 0;
    bool nestedPublished = false;
    std::vector<std::string> warnings;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level != LogLevel::Warning) return;
        warnings.push_back(e.message);
        if (++deliveries > kCap) return;
        mcp::status("life_feedback_counter", std::function<double()>([] { return 1.0; }));
        nestedPublished |= hasStatusNamed("life_feedback_counter");
    });
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createGuestWithValue;
    lib.destroyApp = destroyGuestApp;
    auto* app = static_cast<GuestWithValue*>(lib.create());
    mcp::resource("life://feedback", "feedback").bind([c = std::make_shared<PublishOnDrop>([app] {
        mcp::tool("life_feedback_tool", "late").bind([app] { return app->value; });
    })] { return std::string("f"); });
    lib.destroy();   // must return, not recurse
    auto has = [&](const char* a, const char* b) {
        for (auto& w : warnings) if (w.find(a) != std::string::npos && w.find(b) != std::string::npos) return true;
        return false;
    };
    check("late feedback: retirement returned within the listener cap", deliveries <= kCap);
    check("late feedback: the listener's registration was refused", !nestedPublished && !hasStatusNamed("life_feedback_counter"));
    check("late feedback: the late tool was refused", !mcp::hasTool("life_feedback_tool"));
    check("late feedback: outer Warning plus one summary, no more",
          warnings.size() == 2 && has("life_feedback_tool", "unloading") && has("1 more registrations were refused", "unloading"));
    internal::destroyMainWindow();
}

// #1: an App kept alive past its swap by a tool capture; generation unload
// retires the tool, which releases the App (and its deleter) with no recursion.
void retainedAppRetirement() {
    openMain();
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createGuestApp;
    lib.destroyApp = destroyGuestApp;
    lib.create();
    std::weak_ptr<App> old;
    {
        auto app = internal::mainApp();
        old = app;
        mcp::tool("life_keeper", "retains the old App").bind([app] { ++keeperCalls; return 1; });
        mcp::status("life_keeper_status", std::function<double()>([app] { return 1.0; }));
    }
    swapMain(std::make_shared<App>());
    auto* oldGuest = static_cast<GuestApp*>(old.lock().get());
    check("swap cleaned the outgoing App once while a capture retains it", oldGuest && oldGuest->cleaned);
    // Decision: registrations belong to the generation, so the tool stays
    // listed until the generation unloads, and the retained App is memory-safe.
    check("generation tool outlives its App's swap", mcp::hasTool("life_keeper") && !old.expired());
    keeperCalls = 0;
    check("retained App tool is callable memory-safely", callToolResult("life_keeper").ok && keeperCalls == 1);
    lib.destroy();
    check("unload retires the tool and releases the App", !mcp::hasTool("life_keeper") &&
          !hasStatusNamed("life_keeper_status") && old.expired());
    internal::destroyMainWindow();
}

// #3: the replacement keeps its registrations across the swap; unload removes them.
void replacementKeepsRegistrations() {
    openMain();
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createGuestApp;
    lib.destroyApp = destroyGuestApp;
    lib.create();
    mcp::tool("life_old_tool", "original App tool").bind([] { return "old"; });
    auto next = std::make_shared<NextScene>();
    check("replacement registered before the swap", mcp::hasTool("life_new_tool") && hasStatusNamed("life_new_status"));
    std::weak_ptr<App> old = internal::mainApp();
    swapMain(next);  // the Window releases the original guest App here
    check("replacement attached and original App destroyed", next->getWindow() && old.expired());
    check("replacement tool survives the original App's destruction", mcp::hasTool("life_new_tool"));
    check("replacement status survives the original App's destruction", hasStatusNamed("life_new_status"));
    newToolCalls = 0;
    check("a missing tool is not a successful call", !callToolResult("life_missing_tool").ok);
    {
        const CallResult call = callToolResult("life_new_tool");
        check("replacement tool answers", call.ok && newToolCalls == 1 && call.text.find("new") != std::string::npos);
    }
    lib.destroy();
    check("unload removes the whole generation", !mcp::hasTool("life_new_tool") &&
          !mcp::hasTool("life_old_tool") && !hasStatusNamed("life_new_status"));
    internal::destroyMainWindow();
}

// #2: windowResized() requests a swap and pumps native events inside the callback.
int callbackSwaps = 0, cleanupInCallback = 0;
bool inResize = false, dispatchActiveInResize = false, attachedAfterPump = false;
int marker = 0;
struct ResizeApp : App {
    int member = 123;
    void windowResized(int, int) override {
        inResize = true;
        getWindow()->setApp(std::make_shared<App>());
        dispatchActiveInResize = internal::mainWindowDispatchActive();  // reload() defers on exactly this
        sapp_event ev{};
        ev.type = SAPP_EVENTTYPE_FOCUSED;
        internal::_event_cb(&ev);  // synchronous OS reentry (modal / native call)
        attachedAfterPump = getWindow() != nullptr;
        marker = member;  // reads the receiver after the nested event
        ++callbackSwaps;
        inResize = false;
    }
    void cleanup() override { if (inResize) ++cleanupInCallback; }
};
App* createResizeApp() { return new ResizeApp; }

void resetResizeState() {
    callbackSwaps = cleanupInCallback = marker = 0;
    inResize = dispatchActiveInResize = attachedAfterPump = false;
}

void startupResize() {
    resetResizeState();
    openMain();
    auto app = std::make_shared<ResizeApp>();
    internal::attachMainApp(app);
    internal::resizeMainApp(128, 96);  // startup / native resize delivery
    check("startup resize: callback ran once with the receiver intact", callbackSwaps == 1 && marker == 123);
    check("startup resize: dispatch active, reload defers", dispatchActiveInResize);
    check("startup resize: App not cleaned up mid-callback", cleanupInCallback == 0 && attachedAfterPump);
    check("startup resize: swap applied at the boundary", internal::mainApp() && internal::mainApp() != app && !app->getWindow());
    internal::destroyMainWindow();
}

void reloadResize() {
    resetResizeState();
    openMain();
    trussc::hot_reload::GuestLibrary lib;
    lib.createApp = createResizeApp;
    lib.destroyApp = destroyGuestApp;
    lib.create();  // the reload attaches the new guest App, then seeds its size
    trussc::hot_reload::seedReloadedAppSize(128, 96, 1.0f);
    check("reload seed: callback ran once with the receiver intact", callbackSwaps == 1 && marker == 123);
    check("reload seed: dispatch active, reload defers", dispatchActiveInResize);
    check("reload seed: App not cleaned up mid-callback", cleanupInCallback == 0 && attachedAfterPump);
    check("reload seed: swap applied at the boundary", internal::mainApp() && !lib.app.lock());
    lib.destroy();
    internal::destroyMainWindow();
}

// `only` selects one check by name (recursive, retained, replacement, startup,
// reload); empty runs all. Lets the ASan review harness show each in isolation.
int run(const std::string& only = "") {
    getMainThreadId();
    failures = 0;
    // tools/call is health-only until the host opts in; lift that for the checks
    // without starting a server or changing the granted level.
    const bool previousHealthOnly = mcp::internal::healthOnlyEndpoint();
    mcp::internal::healthOnlyEndpoint() = false;
    if (only.empty() || only == "recursive") { recursiveRetirement(); recursiveStatusRetirement(); }
    if (only.empty() || only == "late") { lateRegistration(); lateFeedback(); }
    if (only.empty() || only == "retained") retainedAppRetirement();
    if (only.empty() || only == "replacement") replacementKeepsRegistrations();
    if (only.empty() || only == "startup") startupResize();
    if (only.empty() || only == "reload") reloadResize();
    mcp::internal::healthOnlyEndpoint() = previousHealthOnly;
    std::printf("hotReloadLifecycle: lifetime checks: %s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}

} // namespace lifetime

int main(int argc, char** argv) {
    if (argc > 2 && std::string(argv[1]) == "--build") {
        std::ofstream(fs::path(argv[2]) / "build-command-reached") << "mock cmake build\n";
        return 0;
    }
    if (argc > 1 && std::string(argv[1]) == "--app") {
        WindowSettings settings;
        settings.setSize(960, 600);
        return TC_RUN_APP(tcApp, settings);
    }
    if (argc > 1 && std::string(argv[1]) == "--reload-check") return runReloadCheck();
    if (argc > 1 && std::string(argv[1]) == "--lifetime-checks") return lifetime::run(argc > 2 ? argv[2] : "");
    if (argc > 1 && std::string(argv[1]) == "--guest-process-exit") {
        getMainThreadId();
        static UnloadProbe probe;
        std::atexit([] {
            const bool ok = probe.exits == 1 && probe.cleanups == 1 && probe.audioDetachedInCleanup;
            std::printf("hotReloadLifecycle: guest process teardown: %s\n", ok ? "PASS" : "FAIL");
            std::fflush(stdout);
            if (!ok) std::_Exit(1);
        });
        internal::createMainWindow(WindowSettings{});
        internal::prepareMainAppBindings();
        internal::WindowRequestAccess::openMain(*internal::mainWindow());
        trussc::hot_reload::GuestLibrary lib;
        if (!lib.load(findGuestLibrary())) return 72;
        auto* app = static_cast<tcApp*>(lib.create());
        if (!app) return 72;
        app->cycleOnly = true;
        app->unloadProbe = &probe;
        app->handleUpdate(0, 0);
        std::exit(0);
    }

    // Record the main thread id first, as _setup_cb does: isMainThread() and
    // runOnMainThread() key off whichever thread asks first.
    getMainThreadId();

    if (int rc = lifetime::run()) return rc;
    if (int rc = checkWatcherExtensions()) return rc;
    if (int rc = checkLoadFailuresLogged()) return rc;
    if (int rc = checkRebuildPreflight()) return rc;

    std::string guestPath = findGuestLibrary();
    if (guestPath.empty()) {
        std::printf("hotReloadLifecycle: FAIL - guest library not found\n");
        return 1;
    }
    std::printf("hotReloadLifecycle: guest = %s\n", guestPath.c_str());

    // Host-owned registration: must survive every reload.
    mcp::statusGraph("host_status", []() { return 42.0; });
    mcp::statusImage("host_image", []() { return Pixels(); });
    mcp::tool("host_probe", "hotReloadLifecycle host tool")
        .bind(std::function<json()>([]() -> json { return json{{"ok", true}}; }));
    // A host tool that defers: its reply must survive a guest's unload.
    mcp::tool("host_deferred", "hotReloadLifecycle host tool answering after the frame")
        .bind(std::function<json()>([]() -> json {
            mcp::deferToolResultUntilAfterFrame([]() -> json { return json{{"deferred", true}, {"host", true}}; });
            return json(nullptr);  // replaced by the deferred result
        }));
    // Grant inspection access and start the test server explicitly.
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::startHttpServer();
    int port = 0;
    for (int i = 0; i < 500 && port <= 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        port = mcp::getHttpPort();
    }
    int rc = 10;
    if (port <= 0) {
        std::printf("hotReloadLifecycle: FAIL - MCP HTTP server did not start\n");
    } else {
        auto keyListener = events().keyPressed.listen([](const KeyEventArgs&) { g_hostKeyPresses++; });
        rc = runCycles(guestPath, port);
    }
    mcp::stopHttpServer();
    std::fflush(stdout);
    if (rc != 0) return rc;

    // Success is also surviving process exit: on Linux the pre-fix regression
    // fired in exit-time finalizers (SIGSEGV after main returned), which the
    // harness sees as a non-zero exit.
    std::printf("hotReloadLifecycle: OK\n");
    return 0;
}
