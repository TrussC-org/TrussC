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
// inject into the host's input dispatch. After unload the guest tool must be
// gone from the server as well.
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
#include <string>
#include <thread>

namespace fs = std::filesystem;

// POST one JSON-RPC request to the MCP server from a worker thread while this
// (main) thread plays the frame loop's part: processHttpQueue() answers it,
// drainDeferredResponses() is the afterFrame step deferred tools wait for.
// Returns the reply body, or "" on a transport failure / timeout.
static std::string mcpPost(int port, const std::string& body) {
    std::atomic<bool> done{false};
    std::string reply;
    std::thread worker([&] {
        // Same name the server bound to: "localhost" resolves to ::1 first on
        // some hosts (CI runners), where 127.0.0.1 would find nothing.
        httplib::Client cli("localhost", port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(10);
        auto r = cli.Post("/mcp", body, "application/json");
        if (r && r->status == 200) reply = r->body;
        done = true;
    });
    auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done && std::chrono::steady_clock::now() < deadline) {
        mcp::processHttpQueue();
        mcp::drainDeferredResponses();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!done) {
        worker.detach();
        return "";
    }
    worker.join();
    return reply;
}

// tools/call -> the tool's content json (the text block, parsed), or a
// discarded json on failure. `error` receives the JSON-RPC error message.
static json callTool(int port, const std::string& name, const json& args, std::string* error = nullptr) {
    static int id = 0;
    json req = {{"jsonrpc", "2.0"}, {"id", ++id}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", args}}}};
    json reply = json::parse(mcpPost(port, req.dump()), nullptr, false);
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
        if (!(mcp::hasTool("guest_probe") && hasStatus("guest_status") && hasStatusImage("guest_image"))) {
            return fail(4, "guest MCP registrations not visible to the host");
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
        callTool(port, "tc_key_release", json{{"key", 65}});
        if (pressed.is_discarded() || g_hostKeyPresses.load() != pressesBefore + 1) {
            return fail(12, "the guest-registered tc_key_press did not reach the host's key dispatch");
        }

        // Destruction + unload: listener removal churns the COW lists, and the
        // (pre-fix) dlclose here is what armed/triggered both crashes.
        lib.unload();
        if (mcp::hasTool("guest_probe") || mcp::hasTool("tc_key_press") ||
            hasStatus("guest_status") || hasStatusImage("guest_image")) {
            return fail(5, "the destroyed guest's MCP registrations are still listed");
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

    std::string guestPath = findGuestLibrary();
    if (guestPath.empty()) {
        std::printf("hotReloadLifecycle: FAIL - guest library not found\n");
        return 1;
    }
    std::printf("hotReloadLifecycle: guest = %s\n", guestPath.c_str());

    // Host-owned registration: must survive every reload.
    mcp::tool("host_probe", "hotReloadLifecycle host tool")
        .bind(std::function<json()>([]() -> json { return json{{"ok", true}}; }));
    // What TRUSSC_MCP=1 gives a running app: the standard tools + the server.
    mcp::registerInspectionTools();
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
