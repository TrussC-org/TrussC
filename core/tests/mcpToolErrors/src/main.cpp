// Framework tool failures retain the request id and use MCP isError (#683).
// Headless: drive the production queue/stages without sockets, a GPU, or sleeps.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <cstdint>
#include <limits>
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
std::future<mcp::internal::ReplyThunk> queue(const std::string& name, const json& id) {
    auto promise = std::make_shared<std::promise<mcp::internal::ReplyThunk>>();
    auto future = promise->get_future();
    mcp::internal::getHttpChannel().send(mcp::McpRequest{request(name, id), promise});
    mcp::processHttpQueue();
    return future;
}

json reply(std::future<mcp::internal::ReplyThunk>& future) {
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
        auto& server = mcp::internal::Server::instance();
        // Invalid ids must never reach a tool or the deferred-id store.
        bool idToolCalled = false;
        mcp::tool("id_probe", "").bind([&]() -> json {
            idToolCalled = true;
            mcp::deferToolResultUntilAfterFrame([]() -> json { return nullptr; });
            return nullptr;
        });
        for (const json& id : {json(nullptr), json(1.5), json(1.0),
                               json{{"nested", 1}}, json::array({1, 2}), json(true)}) {
            const std::string label = id.dump() + " id is Invalid Request with null id";
            mcp::internal::deferralState().id = "unchanged";
            auto raw = request("id_probe", id);
            auto invalid = json::parse(server.processMessage(raw), nullptr, false);
            check(label.c_str(),
                  invalid.contains("error") && invalid["error"]["code"] == -32600 &&
                  invalid.contains("id") && invalid["id"].is_null());
            check("invalid id never reaches handler or deferred storage",
                  !idToolCalled && mcp::internal::deferralState().id == "unchanged");
#ifndef __EMSCRIPTEN__
            auto terminal = json::parse(mcp::internal::normalExitReply(raw));
            check("shutdown rejects invalid id with null id",
                  terminal["error"]["code"] == -32600 && terminal["id"].is_null());
#endif
        }
        // Parsing keeps 1.0 floating-point; the protocol accepts only integer types.
        check("1.0 parses as floating-point", json::parse("1.0").is_number_float());
        for (const json& id : {json("request-id"), json(0), json(-239),
                               json((std::numeric_limits<std::int64_t>::min)()),
                               json((std::numeric_limits<std::int64_t>::max)()),
                               json((std::numeric_limits<std::uint64_t>::max)())}) {
            auto result = json::parse(server.processMessage(
                json{{"jsonrpc", "2.0"}, {"method", "initialize"}, {"id", id}}.dump()), nullptr, false);
            const auto label = "valid id is echoed exactly: " + id.dump();
            check(label.c_str(),
                  result.contains("result") && result.contains("id") && result["id"].dump() == id.dump());
        }
        check("notification has no reply", server.processMessage(
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
#ifndef __EMSCRIPTEN__
        check("shutdown notification has no reply", mcp::internal::normalExitReply(
            R"({"jsonrpc":"2.0","method":"notifications/initialized"})").empty());
#endif
        check("initialize notification has no reply", server.processMessage(
            R"({"jsonrpc":"2.0","method":"initialize"})").empty());
        check("list notification has no reply", server.processMessage(
            R"({"jsonrpc":"2.0","method":"tools/list"})").empty());
        mcp::tool("throws_inline", "").bind([]() -> json {
            throw std::runtime_error("inline failed");
        });
        checkError("inline exception", json::parse(server.processMessage(request("throws_inline", 683))),
                   683, "Tool execution error: inline failed");
        mcp::tool("throws_unknown", "").bind([]() -> json {
            throw 42;
        });
        checkError("inline unknown exception",
                   json::parse(server.processMessage(request("throws_unknown", "non-std"))),
                   "non-std", "Tool execution error: unknown exception");

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
        {
            auto future = queue("throws_main", "main");
            mcp::internal::drainDeferredResponses();
            checkError("main exception", reply(future),
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
            mcp::internal::drainDeferredResponses();
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
            mcp::internal::setDeferralOwner(&owner);
            return nullptr;
        });
        {
            auto timed = queue("pending", "timeout");
            auto& pending = mcp::internal::deferredResponses().back();
            // Exercise expiry without depending on how long the test waited.
            pending.deadline = std::chrono::steady_clock::time_point::min();
            mcp::internal::drainDeferredResponses();
            checkError("timeout", reply(timed), "timeout",
                       "the window rendered no frame within 5 s (minimized, hidden or closed?)");

            auto unloaded = queue("pending", "unload");
            mcp::internal::removeRegistrationsOwnedBy(&owner);
            checkError("unload", reply(unloaded), "unload",
                       "the app code behind this reply was unloaded by a hot reload before the reply was produced");
        }
        // Queue before stopHttpServer closes the request channel.
        auto stopped = queue("pending", 686);
        mcp::stopHttpServer();
        const std::string shutdown = "the MCP server shut down before the reply was produced";
        checkError("shutdown", reply(stopped), 686, shutdown);
        check("cancelled producers never run", !produced);
        check("all deferred responses completed", !mcp::internal::hasDeferredResponses());
#endif
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        ++failures;
    }
    return failures ? 1 : 0;
}
