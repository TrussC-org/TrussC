#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "impl/httplib.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

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
// The test re-launches its own executable per case (as mcpHttpGuard does), so
// one harness covers every desktop OS and reads the child's exit code.
std::string quoteArgument(const std::string& value) {
#ifdef _WIN32
    return "\"" + value + "\"";  // Windows executable paths cannot contain a quote
#else
    std::string quoted = "'";
    for (char c : value) quoted += c == '\'' ? "'\\''" : std::string(1, c);
    return quoted + "'";
#endif
}
int child(bool fromWorker) {
    // Deadlock watchdog, not a success-time assertion.
    std::thread([] { std::this_thread::sleep_for(std::chrono::seconds(30)); std::_Exit(3); }).detach();
    workerExit = fromWorker;
    runHeadlessApp<ExitApp>();
    return 4; // neither exit path should return
}
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && std::string(argv[1]) == "--child") return child(std::string(argv[2]) == "worker");
    std::string command = quoteArgument(fs::absolute(argv[0]).string());
#ifdef TC_CORE_TEST_NAME
    command += " " TC_CORE_TEST_NAME;
#endif
    std::fflush(nullptr);
    const bool appExit = std::system((command + " --child update").c_str()) == 0;
    std::printf("std::exit from update with in-process MCP listener: %s\n", appExit ? "PASS" : "FAIL");
    const bool handlerExit = std::system((command + " --child worker").c_str()) == 0;
    std::printf("std::exit from HTTP request worker: %s\n", handlerExit ? "PASS" : "FAIL");
    return appExit && handlerExit ? 0 : 1;
}
