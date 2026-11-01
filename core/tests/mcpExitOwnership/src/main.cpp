#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#include "impl/httplib.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#ifndef _WIN32
#include <sys/wait.h>
#endif

using namespace tc;
namespace {
bool workerExit = false;
struct ExitApp : App {
    void setup() override {
        mcp::setToolLevel(mcp::ToolLevel::Control);
        mcp::startHttpServer(0);
        if (mcp::getHttpPort() <= 0) std::_Exit(2);
        if (workerExit) {
            mcp::tool("exit_worker", "test process exit from HTTP reply construction").bind([]() -> json {
                mcp::deferToolResultTwoStage([] {
                    return []() -> json { std::exit(0); };
                }, internal::currentAppOwner()->context); // explicit virtual presentation seam
                return nullptr;
            });
            const int port = mcp::getHttpPort();
            std::thread([port] {
                httplib::Client client("127.0.0.1", port);
                client.set_read_timeout(20);
                auto res = client.Post("/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exit_worker"}})", "application/json");
                // The reply thunk exits before any reply is sent, so getting
                // here means the tool did not run its exit: say why.
                if (res) std::fprintf(stderr, "worker request answered without exit: HTTP %d %s\n", res->status, res->body.c_str());
                else std::fprintf(stderr, "worker request failed: %s\n", httplib::to_string(res.error()).c_str());
            }).detach();
        }
    }
    void update() override {
        if (!workerExit) std::exit(0);
        // Simulate an addressed frame on this virtual test context so the
        // real HTTP worker executes its reply thunk; maintenance has no frame.
        mcp::internal::drainDeferredResponses(internal::currentAppOwner()->context, true,
                                            std::chrono::steady_clock::now(), false);
        mcp::internal::drainDeferredResponses(nullptr, false);
    }
};
// The test re-launches its own executable per case (as mcpHttpGuard does), so
// one harness covers every desktop OS and reads the child's exit code.
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
std::string quoteArgument(const std::string& value) {
#ifdef _WIN32
    return "\"" + value + "\"";  // Windows executable paths cannot contain a quote
#else
    std::string quoted = "'";
    for (char c : value) quoted += c == '\'' ? "'\\''" : std::string(1, c);
    return quoted + "'";
#endif
}
#endif
constexpr int kWatchdogExit = 5;
// The child's exit code as the process reported it (std::system's raw status
// is a wait status on POSIX).
#if !defined(__APPLE__) || !TARGET_OS_IPHONE
int exitCode(int status) {
#ifdef _WIN32
    return status;
#else
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return status;
#endif
}
#endif
int child(bool fromWorker) {
    // Deadlock watchdog, not a success-time assertion.
    // Its own exit code, so a failure tells a hang from an abort (3 on Windows).
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(30)); std::_Exit(kWatchdogExit); }).detach();
    workerExit = fromWorker;
    runHeadlessApp<ExitApp>();
    return 4; // neither exit path should return
}
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child") return child(std::string(argv[2]) == "worker");
#if defined(__APPLE__) && TARGET_OS_IPHONE
    std::printf("SKIP MCP process exit checks: system() unavailable on iOS\n");
    return 0;
#else
    std::string command = quoteArgument(fs::absolute(argv[0]).string());
#ifdef TC_CORE_TEST_NAME
    command += " " TC_CORE_TEST_NAME;
#endif
    std::fflush(nullptr);
    const int appCode = exitCode(std::system((command + " --child update").c_str()));
    const bool appExit = appCode == 0;
    std::printf("std::exit from update with in-process MCP listener: %s (exit code %d)\n", appExit ? "PASS" : "FAIL", appCode);
    const int handlerCode = exitCode(std::system((command + " --child worker").c_str()));
    const bool handlerExit = handlerCode == 0;
    std::printf("std::exit from HTTP request worker: %s (exit code %d%s)\n", handlerExit ? "PASS" : "FAIL",
                handlerCode, handlerCode == kWatchdogExit ? ", watchdog: hung" : "");
    return appExit && handlerExit ? 0 : 1;
#endif
}
