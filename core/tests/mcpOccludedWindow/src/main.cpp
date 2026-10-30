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
// The test publishes native metadata through the common host snapshot seam.
//
// The main thread pumps mcp::processHttpQueue() and the main window's
// mcp::internal::drainDeferredResponses() the way the frame loop does; the requests run
// on worker threads with a deadline.
// =============================================================================

#include <TrussC.h>
#ifndef __EMSCRIPTEN__
#include "impl/httplib.h"
#endif
#include "../../common/tcCoreTest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <string>
#include <thread>

using namespace std;
using namespace tc;

namespace {

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

// Explicit fake bindings use the same stored metadata as live Windows.
struct OpenWindow : Window {
    explicit OpenWindow(const string& title) {
        title_ = title;
        internal::WindowRequestAccess::bindForTests(*this, title == "hidden" ? 303 : 202);
    }
    ~OpenWindow() { internal::WindowRequestAccess::unbind(*this); }
};

static string toolCall(const string& name, const string& args) {
    return R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":")" + name +
           R"(","arguments":)" + args + "}}";
}

// The tool's own JSON reply: result.content[0].text, parsed. Null on any
// other shape (transport error, missing text, ...). Framework errors retain
// the MCP result so timeout assertions can check isError.
static json toolReply(const httplib::Result& r) {
    if (!r || r->status != 200) return json();
    json body = json::parse(r->body, nullptr, false);
    if (body.is_discarded() || !body.contains("result")) return json();
    const json& content = body["result"]["content"];
    if (!content.is_array() || content.empty() || !content[0].contains("text")) return json();
    if (body["result"].value("isError", false)) return body["result"];
    json j = json::parse(content[0]["text"].get<string>(), nullptr, false);
    return j.is_discarded() ? json() : j;
}

static string messageOf(const json& reply) {
    if (reply.is_object() && reply.value("isError", false))
        return reply.at("content").at(0).at("text").get<string>();
    return reply.is_object() && reply.contains("message") && reply["message"].is_string()
        ? reply["message"].get<string>() : string();
}

static string statusOf(const json& reply) {
    return reply.is_object() && reply.contains("status") && reply["status"].is_string()
        ? reply["status"].get<string>() : string();
}

static bool contains(const string& s, const string& part) { return s.find(part) != string::npos; }

} // namespace

TC_CORE_TEST_MAIN() {
    OpenWindow visible("visible"), hidden("hidden");
    sapp_tc_window_metadata hiddenMetadata{};
    hiddenMetadata.trust_hidden_signals = true; hiddenMetadata.occluded = true;
    internal::publishWindowMetadata(hidden, hiddenMetadata);

    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    mcp::startHttpServer();
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
        // Connect to the listener's actual address. "localhost" tries ::1 first
        // on Windows, adding a fallback delay for an IPv4 listener.
        auto makeClient = [port] {
            auto cli = make_unique<httplib::Client>("127.0.0.1", port);
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
        check("tc_list_windows lists both positive Window IDs", wins.size() == 2, list.dump());
        if (wins.size() == 2) {
            check("visible window: occluded false",
                  wins[0].value("title", "") == "visible" && wins[0].contains("occluded") &&
                  wins[0]["occluded"] == false, wins[0].dump());
            check("hidden window: occluded true",
                  wins[1].value("title", "") == "hidden" && wins[1].contains("occluded") &&
                  wins[1]["occluded"] == true, wins[1].dump());
        }

        // --- hidden window: both screenshot tools fail at once ------------
        double sec = 0;
        json r = call(*cli, "tc_get_screenshot", R"({"window":303})", &sec);
        check("tc_get_screenshot hidden window -> error", statusOf(r) == "error", r.dump());
        check("  names the window and why",
              contains(messageOf(r), "window 303 is not visible (the OS reports it hidden"), r.dump());
        check("  answered at once (< 2 s, not the 5 s timeout)", sec < 2.0, to_string(sec) + " s");

        r = call(*cli, "tc_save_screenshot", R"({"window":303,"path":)" + savePathJson + "}", &sec);
        check("tc_save_screenshot hidden window -> error", statusOf(r) == "error", r.dump());
        check("  names the window and why",
              contains(messageOf(r), "window 303 is not visible (the OS reports it hidden"), r.dump());
        check("  answered at once (< 2 s, not the 5 s timeout)", sec < 2.0, to_string(sec) + " s");
        // No "no file written" check: headless, a secondary window's capture
        // has no framebuffer to read (0x0), so no file could appear either way.

        // Unchanged: an index past the open windows.
        r = call(*cli, "tc_get_screenshot", R"({"window":999})");
        check("out-of-range window -> index error", contains(messageOf(r), "positive window ID"), r.dump());

        // --- visible window: unchanged, deferred to its tick --------------
        // The stand-in never ticks, so the main window's drain answers both
        // requests with the timeout reply once 5 s have passed.
        json getReply, saveReply;
        double getSec = 0, saveSec = 0;
        thread saver([&] {
            auto c2 = makeClient();
            saveReply = call(*c2, "tc_save_screenshot",
                             R"({"window":202,"path":)" + savePathJson + "}", &saveSec);
        });
        getReply = call(*cli, "tc_get_screenshot", R"({"window":202})", &getSec);
        saver.join();
        const string timeoutMsg = "rendered no frame within 5 s";
        check("tc_get_screenshot visible window -> deferred, then timeout",
              getReply.value("isError", false) && contains(messageOf(getReply), timeoutMsg), getReply.dump());
        check("  waited for the window's tick (>= 4.5 s)", getSec >= 4.5, to_string(getSec) + " s");
        check("tc_save_screenshot visible window -> deferred, then timeout",
              saveReply.value("isError", false) && contains(messageOf(saveReply), timeoutMsg), saveReply.dump());
        check("  waited for the window's tick (>= 4.5 s)", saveSec >= 4.5, to_string(saveSec) + " s");

        // --- a fresh visible native observation clears the common snapshot ---
        // Main reads the snapshot while handling a request; the request
        // channel orders this write before the next read.
        internal::publishWindowMetadata(hidden, {});
        list = call(*cli, "tc_list_windows", "{}");
        const json wins2 = list.is_object() ? list.value("windows", json::array()) : json::array();
        check("after visible observation: both windows occluded false",
              wins2.size() == 2 && wins2[0].value("occluded", true) == false &&
              wins2[1].value("occluded", true) == false, list.dump());

        done = true;
    });

    // The frame loop's part: answer queued requests on this thread, and let
    // the main window's drain give up on targeted deferrals past their
    // deadline.
    auto deadline = chrono::steady_clock::now() + chrono::seconds(40);
    while (!done && chrono::steady_clock::now() < deadline) {
        mcp::processHttpQueue();
        mcp::internal::drainDeferredResponses(nullptr, false);
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    check("finished before the deadline", done.load());
    if (!done) {
        worker.detach();
        return 1;
    }
    worker.join();
    mcp::stopHttpServer();
    internal::publishWindowMetadata(hidden, {});
    error_code ec;
    filesystem::remove(savePath, ec);

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
