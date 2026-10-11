#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <future>
#if (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tc;
namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
int requests = 0, exits = 0, begins = 0, cleans = 0;
std::string mode;
EventListener quitListener, logListener;
void verifyWindowExit() {
    check(exits == 1 && begins == 1 && cleans == 1, "one exit and one complete log pair");
    check(requests == ((mode == "cancel" || mode == "cancel-empty") ? 2 : 1), "quit request count");
    if (failures) std::_Exit(1);
}
struct ExitApp : App {
    int updates = 0;
    void setup() override {
        if (mode == "log-warning" || mode == "log-silent") setLogLevel(LogLevel::Warning);
        if (mode == "log-silent") setFileLogLevel(LogLevel::Silent);
        if (mode == "event-driven") { setIndependentFps(VSYNC, EVENT_DRIVEN); }
        quitListener = events().exitRequested.listen([](ExitRequestEventArgs& args) {
            ++requests;
            if ((mode == "cancel" || mode == "cancel-empty" || mode == "forced" || mode == "escalate" || mode == "escalate-term") && requests == 1) {
                args.cancel = true;
                if (mode != "cancel-empty") args.reason = "Unsaved test document";
            }
#if (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
            if ((mode == "escalate" || mode == "escalate-term") && requests == 2) {
                check(exits == 0 && begins == 0 && cleans == 0,
                      "first signal after veto delivers another cancellable request");
                std::fflush(stdout);
                // This request is still pending inside its listener. Only
                // now does a further signal have permission to force exit.
                std::raise(mode == "escalate-term" ? SIGTERM : SIGINT);
                std::_Exit(1);
            }
#endif
        });
    }
    void update() override {
        ++updates;
#ifdef _WIN32
        HWND hwnd = (HWND)sapp_win32_get_hwnd();
        if (updates == 1) {
            const bool veto = mode == "cancel" || mode == "cancel-empty" || mode == "forced";
            check(SendMessageW(hwnd, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP) == !veto,
                  "session query respects veto (Restart Manager)");
            if (veto) {
                wchar_t reason[256]{};
                DWORD count = 256;
                check(ShutdownBlockReasonQuery(hwnd, reason, &count) != FALSE,
                      "veto registers a shutdown block");
                check(std::wstring(reason) == (mode == "cancel-empty" ? L"The app is still busy." : L"Unsaved test document"),
                      "shutdown block uses custom or generic explanation");
            }
            if (mode == "cancel" || mode == "cancel-empty") {
                SendMessageW(hwnd, WM_ENDSESSION, FALSE, ENDSESSION_CLOSEAPP);
                wchar_t reason[256]{};
                DWORD count = 256;
                check(ShutdownBlockReasonQuery(hwnd, reason, &count) == FALSE,
                      "cancelled session removes shutdown block");
                requestExitApp();
            } else {
                SendMessageW(hwnd, WM_ENDSESSION, TRUE,
                             mode == "forced" ? ENDSESSION_CLOSEAPP : ENDSESSION_LOGOFF);
                check(false, "session end must not return to update");
                std::_Exit(1);
            }
        }
#elif (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
        if (updates == 1) std::raise(mode == "sigint" ? SIGINT : SIGTERM);
        if (requests == 1 && (mode == "cancel" || mode == "cancel-empty")) requestExitApp();
        if (requests == 1 && (mode == "escalate" || mode == "escalate-term")) std::raise(SIGTERM);
#else
        requestExitApp();
#endif
    }
    void exit() override {
        ++exits;
        if (mode == "cleanup-reentry") internal::_cleanup_cb();
    }
};

#ifndef __EMSCRIPTEN__
void testHttpShutdown() {
    internal::setExitReason("sigterm");
    mcp::startHttpServer(0);
    auto waitFor = [](auto ready) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!ready() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return ready();
    };
    const bool listening = waitFor([] { return mcp::getHttpPort() > 0 && mcp::detail::getHttpServer()->is_running(); });
    check(listening, "HTTP server ready");
    if (!listening) { mcp::stopHttpServer(); return; }
    mcp::tool("http_defer", "test").bind(std::function<json()>([] {
        mcp::deferToolResultUntilAfterFrame([] { return json{}; });
        return json{};
    }));
    auto send = [](int id, const char* method) {
        return std::async(std::launch::async, [id, method] {
            httplib::Client client("127.0.0.1", mcp::getHttpPort());
            client.set_read_timeout(10);
            json body{{"jsonrpc", "2.0"}, {"id", id}, {"method", method}, {"params", {{"name", "http_defer"}}}};
            auto response = client.Post("/mcp", body.dump(), "application/json");
            return response ? response->body : std::string();
        });
    };
    auto deferred = send(1, "tools/call");
    check(waitFor([] { return !mcp::detail::getHttpChannel().empty(); }), "deferred HTTP request queued");
    mcp::processHttpQueue();
    auto queued = send(2, "ping");
    check(waitFor([] { return !mcp::detail::getHttpChannel().empty(); }), "last-frame HTTP request queued");
    mcp::stopHttpServer(); // joins real server workers; must not strand either request
    for (auto* future : {&deferred, &queued}) {
        const auto reply = json::parse(future->get(), nullptr, false);
        check(reply.is_object() && reply.value("jsonrpc", "") == "2.0"
              && reply.dump().find("exiting normally (reason=sigterm)") != std::string::npos,
              "HTTP worker received normal-exit JSON-RPC reply before join");
    }
}
#endif

