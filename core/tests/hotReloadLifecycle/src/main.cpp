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
// The App base constructor/destructor traverses exactly that surface
// (AudioEngine listener auto-subscribe, Event COW list churn), so the test is
// just the real GuestLibrary driven through load -> create -> destroy ->
// unload cycles. The guest binary is identical each cycle — the bugs depend on
// image lifetime, not on the code changing — and GuestLibrary already loads
// each cycle from a fresh unique temp copy, exactly like a real reload.
//
// MCP registrations (#227, #249): the guest App registers tools, a status
// entry and a status image capturing `this`, plus the control tools. Each
// cycle checks they are visible while the guest lives and gone after unload,
// while a tool the host registered itself stays. The registry must be the
// host's on every platform: on Windows the guest is a DLL that compiles its own
// copy of any header-inline state, so the MCP state is defined non-inline in
// TrussC.lib (tcMCP.cpp) and the guest imports it from the host.
//
// Each cycle also goes through the MCP HTTP server, the way a client reaches a
// running app (the requests are answered here by pumping processHttpQueue(),
// as the frame loop does): tools/list, a call to the guest's tool that must be
// answered by THIS generation, a guest tool that defers its reply (the
// deferral state must be shared too), the host's tc_get_status reading the
// guest's status entry, and a control tool the guest registered, which must
// inject into the host's input dispatch. The unload itself happens while
// deferred replies wait for the afterFrame drain, as a reload does in a real
// frame: those whose producers run guest code (guest_deferred, which reaches
// the App through `this`, and tc_get_status_image on the guest's getter) must
// come back as an error, and a host tool's deferred reply must still answer.
// After unload the guest tool must be gone from the server as well, and so
// must the browser origin guest code allowed (the host's stays).
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
// Node references (#255), on every platform: the host makes the guest's App
// the main window's root (getRootNode(), a weak reference), and before it
// unloads the guest it resets the weak references every window context keeps
// to nodes (hover, grab, selection, the main root), which here name a node
// guest code made.
//
// Exit code: 0 = survived all cycles (including process exit), non-zero or a
// crash = regression. The test itself never enters TC_RUN_APP: no sokol loop,
// no GPU, no file watcher, no cmake rebuild — CI-safe on every desktop
// platform.
//
// `hotReloadLifecycle --app` instead runs the real hot reload host
// (TC_RUN_APP): a window, the file watcher and rebuilds, with the guest's
// tcxImGui panel — edit src/tcApp.cpp while it runs to try a reload by hand.
// CI only builds that path, which still links the host loop and the guest's
// addon code on every desktop platform.
// =============================================================================

#include "tcApp.h"

#ifndef TC_HOT_RELOAD_BUILD
// The cmake source scan keys the host/guest split off TC_HOT_RELOAD appearing
// in a .cpp: tcApp.cpp carries the real macro; this reference is inside an
// #ifndef so it never compiles, but keeps the intent greppable.
#error "hotReloadLifecycle must be configured as a hot reload (host/guest) build"
#endif

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
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
        // Same name the server bound to: "localhost" resolves to ::1 first on
        // some hosts (CI runners), where 127.0.0.1 would find nothing.
        httplib::Client cli("localhost", port);
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
        if (!hooked && mcp::detail::deferredResponses().size() >= deferred) {
            beforeDrain();
            hooked = true;
        }
        if (hooked) mcp::drainDeferredResponses();
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
    const char* configs[] = {"", "Release", "Debug", "RelWithDebInfo"};
    for (const char* b : buildDirs) {
        for (const char* c : configs) {
            fs::path p = proj / b;
            if (*c) p /= c;
            p /= guestName;
            if (fs::exists(p)) return p.string();
        }
    }
    return "";
}

