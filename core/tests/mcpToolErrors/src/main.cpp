// Framework tool failures retain the request id and use MCP isError (#683).
// Headless: drive the production queue/stages without sockets, a GPU, or sleeps.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <stdexcept>

using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

std::string request(const std::string& name, const json& id) {
    return json{{"jsonrpc", "2.0"}, {"id", id}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", json::object()}}}}.dump();
}

void checkError(const char* name, const json& reply, const json& id,
                const std::string& message) {
    check(name, reply == json{{"jsonrpc", "2.0"}, {"id", id},
        {"result", {{"content", {{{"type", "text"}, {"text", message}}}},
                    {"isError", true}}}});
}

#ifndef __EMSCRIPTEN__
std::future<mcp::detail::ReplyThunk> queue(const std::string& name, const json& id) {
    auto promise = std::make_shared<std::promise<mcp::detail::ReplyThunk>>();
    auto future = promise->get_future();
    mcp::detail::getHttpChannel().send(mcp::McpRequest{request(name, id), promise});
    mcp::processHttpQueue();
    return future;
}

json reply(std::future<mcp::detail::ReplyThunk>& future) {
    // Completion is synchronous after drain/cancel; never hang on a regression.
    if (future.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
        throw std::runtime_error("reply was not completed");
    // Match the HTTP handler: run the thunk on a worker thread.
    auto thunk = future.get();
    return json::parse(std::async(std::launch::async, std::move(thunk)).get());
}
#endif
} // namespace

TC_CORE_TEST_MAIN() {
    try {
        auto& server = mcp::Server::instance();
        mcp::tool("throws_inline", "").bind([]() -> json {
            throw std::runtime_error("inline failed");
        });
        checkError("inline exception", json::parse(server.processMessage(request("throws_inline", 683))),
                   683, "Tool execution error: inline failed");

        auto unknown = json::parse(server.processMessage(request("missing_tool", "unknown")));
        check("unknown tool remains a JSON-RPC error",
              unknown == json{{"jsonrpc", "2.0"}, {"id", "unknown"},
                  {"error", {{"code", -32601}, {"message", "Tool not found: missing_tool"}}}});
        const json ownError = {{"status", "error"}, {"message", "tool's own error"}};
        mcp::tool("own_error", "").bind([ownError]() { return ownError; });
        auto own = json::parse(server.processMessage(request("own_error", 684)));
        check("tool's own error content stays unchanged",
              own == json{{"jsonrpc", "2.0"}, {"id", 684},
                  {"result", {{"content", {{{"type", "text"}, {"text", ownError.dump()}}}}}}});

#ifndef __EMSCRIPTEN__
        mcp::tool("throws_main", "").bind([]() -> json {
            mcp::deferToolResultUntilAfterFrame([]() -> json {
                throw std::runtime_error("main failed");
            });
            return nullptr;
        });
        for (bool fallback : {false, true}) {
            auto future = queue("throws_main", "main");
            if (fallback) mcp::detail::deferredResponses().back().errorReply = nullptr;
            mcp::drainDeferredResponses();
            checkError(fallback ? "main exception fallback" : "main exception", reply(future),
                       "main", "deferred response failed: main failed");
        }
        for (bool mainThrows : {false, true}) {
            mcp::tool(mainThrows ? "two_main" : "two_worker", "").bind([mainThrows]() -> json {
                mcp::deferToolResultTwoStage([mainThrows]() -> std::function<json()> {
                    if (mainThrows) throw std::runtime_error("two-stage main failed");
                    return []() -> json { throw std::runtime_error("worker failed"); };
                });
                return nullptr;
            });
            auto future = queue(mainThrows ? "two_main" : "two_worker", 685);
            mcp::drainDeferredResponses();
            checkError(mainThrows ? "two-stage main exception" : "two-stage worker exception",
                       reply(future), 685, mainThrows ? "deferred response failed: two-stage main failed"
                                                    : "deferred worker stage failed: worker failed");
        }

        int target, owner;
        bool produced = false;
        mcp::tool("pending", "").bind([&]() -> json {
            mcp::deferToolResultUntilAfterFrame([&]() -> json {
                produced = true;
                return nullptr;
            }, &target);
            mcp::detail::setDeferralOwner(&owner);
            return nullptr;
        });
        for (bool fallback : {false, true}) {
            auto timed = queue("pending", "timeout");
            auto& pending = mcp::detail::deferredResponses().back();
            if (fallback) pending.timeoutReply = nullptr;
            // Exercise expiry without depending on how long the test waited.
            pending.deadline = std::chrono::steady_clock::time_point::min();
            mcp::drainDeferredResponses();
            checkError(fallback ? "timeout fallback" : "timeout", reply(timed), "timeout",
                       fallback ? "window did not render" :
                           "the window rendered no frame within 5 s (minimized, hidden or closed?)");

            auto unloaded = queue("pending", "unload");
            if (fallback) mcp::detail::deferredResponses().back().errorReply = nullptr;
            mcp::detail::removeRegistrationsOwnedBy(&owner);
            checkError(fallback ? "unload fallback" : "unload", reply(unloaded), "unload",
                       "the app code behind this reply was unloaded by a hot reload before the reply was produced");
        }
        // Queue both variants before stopHttpServer closes the request channel.
        auto stopped = queue("pending", 686);
        auto stoppedFallback = queue("pending", "shutdown fallback");
        mcp::detail::deferredResponses().back().errorReply = nullptr;
        mcp::stopHttpServer();
        const std::string shutdown = "the MCP server shut down before the reply was produced";
        checkError("shutdown", reply(stopped), 686, shutdown);
        checkError("shutdown fallback", reply(stoppedFallback), "shutdown fallback", shutdown);
        check("cancelled producers never run", !produced);
        check("all deferred responses completed", !mcp::hasDeferredResponses());
#endif
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        ++failures;
    }
    return failures ? 1 : 0;
}
