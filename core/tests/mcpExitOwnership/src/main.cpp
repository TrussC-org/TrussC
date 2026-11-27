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
// Worker-thread std::exit is deliberately not tested; see EXIT_PROTOCOL.md.
bool thunkExit = false;
int requestedCode = 0;
bool endedNormally = false;
struct ExitApp : App {
    std::thread client;
    void setup() override {
        mcp::setToolLevel(mcp::ToolLevel::Control);
        mcp::startHttpServer(0);
        if (mcp::getHttpPort() <= 0) std::_Exit(2);
        if (thunkExit) {
            mcp::tool("exit_app", "request normal app exit from HTTP reply construction").bind([]() -> json {
                mcp::deferToolResultTwoStage([] {
                    return []() -> json {
                        exitApp(requestedCode);
                        return {{"requested", true}};
                    };
                }, internal::currentAppOwner()->context); // explicit virtual presentation seam
                return nullptr;
            });
            const int port = mcp::getHttpPort();
            client = std::thread([port] {
                httplib::Client client("127.0.0.1", port);
                client.set_read_timeout(20);
                auto res = client.Post("/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exit_app"}})", "application/json");
                if (!res) std::fprintf(stderr, "exitApp request failed: %s\n", httplib::to_string(res.error()).c_str());
            });
        }
    }
    void update() override {
        if (!thunkExit) std::exit(0);
        // Simulate an addressed frame on this virtual test context so the
        // real HTTP worker executes its reply thunk; maintenance has no frame.
        mcp::internal::drainDeferredResponses(internal::currentAppOwner()->context, true,
                                            std::chrono::steady_clock::now(), false);
        mcp::internal::drainDeferredResponses(nullptr, false);
    }
    void exit() override {
        if (client.joinable()) client.join();
        endedNormally = true;
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
int child(bool fromThunk, int code) {
    // Deadlock watchdog, not a success-time assertion.
    // Its own exit code, so a failure tells a hang from an abort (3 on Windows).
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(30)); std::_Exit(kWatchdogExit); }).detach();
    thunkExit = fromThunk;
    requestedCode = code;
    const int result = runHeadlessApp<ExitApp>();
    if (!endedNormally || result != code) return 4;
    std::printf("MCP exit completed: %d\n", code);
    return result;
}
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child") {
        const std::string mode = argv[2];
        return child(mode != "update", mode == "thunk-error" ? 7 : 0);
    }
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
    bool thunksExit = true;
    for (const auto& mode : {"thunk", "thunk-error"}) {
        const int expected = std::string(mode) == "thunk-error" ? 7 : 0;
        const std::string childCommand = command + " --child " + mode + " 2>&1";
#ifdef _WIN32
        FILE* pipe = _popen(childCommand.c_str(), "r");
#else
        FILE* pipe = popen(childCommand.c_str(), "r");
#endif
        if (!pipe) return 1;
        std::string output;
        char buffer[512];
        while (std::fgets(buffer, sizeof(buffer), pipe)) output += buffer;
#ifdef _WIN32
        const int code = exitCode(_pclose(pipe));
#else
        const int code = exitCode(pclose(pipe));
#endif
        std::printf("%s", output.c_str());
        const std::string marker = "MCP exit completed: " + std::to_string(expected) + "\n";
        const bool passed = code == expected && output.find(marker) != std::string::npos;
        std::printf("exitApp(%d) from HTTP reply thunk: %s (exit code %d%s)\n",
                    expected, passed ? "PASS" : "FAIL", code,
                    code == kWatchdogExit ? ", watchdog: hung" : "");
        thunksExit = thunksExit && passed;
    }
    return appExit && thunksExit ? 0 : 1;
#endif
}