// The load -> create -> check -> unload cycles. Returns 0 or the failing
// check's exit code (after unloading the guest, so nothing it registered
// outlives its App while the caller shuts the server down).
static int runCycles(const std::string& guestPath, int port) {
    using namespace trussc::hot_reload;

    auto hasStatus = [](const char* name) {
        for (auto& e : mcp::detail::statusRegistry()) if (e.name == name) return true;
        return false;
    };
    auto hasStatusImage = [](const char* name) {
        for (auto& e : mcp::detail::statusImageRegistry()) if (e.name == name) return true;
        return false;
    };

    const int kCycles = 5;
    uint64_t prevAppId = 0;
    for (int i = 1; i <= kCycles; ++i) {
        GuestLibrary lib;
        auto fail = [&](int code, const std::string& what) {
            std::printf("hotReloadLifecycle: FAIL - %s (cycle %d)\n", what.c_str(), i);
            lib.unload();
            return code;
        };
        if (!lib.load(guestPath)) return fail(2, "load failed");
        // App construction walks the hazardous surface: first-touch of the
        // AudioEngine singleton, listener registration on host-owned Events.
        App* app = lib.create();
        if (!app) return fail(3, "create failed");
        // Node ids are unique per process: a new generation's nodes (its App
        // first) must not count from 0 again, as a Windows guest DLL's own
        // counter did.
        if (i > 1 && app->getInstanceId() <= prevAppId) {
            return fail(26, "the guest's node ids restarted instead of continuing the process-wide sequence");
        }
        prevAppId = app->getInstanceId();
        // The host makes the guest's App the main window's root
        // (getRootNode()), held weakly (#255): the App's constructor can't
        // register itself, weak_from_this() being empty until it returns.
        if (internal::mainWindowContext().rootNode.lock().get() != app) {
            return fail(38, "the guest's App is not the main window's root (getRootNode())");
        }
        if (!(mcp::hasTool("guest_probe") && hasStatus("guest_status") && hasStatusImage("guest_image"))) {
            return fail(4, "guest MCP registrations not visible to the host");
        }
        if (!mcp::detail::isAllowedOrigin(kGuestOrigin, port)) {
            return fail(37, "the browser origin guest code allowed with mcp::allowOrigin() is not allowed");
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
        if (status.is_object() && status["values"].is_array()) {
            for (auto& v : status["values"]) statusListed |= v.is_object() && v.value("name", "") == "guest_status";
        }
        if (!statusListed) return fail(11, "tc_get_status over HTTP does not report guest_status");
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
        setDefaultScreenFov(45.0f);
        setDataPathRoot("data");
        bitmapfont::internal::registry().clear();
        internal::overlayHoveredQuery() = nullptr;
        internal::overlayFocusedQuery() = nullptr;
        setBeepVolume(0.5f);
        {
            std::lock_guard<std::mutex> lock(mcp::detail::alertMutex());
            mcp::detail::alertQueue().clear();
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
        if (getNearClip() != 0.25f || getFarClip() != 750.0f || getDefaultScreenFov() != 30.0f) {
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
        setDefaultScreenFov(45.0f);
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
        const GuestView seen = guest->readSharedState();
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
            {in.audioEngine, &AudioEngine::getInstance(), "AudioEngine::getInstance()"},
            {in.screenRecorder, &internal::globalScreenRecorder(), "screen recorder"},
            {in.asyncScheduler, &internal::AsyncScheduler::get(), "AsyncScheduler"},
            {in.beepManager, &internal::getManager(), "beep manager"},
            {in.consoleRunning, &console::detail::isRunning(), "console state"},
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
            const bool guestSawRelease = !guest->seesAttached(sub.get());
            const bool attachedNew = guest->attachApp(second, reopened);
            second.setApp(nullptr);
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
        for (size_t k = 0; k < 2; k++) {
            json cancelled = toolContent(replies[k]);
            if (!cancelled.is_object() || cancelled.value("status", "") != "error" ||
                cancelled.value("message", "").find("unloaded") == std::string::npos) {
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
        if (mcp::hasTool("guest_probe") || mcp::hasTool("tc_key_press") ||
            hasStatus("guest_status") || hasStatusImage("guest_image")) {
            return fail(5, "the destroyed guest's MCP registrations are still listed");
        }
        if (mcp::detail::isAllowedOrigin(kGuestOrigin, port)) {
            return fail(37, "the browser origin the destroyed guest allowed is still allowed");
        }
        if (!mcp::detail::isAllowedOrigin(kHostOrigin, port)) {
            return fail(37, "unloading the guest removed a browser origin the host allowed");
        }
        std::string callError;
        if (!callTool(port, "guest_probe", json::object(), &callError).is_discarded() ||
            callError.find("Tool not found") == std::string::npos) {
            return fail(13, "the destroyed guest's tool still answers over HTTP");
        }
        if (!mcp::hasTool("host_probe")) return fail(6, "the host's own tool was removed");
        std::printf("hotReloadLifecycle: cycle %d/%d ok\n", i, kCycles);
    }
    return 0;
}

int main(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--app") {
        WindowSettings settings;
        settings.setSize(960, 600);
        return TC_RUN_APP(tcApp, settings);
    }

    // Record the main thread id first, as _setup_cb does: isMainThread() and
    // runOnMainThread() key off whichever thread asks first.
    getMainThreadId();

    std::string guestPath = findGuestLibrary();
    if (guestPath.empty()) {
        std::printf("hotReloadLifecycle: FAIL - guest library not found\n");
        return 1;
    }
    std::printf("hotReloadLifecycle: guest = %s\n", guestPath.c_str());

    // Host-owned registration: must survive every reload.
    mcp::tool("host_probe", "hotReloadLifecycle host tool")
        .bind(std::function<json()>([]() -> json { return json{{"ok", true}}; }));
    // A host tool that defers: its reply must survive a guest's unload.
    mcp::tool("host_deferred", "hotReloadLifecycle host tool answering after the frame")
        .bind(std::function<json()>([]() -> json {
            mcp::deferToolResultUntilAfterFrame([]() -> json { return json{{"deferred", true}, {"host", true}}; });
            return json(nullptr);  // replaced by the deferred result
        }));
    // What TRUSSC_MCP=1 gives a running app: the standard tools + the server.
    mcp::registerInspectionTools();
    // A browser origin the host allows: it must survive every unload, while
    // the one each guest generation allows (tcApp()) goes with it.
    mcp::allowOrigin(kHostOrigin);
    mcp::startHttpServer(0, "localhost");
    int port = 0;
    for (int i = 0; i < 500 && port <= 0; i++) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        port = mcp::getHttpPort();
    }
    int rc = 10;
    if (port <= 0) {
        std::printf("hotReloadLifecycle: FAIL - MCP HTTP server did not start\n");
    } else {
        internal::appKeyPressedFunc = [](const KeyEventArgs&) { g_hostKeyPresses++; };
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
