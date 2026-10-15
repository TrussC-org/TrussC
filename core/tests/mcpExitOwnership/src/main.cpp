#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstdlib>
#if defined(__linux__) && !defined(__ANDROID__)
#include "impl/httplib.h"
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tc;
namespace {
#if defined(__linux__) && !defined(__ANDROID__)
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
                });
                return nullptr;
            });
            const int port = mcp::getHttpPort();
            std::thread([port] {
                httplib::Client client("127.0.0.1", port);
                client.set_read_timeout(20);
                client.Post("/mcp", R"({"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exit_worker"}})", "application/json");
            }).detach();
        }
    }
    void update() override {
        if (!workerExit) std::exit(0);
        // Headless apps have no present; explicitly run the deferred main
        // stage so the real HTTP request worker executes its reply thunk.
        mcp::internal::drainDeferredResponses();
    }
};
bool runChild(bool fromWorker) {
    std::fflush(nullptr);
    const pid_t child = fork();
    if (child == 0) {
        alarm(30); // deadlock watchdog, not a success-time assertion
        workerExit = fromWorker;
        runHeadlessApp<ExitApp>();
        std::_Exit(3); // neither exit path should return
    }
    int status = 0;
    return child > 0 && waitpid(child, &status, 0) == child &&
        WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#endif
}
TC_CORE_TEST_MAIN() {
#if defined(__linux__) && !defined(__ANDROID__)
    const bool appExit = runChild(false);
    std::printf("std::exit from update with in-process MCP listener: %s\n", appExit ? "PASS" : "FAIL");
    const bool handlerExit = runChild(true);
    std::printf("std::exit from HTTP request worker: %s\n", handlerExit ? "PASS" : "FAIL");
    return appExit && handlerExit ? 0 : 1;
#else
    std::puts("SKIP: process-exit harness uses Linux fork/waitpid");
    return 0;
#endif
}
