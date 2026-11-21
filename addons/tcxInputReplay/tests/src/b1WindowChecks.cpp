#include <tcxInputReplay.h>
#include "../../../../core/tests/common/tcWindowTest.h"
#include "../../../../core/tests/common/tcInjectedWindowTest.h"
#include <cstdio>
#ifndef __EMSCRIPTEN__
#include <impl/httplib.h>
#endif

namespace {
namespace replay = tcx::inputreplay;
using namespace tcx::inputreplay::detail;
int failures = 0, checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::printf("FAIL: %s\n", label); }
}
Json request(const std::string& method, Json params) {
    return Json::parse(tc::mcp::internal::Server::instance().processMessage(
        Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", method}, {"params", std::move(params)}}.dump()));
}
Json call(const std::string& name, Json args = Json::object()) {
    const auto reply = request("tools/call", {{"name", "tcx_inputreplay_" + name}, {"arguments", std::move(args)}});
    return Json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>());
}
void windowChecks() {
    tcCoreTest::WindowFixture first(9601), second(9602);
    tcCoreTest::InjectedWindow firstNative(*first.window, 9601), secondNative(*second.window, 9602);
    { tc::internal::WindowEntryScope seed(*first.window, false, true, false); }
    { tc::internal::WindowEntryScope seed(*second.window, false, true, false); }
    tc::internal::currentWindowCtx() = &tc::internal::WindowRequestAccess::context(*first.window);
    auto app = tc::internal::makeConstructedAppAt<tc::App>(&tc::internal::WindowRequestAccess::context(*first.window));
    tc::internal::attachCandidate(*first.window, app);
    tc::internal::applyPendingAppForTests(*first.window);
    tc::internal::AppOwnerScope owner(tc::internal::appOwner(*app));
    auto& s = state();
    const auto path = std::filesystem::current_path() / "inputreplay-b1.jsonl";
    replay::setup({{"left", first.window.get()}, {"left", second.window.get()}});
    check(!s.window && s.lastError.find("duplicate") != std::string::npos, "duplicate keys reject setup");
    replay::setup({{"left", first.window.get()}, {"right", first.window.get()}});
    check(!s.window, "duplicate destination rejects setup");
    replay::setup({{"left", nullptr}});
    check(!s.window, "missing window rejects setup");
    replay::setup({{"left", first.window.get()}, {"right", second.window.get()}});
    check(replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(5), 8), "start ring through public API");
    check(first.window->injectInput(tc::InputEvent::mouseMoved({10, 20}), "script"), "record first window");
    check(second.window->injectInput(tc::InputEvent::mouseMoved({30, 40}), "script"), "record second window");
    check(replay::flush(path) && !s.recording(), "public flush saves and stops");
    std::ifstream file(path);
    auto session = load(file);
    check(session.windows.size() == 2 && session.records[0].data["window"] == "left" &&
        session.records[1].data["window"] == "right", "stable explicit keys stored in header and input");
    file.close();
    int firstReplayed = 0, secondReplayed = 0;
    auto firstListener = first.window->events().mouseMoved.listen([&](const tc::MouseMoveEventArgs& e) {
        if (e.sourceTag == "replayed") { ++firstReplayed; check(e.pos == tc::Vec2(10, 20), "first coordinates delivered"); }
    });
    auto secondListener = second.window->events().mouseMoved.listen([&](const tc::MouseMoveEventArgs& e) {
        if (e.sourceTag == "replayed") { ++secondReplayed; check(e.pos == tc::Vec2(30, 40), "second coordinates delivered"); }
    });
    check(replay::play(path), "play multi-window session");
    s.timeline.start(session, 0); // Due immediately with the real Window injection path.
    s.update();
    check(!s.timeline.active && firstReplayed == 1 && secondReplayed == 1, "two windows replay to distinct mapped destinations");
    int warnings = 0;
    auto log = tc::getLogger().onLog.listen([&](const tc::LogEventArgs& e) {
        if (e.level == tc::LogLevel::Warning) ++warnings;
    });
    check(!replay::flush(path) && !replay::flush(path) && warnings == 2, "flush while stopped warns every time");
    tc::mcp::internal::grantToolLevelWithoutOpening(tc::mcp::ToolLevel::ReadOnly);
    auto denied = request("tools/call", {{"name", "tcx_inputreplay_start_recording"}, {"arguments", Json::object()}});
    check(denied.dump().find("requires Control") != std::string::npos && !s.recording(), "MCP requires Control grant");
    tc::mcp::internal::grantToolLevelWithoutOpening(tc::mcp::ToolLevel::Control);
    for (const auto* suffix : {"start_recording", "stop_recording", "play", "stop_playback", "flush"})
        check(tc::mcp::hasTool(std::string("tcx_inputreplay_") + suffix), "setup registers MCP tool");
    check(call("start_recording", {{"durationMs", 1000}, {"capacity", 4}})["status"] == "ok" && s.ringRecording, "MCP ring start");
    check(call("start_recording")["status"] == "error", "MCP redundant start returns error text");
    check(call("flush", {{"path", tc::pathToUtf8(path)}})["status"] == "ok" && !s.recording(), "MCP flush saves and stops");
    check(call("flush")["status"] == "error", "MCP stopped flush reports error");
    check(call("play", {{"path", tc::pathToUtf8(path)}, {"scale", "fit"}})["status"] == "ok" && s.timeline.active, "MCP play");
    check(call("stop_playback")["status"] == "ok" && !s.timeline.active, "MCP stop playback");
    check(call("play", {{"path", tc::pathToUtf8(path)}, {"scale", "invalid"}})["status"] == "error", "MCP rejects invalid scale");
    check(call("start_recording", {{"path", tc::pathToUtf8(path)}})["status"] == "ok" && s.output.is_open(), "MCP file recording");
    check(call("stop_recording")["status"] == "ok" && !s.recording(), "MCP stop recording");
    check(call("play", {{"path", "missing-inputreplay.jsonl"}})["message"] == "cannot open playback file", "MCP file error text");
#ifndef __EMSCRIPTEN__
    tc::mcp::startHttpServer();
    const int port = tc::mcp::getHttpPort();
    if (port > 0) {
        std::atomic<bool> done{false};
        bool repliesOk = true;
        std::thread client([&] {
            httplib::Client connection("127.0.0.1", port);
            connection.set_read_timeout(5);
            const std::vector<std::pair<std::string, Json>> calls{
                {"start_recording", {{"durationMs", 1000}, {"capacity", 4}}},
                {"flush", {{"path", tc::pathToUtf8(path)}}},
                {"play", {{"path", tc::pathToUtf8(path)}}},
                {"stop_playback", Json::object()},
                {"start_recording", {{"path", tc::pathToUtf8(path)}}},
                {"stop_recording", Json::object()}};
            for (const auto& [name, args] : calls) {
                const auto body = Json{{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/call"},
                    {"params", {{"name", "tcx_inputreplay_" + name}, {"arguments", args}}}}.dump();
                auto response = connection.Post("/mcp", body, "application/json");
                try {
                    if (!response || response->status != 200) { repliesOk = false; break; }
                    auto reply = Json::parse(response->body);
                    repliesOk = repliesOk && Json::parse(reply.at("result").at("content").at(0).at("text").get<std::string>())["status"] == "ok";
                } catch (...) { repliesOk = false; break; }
            }
            done.store(true);
        });
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        while (!done.load() && std::chrono::steady_clock::now() < deadline) {
            tc::mcp::processHttpQueue();
            std::this_thread::yield();
        }
        tc::mcp::stopHttpServer();
        client.join();
        check(repliesOk && done.load(), "all five MCP controls through loopback HTTP queue");
    } else {
        std::printf("MCP loopback unavailable; core JSON-RPC path tested directly\n");
    }
#endif
    check(replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1)), "start auto flush ring");
    const auto folder = std::filesystem::current_path() / "inputreplay-auto";
    replay::setFlushDirectory(folder);
    check(replay::flush(), "timestamped flush creates configured folder");
    size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(folder)) {
        std::ifstream saved(entry.path());
        check(load(saved).records.back().type == "end", "automatic flush loads");
        ++files;
    }
    check(files == 1, "one automatic timestamped file");
    std::filesystem::remove_all(folder);
    s.flushDirectory = "input-replay";
    check(replay::startRecording(replay::RecordingMode::Ring, std::chrono::minutes(1)), "start ring before write error");
    check(!replay::flush(path / "cannot-write.jsonl") && !s.recording(), "failed flush also stops ring");
    s.timeline.start(session, 0);
    tc::internal::WindowRequestAccess::unbind(*second.window);
    s.update();
    check(!s.timeline.active && s.lastError == "mapped window is closed", "closed mapping stops before next injection");
    s.cleanup();
    replay::setup({{"left", first.window.get()}});
    std::ofstream saved(path);
    saved << session.header.dump() << '\n';
    for (const auto& record : session.records) saved << record.data.dump() << '\n';
    saved.close();
    check(!replay::play(path) && s.lastError == "missing window mapping", "missing recorded mapping rejects play before injection");
    s.cleanup();
    replay::setup({{"left", second.window.get()}});
    check(!s.window, "closed mapping rejects setup");
    replay::setup({{"left", first.window.get()}});
    check(replay::startRecording(path) && replay::stopRecording() && replay::play(path), "play before setup window closure");
    tc::internal::WindowRequestAccess::unbind(*first.window);
    s.cleanup();
    check(!s.timeline.active && s.lastError == "mapped window is closed", "setup window closure reports playback error during cleanup");
    std::filesystem::remove(path);
}
}
int runInputReplayB1WindowChecks() {
    windowChecks();
    std::printf("B1 windows/MCP: %d checks, %d failures\n", checks, failures);
    return failures;
}