void testShared() {
    check(internal::exitReason().empty(), "unknown reason omitted");
    check(internal::exitLogMessage(false).find("exit: begin pid=") == 0, "unknown begin format");
    internal::setExitReason("os-session-end");
    internal::setExitReasonIfEmpty("window-close");
    check(internal::exitReason() == "os-session-end", "routing preserves origin");
    check(internal::exitLogMessage(true).find("exit: clean reason=os-session-end code=0 pid=") == 0,
          "stable clean format");
#if (defined(__linux__) && !defined(__ANDROID__)) || (defined(__APPLE__) && TARGET_OS_OSX)
    internal::installWindowExitSignals();
    std::raise(SIGTERM);
    check(internal::pendingWindowExitSignal() == SIGTERM, "SIGTERM flag consumed on main thread");
    check(internal::pendingWindowExitSignal() == 0, "signal requests quit only once");
    // The inherited pending flag must escalate even after the request was consumed.
    pid_t child = fork();
    if (child == 0) { std::raise(SIGINT); std::_Exit(1); }
    int status = 0;
    check(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status)
              && WEXITSTATUS(status) == 128 + SIGINT, "second signal forces process exit");
    internal::cancelWindowExitSignal();
    check(internal::pendingWindowExitSignal() == 0, "veto clears pending signal");
    std::raise(SIGTERM);
    check(internal::pendingWindowExitSignal() == SIGTERM, "same signal after veto requests quit again");
    check(internal::pendingWindowExitSignal() == 0, "renewed signal delivered only once");
    child = fork();
    if (child == 0) { std::raise(SIGTERM); std::_Exit(1); }
    check(child > 0 && waitpid(child, &status, 0) == child && WIFEXITED(status)
              && WEXITSTATUS(status) == 128 + SIGTERM, "second pending signal after veto exits with 143");
    internal::cancelWindowExitSignal();
    std::raise(SIGINT);
    check(internal::pendingWindowExitSignal() == SIGINT, "different signal after veto requests quit again");
    internal::restoreWindowExitSignals();
    internal::installWindowExitSignals();
    std::raise(SIGINT);
    check(internal::pendingWindowExitSignal() == SIGINT, "SIGINT requests quit");
    internal::restoreWindowExitSignals();
#endif
#ifndef __EMSCRIPTEN__
    // Queue a normal request and a deferred tool, then stop without another
    // frame. All futures must receive JSON-RPC replies with their original IDs.
    auto enqueue = [](const std::string& body) {
        auto promise = std::make_shared<std::promise<mcp::detail::ReplyThunk>>();
        auto future = promise->get_future();
        mcp::detail::enqueueHttpRequest({body, promise});
        return future;
    };
    mcp::tool("exit_test_defer", "test").bind(std::function<json()>([] {
        mcp::deferToolResultUntilAfterFrame([] { return json{{"unexpected", true}}; });
        return json{};
    }));
    auto deferred = enqueue(R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exit_test_defer"}})");
    mcp::processHttpQueue();
    check(mcp::hasDeferredResponses(), "request deferred until next frame");
    auto queued = enqueue(R"({"jsonrpc":"2.0","id":"queued","method":"ping"})");
    // A racing sender is either drained from the queue or answered directly.
    std::future<mcp::detail::ReplyThunk> raced;
    std::thread sender([&] { raced = enqueue(R"({"jsonrpc":"2.0","id":3,"method":"ping"})"); });
    mcp::stopHttpServer();
    sender.join();
    auto late = enqueue(R"({"jsonrpc":"2.0","id":4,"method":"ping"})");
    auto verify = [](auto& future, const json& id) {
        const auto ready = future.wait_for(std::chrono::seconds(5)) == std::future_status::ready;
        check(ready, "shutdown answers pending worker");
        if (!ready) return;
        const auto reply = json::parse(future.get()());
        check(reply["jsonrpc"] == "2.0" && reply["id"] == id, "JSON-RPC envelope keeps request ID");
        check(reply.dump().find("the app is exiting normally (reason=os-session-end)") != std::string::npos,
              "reply explains normal exit and reason");
    };
    verify(deferred, 1); verify(queued, "queued"); verify(raced, 3); verify(late, 4);
    check(!mcp::hasDeferredResponses(), "no deferred producers left at shutdown");
#endif
    check(internal::beginExitCleanup(), "first cleanup accepted");
    check(!internal::beginExitCleanup(), "reentrant cleanup rejected");
    internal::setExitReason("exit-app");
    check(internal::exitReason() == "os-session-end", "cleanup freezes exit reason");
}
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 1 && std::string(argv[1]) == "--window") {
        mode = argc > 2 ? argv[2] : "accept";
        logListener = getLogger().onLog.listen([](LogEventArgs& args) {
            if (args.message.find("exit: begin ") != std::string::npos) ++begins;
            if (args.message.find("exit: clean ") != std::string::npos) {
                ++cleans;
#ifdef _WIN32
                // TerminateProcess skips atexit, so verify before leaving cleanup.
                if (mode == "accept" || mode == "forced") {
                    verifyWindowExit();
                    std::fflush(stdout);
                }
#endif
            }
        });
        std::atexit([] {
#ifdef _WIN32
            check(mode != "accept" && mode != "forced", "session end skips atexit");
#endif
            verifyWindowExit(); // macOS sapp_run does not return
        });
        WindowSettings settings;
        settings.width = 64; settings.height = 64;
        runApp<ExitApp>(settings);
#ifndef __EMSCRIPTEN__
    } else if (argc > 1 && std::string(argv[1]) == "--http") {
        testHttpShutdown();
#endif
    } else {
        testShared();
    }
    return failures ? 1 : 0;
}
