// =============================================================================
// core/tests/mcpOccludedWindow — behavioral regression test for the MCP
// screenshot tools and hidden secondary windows (#347).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// A secondary window that the OS reports hidden (minimized, fully covered)
// runs no tick, so a screenshot deferred to it could only time out after 5 s.
// Guards the invariant:
// - tc_list_windows reports Window::isOccluded() as "occluded" on each
//   secondary entry (the main window, which keeps rendering while hidden,
//   has no such field)
// - tc_get_screenshot / tc_save_screenshot fail at once, with a specific
//   error, for a window whose flag is set
// - a window whose flag is not set is unchanged: the request is deferred to
//   the window's own tick and, when no tick comes, answered by the 5 s
//   timeout
//
// A headless test has no native window the OS could cover or minimize, so
// the flag is driven through the test seam
// internal::windowOccludedHookForTests(), which Window::isOccluded() consults
// before the native flag. The native flags themselves (NSWindow
// occlusionState, WM_SIZE / DXGI_STATUS_OCCLUDED, X11 WM_STATE /
// VisibilityNotify) are checked by hand on each platform.
//
// The main thread pumps mcp::processHttpQueue() and the main window's
// mcp::drainDeferredResponses() the way the frame loop does; the requests run
// on worker threads with a deadline.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

// A Window made in a headless test has no native side: it is a closed window
// (isOpen() is false) and openWindows() skips it. OpenWindow stands in for an
// open one. Its native_ points at zeroed memory: the platform adapter reads
// it as its state, whose sapp_window handle is then 0, the invalid handle, so
// getWidth() / getHeight() report 0 and the native occluded flag is false.
// native_ is cleared before ~Window() would close() it.
alignas(16) static unsigned char g_nativeStandIn[256] = {};
struct OpenWindow : Window {
    explicit OpenWindow(const string& title) {
        title_ = title;
        native_ = g_nativeStandIn;
    }
    ~OpenWindow() { native_ = nullptr; }
};

static OpenWindow* g_hidden = nullptr;    // window 2 (window 1 stays visible)

// Called on the main thread (tool handlers run in processHttpQueue()).
static bool occludedHook(const Window& w) { return &w == g_hidden; }

static string toolCall(const string& name, const string& args) {
    return R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")" + name +
           R"(","arguments":)" + args + "}}";
}

// The tool's own JSON reply: result.content[0].text, parsed. Null on any
// other shape (transport error, timeout reply without text, ...).
static json toolReply(const httplib::Result& r) {
    if (!r || r->status != 200) return json();
    json body = json::parse(r->body, nullptr, false);
    if (body.is_discarded() || !body.contains("result")) return json();
    const json& content = body["result"]["content"];
    if (!content.is_array() || content.empty() || !content[0].contains("text")) return json();
    json j = json::parse(content[0]["text"].get<string>(), nullptr, false);
    return j.is_discarded() ? json() : j;
}

static string messageOf(const json& reply) {
    return reply.is_object() && reply.contains("message") && reply["message"].is_string()
        ? reply["message"].get<string>() : string();
}

static string statusOf(const json& reply) {
    return reply.is_object() && reply.contains("status") && reply["status"].is_string()
        ? reply["status"].get<string>() : string();
}

static bool contains(const string& s, const string& part) { return s.find(part) != string::npos; }

