// MCP replies preserve their contents when strings contain malformed UTF-8 (#324).
// Headless: drive the same queue and deferred stages without a socket or GPU.
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

std::string request(const std::string& name) {
    return json{{"jsonrpc", "2.0"}, {"id", 324}, {"method", "tools/call"},
                {"params", {{"name", name}, {"arguments", json::object()}}}}.dump();
}

json call(const std::string& name) {
    return json::parse(mcp::Server::instance().processMessage(request(name)));
}

json content(const json& reply) {
    check("reply retains JSON-RPC version and request id",
          reply.at("jsonrpc") == "2.0" && reply.at("id") == 324);
    return json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>());
}

#ifndef __EMSCRIPTEN__
json deferredCall(const std::string& name) {
    auto promise = std::make_shared<std::promise<mcp::detail::ReplyThunk>>();
    auto future = promise->get_future();
    mcp::detail::getHttpChannel().send(mcp::McpRequest{request(name), promise});
    mcp::processHttpQueue();
    mcp::drainDeferredResponses();
    return json::parse(future.get()());
}
#endif
} // namespace

TC_CORE_TEST_MAIN() {
    try {
        int logs = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs&) { ++logs; });
        const std::string bad = "bad \"quoted\" \\path \xff";
        const std::string repaired = "bad \"quoted\" \\path \xef\xbf\xbd";
        mcp::tool("invalid_object", "").bind([bad]() -> json {
            return {{"s", bad}, {"untouched", 42}};
        });
        check("object reply replaces invalid bytes and keeps other fields",
              content(call("invalid_object")) == json{{"s", repaired}, {"untouched", 42}});

        mcp::tool("invalid_array", "").bind([bad]() -> json {
            return json::array({json{{"type", "text"}, {"text", bad}}});
        });
        auto arrayReply = call("invalid_array");
        check("content array is repaired at the envelope boundary",
              arrayReply.at("id") == 324 && arrayReply.at("jsonrpc") == "2.0" &&
              arrayReply.at("result").at("content").at(0).at("text") == repaired);

        mcp::tool("invalid_exception", "").bind([bad]() -> json {
            throw std::runtime_error(bad);
        });
        auto error = call("invalid_exception");
        check("exception is an MCP tool error with escaped, repaired text",
              error.at("id") == 324 && error.at("jsonrpc") == "2.0" &&
              error.at("result").at("isError") == true &&
              error.at("result").at("content").at(0).at("text") == "Tool execution error: " + repaired);

        const json valid = {{"s", "日本語 😀 \" \\ \n"}, {"n", 42}};
        mcp::tool("valid_reply", "").bind([valid]() { return valid; });
        const json expected = {{"jsonrpc", "2.0"}, {"id", 324},
            {"result", {{"content", json::array({json{{"type", "text"}, {"text", valid.dump()}}})}}}};
        check("valid reply stays byte-for-byte identical",
              mcp::Server::instance().processMessage(request("valid_reply")) == expected.dump());
        mcp::tool("incomplete_utf8", "").bind([]() -> json { return {{"s", "end \xe6\x97"}}; });
        check("incomplete trailing code point becomes U+FFFD",
              content(call("incomplete_utf8")).at("s") == "end \xef\xbf\xbd");

        // Same display conversion as tc_get_audio_state, including native Windows paths.
#ifdef _WIN32
        const fs::path path(std::wstring(L"sound_") + wchar_t(0xD800) + L".wav");
#else
        const fs::path path("sound_\xff.wav");
#endif
        mcp::tool("display_path", "").bind([path]() -> json {
            return {{"path", internal::pathToDisplayUtf8(path)}};
        });
        check("native audio display path survives the MCP boundary",
              content(call("display_path")).at("path") == "sound_\xef\xbf\xbd.wav");
        check("Windows lossy UTF-16 conversion replaces lone surrogate",
              internal::utf16ToUtf8Lossy<char16_t>(u"sound_\xD800.wav") == "sound_\xef\xbf\xbd.wav");

#ifndef __EMSCRIPTEN__
        for (bool twoStage : {false, true}) {
            const std::string name = twoStage ? "deferred_worker" : "deferred_main";
            mcp::tool(name, "").bind([bad, twoStage]() -> json {
                if (twoStage) {
                    mcp::deferToolResultTwoStage([bad] {
                        return [bad]() -> json { throw std::runtime_error(bad); };
                    });
                } else {
                    mcp::deferToolResultUntilAfterFrame([bad]() -> json { throw std::runtime_error(bad); });
                }
                return nullptr;
            });
            check(twoStage ? "worker exception keeps quotes, backslash and replacement"
                           : "deferred exception keeps quotes, backslash and replacement",
                  deferredCall(name).at("result").at("content").at(0).at("text") ==
                      (twoStage ? "deferred worker stage failed: " : "deferred response failed: ") + repaired);

            const std::string resultName = name + "_result";
            mcp::tool(resultName, "").bind([bad, twoStage]() -> json {
                if (twoStage) {
                    mcp::deferToolResultTwoStage([bad] {
                        return [bad]() -> json { return {{"s", bad}}; };
                    });
                } else {
                    mcp::deferToolResultUntilAfterFrame([bad]() -> json { return {{"s", bad}}; });
                }
                return nullptr;
            });
            check("deferred result remains a successful JSON-RPC reply",
                  content(deferredCall(resultName)).at("s") == repaired);
        }
        httplib::Response rejected;
        mcp::detail::rejectRequest(rejected, 403, bad);
        check("HTTP error reply escapes and replaces its message",
              rejected.status == 403 && json::parse(rejected.body).at("error") == repaired);

        // Exercise the cancellation replies used during owner unload and normal shutdown.
        int owner;
        for (bool shutdown : {false, true}) {
            mcp::detail::DeferredResponse pending;
            pending.id = 324;
            pending.owner = &owner;
            pending.response = std::make_shared<std::promise<mcp::detail::ReplyThunk>>();
            auto future = pending.response->get_future();
            mcp::detail::deferredResponses().push_back(std::move(pending));
            if (shutdown) mcp::stopHttpServer();
            else mcp::detail::removeRegistrationsOwnedBy(&owner);
            auto reply = json::parse(future.get()());
            check("shutdown/unload cancellation is valid JSON",
                  reply.at("result").at("content").at(0).at("text").get<std::string>().find(shutdown ? "shut down" : "unloaded") != std::string::npos);
        }
#endif
        check("malformed reply bytes produce no warning or error log", logs == 0);
    } catch (const std::exception& e) {
        std::printf("FAIL: %s\n", e.what());
        ++failures;
    }
    return failures ? 1 : 0;
}