int main() {
    OpenWindow visible("visible"), hidden("hidden");
    g_hidden = &hidden;
    internal::windowOccludedHookForTests() = &occludedHook;

    mcp::registerInspectionTools();
    mcp::startHttpServer(0, "localhost");
    int port = 0;
    for (int i = 0; i < 500 && port <= 0; i++) {
        this_thread::sleep_for(chrono::milliseconds(10));
        port = mcp::getHttpPort();
    }
    check("server bound to a port", port > 0);
    if (port <= 0) return 1;

    const string savePath = (filesystem::temp_directory_path() /
                             ("tc_mcpOccludedWindow_" + to_string(port) + ".png")).string();
    const string savePathJson = json(savePath).dump();

    atomic<bool> done{false};
    thread worker([&] {
        // Same name the server bound to: "localhost" resolves to ::1 first on
        // some hosts (CI runners), where 127.0.0.1 would find nothing.
        auto makeClient = [port] {
            auto cli = make_unique<httplib::Client>("localhost", port);
            cli->set_connection_timeout(5);
            cli->set_read_timeout(15);
            return cli;
        };
        auto cli = makeClient();
        auto call = [](httplib::Client& c, const string& name, const string& args,
                       double* seconds = nullptr) {
            auto t0 = chrono::steady_clock::now();
            auto r = c.Post("/mcp", toolCall(name, args), "application/json");
            if (seconds) {
                *seconds = chrono::duration<double>(chrono::steady_clock::now() - t0).count();
            }
            return toolReply(r);
        };

        // --- tc_list_windows: the flag per secondary window ---------------
        json list = call(*cli, "tc_list_windows", "{}");
        const json wins = list.is_object() ? list.value("windows", json::array()) : json::array();
        check("tc_list_windows lists main + 2 secondary windows", wins.size() == 3, list.dump());
        if (wins.size() == 3) {
            check("main entry has no occluded field", !wins[0].contains("occluded"), wins[0].dump());
            check("visible window: occluded false",
                  wins[1].value("title", "") == "visible" && wins[1].contains("occluded") &&
                  wins[1]["occluded"] == false, wins[1].dump());
            check("hidden window: occluded true",
                  wins[2].value("title", "") == "hidden" && wins[2].contains("occluded") &&
                  wins[2]["occluded"] == true, wins[2].dump());
        }

        // --- hidden window: both screenshot tools fail at once ------------
        double sec = 0;
        json r = call(*cli, "tc_get_screenshot", R"({"window":2})", &sec);
        check("tc_get_screenshot hidden window -> error", statusOf(r) == "error", r.dump());
        check("  names the window and why",
              contains(messageOf(r), "window 2 is not visible (the OS reports it hidden"), r.dump());
        check("  answered at once (< 2 s, not the 5 s timeout)", sec < 2.0, to_string(sec) + " s");

        r = call(*cli, "tc_save_screenshot", R"({"window":2,"path":)" + savePathJson + "}", &sec);
        check("tc_save_screenshot hidden window -> error", statusOf(r) == "error", r.dump());
        check("  names the window and why",
              contains(messageOf(r), "window 2 is not visible (the OS reports it hidden"), r.dump());
        check("  answered at once (< 2 s, not the 5 s timeout)", sec < 2.0, to_string(sec) + " s");
        // No "no file written" check: headless, a secondary window's capture
        // has no framebuffer to read (0x0), so no file could appear either way.

        // Unchanged: an index past the open windows.
        r = call(*cli, "tc_get_screenshot", R"({"window":3})");
        check("out-of-range window -> index error", contains(messageOf(r), "out of range"), r.dump());

        // --- visible window: unchanged, deferred to its tick --------------
        // The stand-in never ticks, so the main window's drain answers both
        // requests with the timeout reply once 5 s have passed.
        json getReply, saveReply;
        double getSec = 0, saveSec = 0;
        thread saver([&] {
            auto c2 = makeClient();
            saveReply = call(*c2, "tc_save_screenshot",
                             R"({"window":1,"path":)" + savePathJson + "}", &saveSec);
        });
        getReply = call(*cli, "tc_get_screenshot", R"({"window":1})", &getSec);
        saver.join();
        const string timeoutMsg = "rendered no frame within 5 s";
        check("tc_get_screenshot visible window -> deferred, then timeout",
              contains(messageOf(getReply), timeoutMsg), getReply.dump());
        check("  waited for the window's tick (>= 4.5 s)", getSec >= 4.5, to_string(getSec) + " s");
        check("tc_save_screenshot visible window -> deferred, then timeout",
              contains(messageOf(saveReply), timeoutMsg), saveReply.dump());
        check("  waited for the window's tick (>= 4.5 s)", saveSec >= 4.5, to_string(saveSec) + " s");

        // --- no hook: the native flag of a window the OS never hid --------
        // Main reads the hook only while it handles a request; the request
        // channel orders this write before the next read.
        internal::windowOccludedHookForTests() = nullptr;
        list = call(*cli, "tc_list_windows", "{}");
        const json wins2 = list.is_object() ? list.value("windows", json::array()) : json::array();
        check("without the hook: both windows occluded false",
              wins2.size() == 3 && wins2[1].value("occluded", true) == false &&
              wins2[2].value("occluded", true) == false, list.dump());

        done = true;
    });

    // The frame loop's part: answer queued requests on this thread, and let
    // the main window's drain give up on targeted deferrals past their
    // deadline.
    auto deadline = chrono::steady_clock::now() + chrono::seconds(40);
    while (!done && chrono::steady_clock::now() < deadline) {
        mcp::processHttpQueue();
        mcp::drainDeferredResponses();
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    check("finished before the deadline", done.load());
    if (!done) {
        worker.detach();
        return 1;
    }
    worker.join();
    mcp::stopHttpServer();
    internal::windowOccludedHookForTests() = nullptr;
    error_code ec;
    filesystem::remove(savePath, ec);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
