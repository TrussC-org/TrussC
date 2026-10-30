// =============================================================================
// core/tests/mcpHttpGuard — behavioral regression test for the MCP HTTP
// server's browser-facing checks (#238).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: a web page in the user's browser cannot drive the
// loopback MCP server. It can SEND requests without CORS, so the server itself
// must refuse them:
// - a Host that is not localhost / 127.0.0.1 / [::1] (DNS rebinding) -> 403
// - an Origin other than the server's own (another localhost port too) -> 403
// - a POST whose Content-Type is not application/json                  -> 415
// Native MCP clients (no Origin, JSON body, localhost Host) keep working.
//
// The server runs with a token, so /mcp also needs "Authorization: Bearer
// <token>": a missing, shorter, longer or different token -> 401. The token
// comparison helper (mcp::internal::constantTimeEquals) is also checked directly.
//
// The main thread pumps mcp::processHttpQueue() the way the frame loop does;
// the requests run on a worker with a deadline.
//
// Also guards the port line (#311, #414): once the server has bound its port it
// logs "[MCP] HTTP server listening on http://HOST:PORT/mcp" through the
// Logger at Notice, exactly once, with the actual (here OS-assigned) port, so
// the line reaches onLog listeners, stdout and the log file, with no raw
// stderr copy. A Warning console level hides it only on the console.
//
// Also checks that the MCP server reports a port that is already in use: on a
// fixed port another server listens on, it fails to bind, logs exactly one
// "Failed to bind" error, and the other server keeps answering.
//
// Also checks the default host: mcp::startHttpServer() with no host argument
// listens on 127.0.0.1 (the port line names it, and a client connecting to
// 127.0.0.1 gets an answer), the same on every OS.
// =============================================================================

#include <TrussC.h>
#ifndef __EMSCRIPTEN__
#include "impl/httplib.h"
#endif
#include "../../common/tcCoreTest.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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

// A port that is already in use. The first server listens on a port the OS
// picked; mcp::startHttpServer() on that same port must fail to bind and log
// exactly one "Failed to bind" error, and the first server must keep
// answering every request. Both use 127.0.0.1.
static void testPortInUse() {
    httplib::Server first;
    first.Get("/", [](const httplib::Request&, httplib::Response& res) {
        res.set_content("first", "text/plain");
    });
    const int fixedPort = first.bind_to_any_port("127.0.0.1");
    check("first server bound to a port", fixedPort > 0);
    if (fixedPort <= 0) return;
    thread firstThread([&] { first.listen_after_bind(); });
    first.wait_until_ready();

    atomic<int> bindErrors{0};
    EventListener listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("Failed to bind") != string::npos) {
            ++bindErrors;
        }
    });

    mcp::startHttpServer(fixedPort, "127.0.0.1");
    for (int i = 0; i < 300 && bindErrors.load() == 0; i++) {
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    check("port in use -> exactly one \"Failed to bind\" error", bindErrors.load() == 1,
          to_string(bindErrors.load()) + " errors");

    httplib::Client cli("127.0.0.1", fixedPort);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(10);
    const int requests = 20;
    int answeredByFirst = 0;
    for (int i = 0; i < requests; i++) {
        auto r = cli.Get("/");
        if (r && r->status == 200 && r->body == "first") ++answeredByFirst;
    }
    check("port in use -> first server answers every request", answeredByFirst == requests,
          to_string(answeredByFirst) + " of " + to_string(requests));

    listener.disconnect();
    mcp::stopHttpServer();
    first.stop();
    firstThread.join();
}

// The default host. Started with no arguments, the server picks a port, logs
// "[MCP] HTTP server listening on http://127.0.0.1:PORT/mcp" and answers a
// client that connects to the 127.0.0.1 address.
static void testDefaultHost() {
    mutex logMutex;
    string portLine;
    EventListener listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.message.find("HTTP server listening on") == string::npos) return;
        lock_guard<mutex> lock(logMutex);
        if (portLine.empty()) portLine = e.message;
    });

    mcp::startHttpServer();
    string line;
    for (int i = 0; i < 500 && line.empty(); i++) {
        this_thread::sleep_for(chrono::milliseconds(10));
        lock_guard<mutex> lock(logMutex);
        line = portLine;
    }
    listener.disconnect();

    const string prefix = "[MCP] HTTP server listening on http://127.0.0.1:";
    const string suffix = "/mcp";
    int port = 0;
    if (line.size() > prefix.size() + suffix.size() &&
        line.compare(0, prefix.size(), prefix) == 0 &&
        line.compare(line.size() - suffix.size(), suffix.size(), suffix) == 0) {
        port = atoi(line.substr(prefix.size(), line.size() - prefix.size() - suffix.size()).c_str());
    }
    check("default host -> port line names http://127.0.0.1:PORT/mcp", port > 0, line);
    check("default host -> port line has the bound port", port > 0 && port == mcp::getHttpPort(),
          to_string(port) + " vs " + to_string(mcp::getHttpPort()));

    if (port > 0) {
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(10);
        auto r = cli.Get("/");
        check("default host -> answers on 127.0.0.1", r && r->status == 200,
              r ? to_string(r->status) : httplib::to_string(r.error()));
        check("default host -> answer is the MCP server info",
              r && r->body.find("TrussC MCP Server") != string::npos, r ? r->body : "");
    }

    mcp::stopHttpServer();
}

// Port discovery uses the Logger outputs, including a file when the console
// filters out Notice. Capture only synchronous startup, before making checks.
static void testPortLogOutputs() {
    const auto consoleLevel = getLogger().getConsoleLogLevel();
    const auto fileLevel = getLogger().getFileLogLevel();
    const fs::path logPath = fs::temp_directory_path() /
        ("trussc_mcpPortOutputs_" +
         to_string(chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    setFileLogLevel(LogLevel::Notice);
    for (auto level : {LogLevel::Notice, LogLevel::Warning}) {
        check("port discovery log file opened", setLogFile(logPath).ok());
        setConsoleLogLevel(level);
        ostringstream capturedOut, capturedErr;
        auto* savedOut = cout.rdbuf(capturedOut.rdbuf());
        auto* savedErr = cerr.rdbuf(capturedErr.rdbuf());
        mcp::startHttpServer();
        cout.rdbuf(savedOut);
        cerr.rdbuf(savedErr);

        const int port = mcp::getHttpPort();
        check("port discovery server bound", port > 0);
        const string portLine = "[NOTICE] [MCP] HTTP server listening on http://127.0.0.1:" +
            to_string(port) + "/mcp\n";
        const auto output = capturedOut.str();
        const auto position = output.find(portLine);
        if (level == LogLevel::Notice) {
            check("stdout contains the Logger port Notice exactly once", position != string::npos &&
                  output.find(portLine, position + portLine.size()) == string::npos);
        } else {
            check("Warning console level hides the port Notice",
                  output.find("HTTP server listening on") == string::npos);
        }
        check("startup has no raw stderr port copy", capturedErr.str().empty(), capturedErr.str());
        mcp::stopHttpServer();
        closeLogFile();
        ifstream in(logPath);
        stringstream text;
        text << in.rdbuf();
        in.close();
        check("log file exposes the port at either console level", text.str().find(portLine) != string::npos);
        error_code ec;
        fs::remove(logPath, ec);
    }
    setConsoleLogLevel(consoleLevel);
    setFileLogLevel(fileLevel);
}

static const string kListTools = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
static const string kToken = "test-token-0123456789abcdef";


void setEnv(const char* key, const char* value) {
#ifdef _WIN32
    _putenv_s(key, value ? value : "");
#else
    if (value) setenv(key, value, 1);
    else unsetenv(key);
#endif
}

// Each opener gets a fresh process: initializeMcp() is host-once state.
int firstOpenerCase(const string& mode) {
    const string token = mode == "first-explicit" ? kToken : "";
    if (mode == "first-grant") mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    else if (mode == "first-explicit") mcp::startHttpServer(0, "127.0.0.1", token);
    else {
        if (mode == "first-environment") setEnv("TRUSSC_MCP", "1");
        mcp::internal::initializeMcp();
    }
    const int port = mcp::getHttpPort();
    check("first opener binds a listener", port > 0);
    if (port <= 0) return 1;
    const string opener = mode == "first-grant" ? "the grant" :
        mode == "first-explicit" ? "startHttpServer()" :
        mode == "first-environment" ? "the environment" : "the dev build";
    const string expected = "[MCP] already listening on 127.0.0.1:" + to_string(port) +
        ", opened by " + opener +
        "; call startHttpServer(port, host, token) before setToolLevel() to choose the address";
    vector<pair<LogLevel, string>> logs;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        logs.emplace_back(e.level, e.message);
    });
    auto* server = mcp::internal::getHttpServer().get();
    auto* serverThread = mcp::internal::getHttpThread().get();
    const bool healthOnly = mcp::internal::healthOnly();
    const bool explicitStart = mcp::internal::explicitlyOpened();
    const auto level = mcp::getToolLevel();
    auto reject = [&](int requestedPort, const string& host, const string& requestedToken) {
        logs.clear();
        mcp::startHttpServer(requestedPort, host, requestedToken);
        check("different listener parameters log exactly one Error naming first opener",
              logs.size() == 1 && logs[0].first == LogLevel::Error && logs[0].second == expected);
        check("rejected start preserves listener, policy and authentication",
              mcp::getHttpPort() == port && mcp::internal::getHttpServer().get() == server &&
              mcp::internal::getHttpThread().get() == serverThread &&
              mcp::internal::mcpAuthToken() == token && mcp::internal::mcpLoopbackOnly() &&
              mcp::internal::healthOnly() == healthOnly &&
              mcp::internal::explicitlyOpened() == explicitStart && mcp::getToolLevel() == level);
    };
    reject(0, "localhost", token);
    reject(port == 65535 ? port - 1 : port + 1, "127.0.0.1", token);
    reject(0, "127.0.0.1", token + "different");

    // Verify actual HTTP replies and auth, including health-only filtering.
    auto reply = async(launch::async, [&] {
        httplib::Client client("127.0.0.1", port);
        client.set_connection_timeout(5);
        client.set_read_timeout(5);
        client.set_bearer_token_auth(token);
        return client.Post("/mcp", kListTools, "application/json");
    });
    while (reply.wait_for(chrono::milliseconds(0)) != future_status::ready) {
        mcp::processHttpQueue();
        this_thread::yield();
    }
    auto response = reply.get();
    check("original listener still serves HTTP with original token", response && response->status == 200);
    if (response && response->status == 200) {
        auto listed = json::parse(response->body)["result"]["tools"];
        check("rejected explicit start leaves health endpoint restricted", !healthOnly ||
              (listed.size() == 1 && listed[0]["name"] == "tc_get_health"));
    }
    // An accepted explicit opt-in may widen health-only exposure, as before.
    // Once opted in, identical calls and the actual assigned port are no-ops.
    mcp::startHttpServer(0, "127.0.0.1", token);
    logs.clear();
    mcp::startHttpServer(0, "127.0.0.1", token);
    mcp::startHttpServer(port, "127.0.0.1", token);
    check("matching starts produce no logs and do not restart",
          logs.empty() && mcp::internal::getHttpServer().get() == server &&
          mcp::getHttpPort() == port && mcp::internal::getHttpThread().get() == serverThread);
    // A successful repeat must not replace the original opener in diagnostics.
    logs.clear();
    mcp::startHttpServer(0, "localhost", token);
    check("matching explicit opt-in preserves original opener",
          logs.size() == 1 && logs[0].second == expected);
    listener.disconnect();
    mcp::stopHttpServer();
    return g_fail ? 1 : 0;
}

bool headlessGrantInSetup = false;
bool headlessSetupDone = false;
struct HeadlessMcpApp : App {
    future<vector<json>> replies;
    void setup() override {
        check("headless setup precedes automatic MCP startup", !mcp::internal::getHttpServer());
        if (headlessGrantInSetup) mcp::setToolLevel(mcp::ToolLevel::Control);
        mcp::tool("headless_public", "").bind([]() -> json { return "public"; });
        mcp::tool("headless_control", "").level(mcp::ToolLevel::Control)
            .bind([]() -> json { return "control"; });
        headlessSetupDone = true;
    }
    void update() override {
        const bool dev = mcp::internal::isDevBuild();
        const bool closed = mcp::internal::environmentClosesMcp();
        if (!replies.valid()) {
            check("headless startup listener follows environment/build", (mcp::getHttpPort() > 0) == !closed);
            if (closed || mcp::getHttpPort() <= 0) {
                exitApp();
                return;
            }
            check("headless endpoint uses loopback", mcp::internal::mcpLoopbackOnly());
            replies = async(launch::async, [] {
                httplib::Client client("127.0.0.1", mcp::getHttpPort());
                client.set_connection_timeout(5);
                client.set_read_timeout(5);
                vector<json> results;
                auto exchange = [&](const string& method, const json& params) {
                    const auto result = client.Post("/mcp", json{{"jsonrpc", "2.0"}, {"id", 1},
                        {"method", method}, {"params", params}}.dump(), "application/json");
                    results.push_back(result && result->status == 200
                        ? json::parse(result->body) : json::object());
                };
                exchange("tools/list", json::object());
                for (const char* name : {"tc_get_health", "headless_public", "headless_control"})
                    exchange("tools/call", {{"name", name}, {"arguments", json::object()}});
                return results;
            });
        }
        if (replies.wait_for(chrono::milliseconds(0)) == future_status::ready) {
            const auto results = replies.get();
            const bool full = dev || headlessGrantInSetup;
            const auto listed = results[0].value("result", json::object()).value("tools", json::array());
            check("headless loop services HTTP", !listed.empty());
            check("headless environment lists health only", full ||
                  (listed.size() == 1 && listed[0]["name"] == "tc_get_health"));
            auto has = [&](const string& name) {
                return any_of(listed.begin(), listed.end(), [&](const json& tool) { return tool["name"] == name; });
            };
            check("headless full endpoint exposes Public, ReadOnly and Control tools",
                  has("headless_public") == full && has("tc_get_status") == full &&
                  has("headless_control") == full && has("tc_mouse_click") == full);
            const auto healthResult = results[1].value("result", json::object());
            check("headless health call succeeds", healthResult.contains("content"));
            if (healthResult.contains("content")) {
                const auto health = json::parse(healthResult["content"][0]["text"].get<string>());
                check("headless health reports build and grant", health["buildKind"] == (dev ? "dev" : "default") &&
                      health["toolLevel"] == (full ? "Control" : "Public"));
            }
            for (size_t i : {2u, 3u}) {
                check("headless app tool call follows code/dev grant", results[i].contains("result") &&
                      results[i]["result"].value("isError", false) == !full);
            }
            exitApp();
        }
    }
};

int environmentCase(const string& mode) {
    for (const char* key : {"TRUSSC_MCP", "TRUSSC_MCP_PORT", "TRUSSC_MCP_HOST", "TRUSSC_MCP_TOKEN"})
        setEnv(key, nullptr);
    if (mode.rfind("first-", 0) == 0) return firstOpenerCase(mode);
    if (mode.rfind("headless", 0) == 0) {
        const bool dev = mcp::internal::isDevBuild();
        const bool closed = mode == "headless-closed";
        headlessGrantInSetup = mode == "headless-grant";
        headlessSetupDone = false;
        if (mode == "headless-port") setEnv("TRUSSC_MCP_PORT", "0");
        else if (mode != "headless-dev") setEnv("TRUSSC_MCP", closed ? "0" : "1");
        int healthNotices = 0, devWarnings = 0, exposureNotices = 0;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            const bool notice = e.level == LogLevel::Notice && e.message ==
                "[MCP] MCP: health only (ToolLevel Public). The supervisor gets logs, restart and hang detection; for screenshots and status add mcp::setToolLevel(mcp::ToolLevel::ReadOnly) in setup().";
            const bool warning = e.level == LogLevel::Warning && e.message.find("MCP development build:") != string::npos;
            if (notice) ++healthNotices;
            if (warning) ++devWarnings;
            const bool exposure = e.level == LogLevel::Notice && e.message.find("[MCP] ToolLevel ") == 0;
            if (exposure) {
                ++exposureNotices;
                check("exposure Notice includes tool registered after setup grant",
                      e.message.find("headless_control") != string::npos);
            }
            if (notice || warning || exposure) check("headless startup log follows setup", headlessSetupDone);
        });
        check("headless runner succeeds", runHeadlessApp<HeadlessMcpApp>() == 0);
        check("headless health-only Notice occurs exactly once", healthNotices == (!dev && !headlessGrantInSetup && !closed ? 1 : 0));
        check("headless exposure Notice occurs exactly once when full",
              exposureNotices == (!closed && (dev || headlessGrantInSetup) ? 1 : 0));
        check("headless dev warning occurs exactly once, even when disabled", devWarnings == (dev ? 1 : 0));
        check("headless cleanup stops HTTP", !mcp::internal::getHttpServer() && mcp::getHttpPort() == 0);
        return g_fail ? 1 : 0;
    }
    const bool dev = mcp::internal::isDevBuild();
    const bool closed = mode == "closed" || mode == "closed-grant" || mode == "closed-explicit";
    const bool downgrade = mode.rfind("downgrade-", 0) == 0;
    const bool grant = downgrade || mode == "grant" || mode == "grant-port" || mode == "exit-cleanup" || mode == "closed-grant";
    const bool explicitStart = mode == "explicit" || mode == "closed-explicit" || mode == "downgrade-explicit";
    const bool envAsk = mode == "env" || mode == "port" || mode == "upgrade" || mode == "upgrade-grant" || closed;
    if (closed) setEnv("TRUSSC_MCP", "0");
    else if (mode == "env" || mode == "upgrade" || mode == "upgrade-grant" || mode == "downgrade-env") setEnv("TRUSSC_MCP", "1");
    if (mode == "port" || mode == "downgrade-port" || closed) setEnv("TRUSSC_MCP_PORT", "0");
    int configuredPort = 0;
    if (mode == "port" || mode == "grant-port") {
        httplib::Server probe;
        configuredPort = probe.bind_to_any_port("127.0.0.1");
        if (configuredPort <= 0) return 1;
        thread probeThread([&] { probe.listen_after_bind(); });
        probe.wait_until_ready();
        probe.stop();
        probeThread.join();
        setEnv("TRUSSC_MCP_PORT", to_string(configuredPort).c_str());
    }
    // A remote bind/token must be ignored by every default-build env path.
    if (mode != "none") {
        setEnv("TRUSSC_MCP_HOST", "0.0.0.0");
        setEnv("TRUSSC_MCP_TOKEN", "environment-token");
    }

    int healthNotices = 0, devWarnings = 0;
    vector<string> exposureNotices;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Notice && e.message ==
            "[MCP] MCP: health only (ToolLevel Public). The supervisor gets logs, restart and hang detection; for screenshots and status add mcp::setToolLevel(mcp::ToolLevel::ReadOnly) in setup().") ++healthNotices;
        if (e.level == LogLevel::Warning && e.message.find("MCP development build:") != string::npos) ++devWarnings;
        if (e.level == LogLevel::Notice && e.message.find("[MCP] ToolLevel ") == 0)
            exposureNotices.push_back(e.message);
    });
    mcp::resource("test://public", "public resource").bind([] { return std::string("resource"); });
    mcp::tool("public_probe", "").bind([]() -> json { return "public"; });
    mcp::tool("control_probe", "").level(mcp::ToolLevel::Control).bind([]() -> json { return "control"; });
    check("registration alone never starts HTTP", !mcp::internal::getHttpServer());
    if (explicitStart) mcp::startHttpServer();
    if (grant) mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    check("code opt-in opens immediately unless disabled",
          bool(mcp::internal::getHttpServer()) == ((grant || explicitStart) && !closed));
    check("no exposure Notice before initialization", exposureNotices.empty());
    mcp::internal::initializeMcp();
    mcp::internal::initializeMcp(); // Host initialization is idempotent.
    const bool listening = !closed && (dev || grant || explicitStart || envAsk);
    check("startup listener matches environment/code/build", (mcp::getHttpPort() > 0) == listening);
    const bool healthOnly = listening && !dev && !grant && !explicitStart;
    check("initialization logs full exposure once", exposureNotices.size() == (listening && !healthOnly ? 1u : 0u));
    check("health-only Notice occurs exactly once", healthNotices == (healthOnly ? 1 : 0));
    check("dev warning follows build even when disabled", devWarnings == (dev ? 1 : 0));
    check("main title marks dev builds", (mcp::internal::markedWindowTitle("Test") == (dev ? "Test [MCP]" : "Test")));
    check("main title does not duplicate marker", mcp::internal::markedWindowTitle("Test [MCP]") == "Test [MCP]");
    if (!listening) return g_fail ? 1 : 0;
    check("PORT selects the requested listener", !configuredPort || mcp::getHttpPort() == configuredPort);
    const bool devEnvironment = dev && !explicitStart && mode != "none";
    check("HOST/TOKEN honored only for dev auto-start", mcp::internal::mcpLoopbackOnly() == !devEnvironment &&
          mcp::internal::mcpAuthToken() == (devEnvironment ? "environment-token" : ""));

    auto exchange = [](const string& method, const json& params = json::object()) {
        auto result = async(launch::async, [=] {
            httplib::Client client("127.0.0.1", mcp::getHttpPort());
            client.set_connection_timeout(5);
            client.set_read_timeout(5);
            client.set_bearer_token_auth(mcp::internal::mcpAuthToken());
            return client.Post("/mcp", json{{"jsonrpc", "2.0"}, {"id", 1},
                {"method", method}, {"params", params}}.dump(), "application/json");
        });
        const auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
        while (result.wait_for(chrono::milliseconds(0)) != future_status::ready &&
               chrono::steady_clock::now() < deadline) {
            mcp::processHttpQueue();
            this_thread::yield();
        }
        auto reply = result.get();
        check("native HTTP request succeeds", reply && reply->status == 200);
        return reply ? json::parse(reply->body) : json::object();
    };
    const auto listed = exchange("tools/list")["result"]["tools"];
    check("health endpoint lists exactly one tool", !healthOnly ||
          (listed.size() == 1 && listed[0]["name"] == "tc_get_health"));
    check("health endpoint hides app resources", exchange("resources/list")["result"]["resources"].empty() == healthOnly);
    check("health endpoint refuses app resource reads", exchange("resources/read", {{"uri", "test://public"}}).contains("error") == healthOnly);
    auto call = [&](const string& name) { return exchange("tools/call", {{"name", name}, {"arguments", json::object()}}); };
    const auto health = json::parse(call("tc_get_health")["result"]["content"][0]["text"].get<string>());
    check("health reports actual build kind", health["buildKind"] == (dev ? "dev" : "default"));
    check("health reports actual grant", health["toolLevel"] == mcp::internal::toolLevelName(mcp::getToolLevel()));
    check("Public app tools require code or dev opt-in", call("public_probe")["result"].value("isError", false) == healthOnly);
    check("ReadOnly tools require a grant", call("tc_get_status")["result"].value("isError", false) == (!dev && !grant));
    check("Control tools require Control", call("control_probe")["result"].value("isError", false) == !dev);
    if (mode == "upgrade" || mode == "upgrade-grant") {
        const int port = mcp::getHttpPort();
        const auto noticesBefore = exposureNotices.size();
        if (mode == "upgrade") mcp::startHttpServer(0, dev ? "0.0.0.0" : "127.0.0.1",
                                                   dev ? "environment-token" : "");
        else mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        check("health-only to full logs exposure once", exposureNotices.size() == noticesBefore + (dev ? 0u : 1u));
        check("exposure Notice lists app tool", !exposureNotices.empty() &&
              exposureNotices.back().find("public_probe") != string::npos);
        check("upgrade opens app tools without rebinding", mcp::getHttpPort() == port &&
              !call("public_probe")["result"].value("isError", false));
        const auto afterUpgrade = exposureNotices.size();
        if (mode == "upgrade") mcp::startHttpServer(0, dev ? "0.0.0.0" : "127.0.0.1",
                                                   dev ? "environment-token" : "");
        else mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        check("repeated opt-in does not duplicate exposure Notice", exposureNotices.size() == afterUpgrade);
        mcp::setToolLevel(mcp::ToolLevel::Control);
        check("widening grant logs once", exposureNotices.size() == afterUpgrade + (dev ? 0u : 1u));
        check("widened exposure Notice lists Control app tool", exposureNotices.back().find("control_probe") != string::npos);
        const auto afterWidening = exposureNotices.size();
        mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        check("narrowing grant does not log exposure", exposureNotices.size() == afterWidening);
    }
    if (downgrade) {
        const int port = mcp::getHttpPort();
        const auto noticesBefore = exposureNotices.size();
        mcp::setToolLevel(mcp::ToolLevel::Public);
        check("dev grant stays Control on Public downgrade", mcp::getToolLevel() ==
              (dev ? mcp::ToolLevel::Control : mcp::ToolLevel::Public));
        check("Public downgrade does not log exposure", exposureNotices.size() == noticesBefore);
        const bool remainsOpen = dev || mode != "downgrade-none";
        const bool narrowsToHealth = !dev && (mode == "downgrade-env" || mode == "downgrade-port");
        check("Public downgrade preserves or stops listener as requested", mcp::getHttpPort() == (remainsOpen ? port : 0) &&
              bool(mcp::internal::getHttpServer()) == remainsOpen);
        check("Public downgrade narrows only grant-opened environment listeners", mcp::internal::healthOnly() == narrowsToHealth);
        if (remainsOpen) {
            const auto tools = exchange("tools/list")["result"]["tools"];
            check("downgraded health listener lists only health", !narrowsToHealth ||
                  (tools.size() == 1 && tools[0]["name"] == "tc_get_health"));
            check("downgraded Public tool call follows listener policy",
                  call("public_probe")["result"].value("isError", false) == narrowsToHealth);
            check("downgraded resources follow listener policy",
                  exchange("resources/list")["result"]["resources"].empty() == narrowsToHealth);
        } else {
            httplib::Client client("127.0.0.1", port);
            client.set_connection_timeout(1);
            check("stopped listener refuses connections", !client.Get("/"));
        }
        mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        check("renewed grant logs reopened or widened exposure", exposureNotices.size() == noticesBefore + (dev ? 0u : 1u));
        check("renewed grant serves HTTP again", !call("public_probe")["result"].value("isError", false));
    }
    if (mode == "exit-cleanup") return g_fail ? 1 : 0; // exercise host exit hook
    mcp::stopHttpServer();
    check("stop clears reported listener port", mcp::getHttpPort() == 0);
    if (mode == "downgrade-explicit") {
        mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
        mcp::setToolLevel(mcp::ToolLevel::Public);
        check("explicit-start flag does not survive stop", bool(mcp::internal::getHttpServer()) == dev);
        mcp::stopHttpServer();
    }
    if (mode == "explicit") {
        const auto noticesBefore = exposureNotices.size();
        mcp::startHttpServer();
        check("explicit reopening after initialization logs exposure once", exposureNotices.size() == noticesBefore + 1);
        check("explicit reopening serves HTTP", !call("public_probe")["result"].value("isError", false));
        mcp::stopHttpServer();
    }
    return g_fail ? 1 : 0;
}

string quoteArgument(const string& value) {
#ifdef _WIN32
    // Executable paths cannot contain a quote on Windows.
    return "\"" + value + "\"";
#else
    string quoted = "'";
    for (char c : value) quoted += c == '\'' ? "'\\''" : string(1, c);
    return quoted + "'";
#endif
}

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc == 3 && string(argv[1]) == "--environment") return environmentCase(argv[2]);
    string command = quoteArgument(fs::absolute(argv[0]).string());
#ifdef TC_CORE_TEST_NAME
    command += " " TC_CORE_TEST_NAME;
#endif
    for (const char* mode : {"first-grant", "first-explicit", "first-environment", "headless", "headless-port", "headless-grant", "headless-closed", "none", "host-only", "env", "port", "grant", "grant-port", "exit-cleanup", "explicit", "upgrade", "upgrade-grant", "downgrade-explicit", "downgrade-env", "downgrade-port", "downgrade-none", "closed", "closed-grant", "closed-explicit"})
        check(string("environment case: ") + mode,
              std::system((command + " --environment " + mode).c_str()) == 0);

    if (mcp::internal::isDevBuild())
        check("environment case: first-dev",
              std::system((command + " --environment first-dev").c_str()) == 0);

    if (mcp::internal::isDevBuild())
        check("environment case: headless-dev",
              std::system((command + " --environment headless-dev").c_str()) == 0);

    // Token comparison helper, no server needed.
    {
        using mcp::internal::constantTimeEquals;
        using mcp::internal::bearerTokenMatches;
        check("equal strings -> equal", constantTimeEquals("abc123", "abc123"));
        check("both empty -> equal", constantTimeEquals("", ""));
        check("same length, last byte differs -> not equal", !constantTimeEquals("abc123", "abc124"));
        check("same length, first byte differs -> not equal", !constantTimeEquals("abc123", "xbc123"));
        check("shorter first -> not equal", !constantTimeEquals("abc", "abc123"));
        check("longer first -> not equal", !constantTimeEquals("abc123", "abc"));
        check("empty vs non-empty -> not equal", !constantTimeEquals("", "a"));
        check("non-empty vs empty -> not equal", !constantTimeEquals("a", ""));
        check("trailing NUL is not padding -> not equal",
              !constantTimeEquals(string_view("abc\0", 4), "abc"));
        check("embedded NUL, equal -> equal",
              constantTimeEquals(string_view("a\0b", 3), string_view("a\0b", 3)));
        check("Bearer <token> -> match", bearerTokenMatches("Bearer " + kToken, kToken));
        check("token without scheme -> no match", !bearerTokenMatches(kToken, kToken));
        check("scheme only -> no match", !bearerTokenMatches("Bearer ", kToken));
        check("scheme without space -> no match", !bearerTokenMatches("Bearer" + kToken, kToken));
        check("empty header -> no match", !bearerTokenMatches("", kToken));
    }

    // Capture the caller-thread startup logs and send them to a log file too.
    const auto callerThread = this_thread::get_id();
    atomic<bool> startupOnCaller{true};
    mutex logMutex;
    vector<pair<LogLevel, string>> logLines;
    EventListener logListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        lock_guard<mutex> lock(logMutex);
        logLines.emplace_back(e.level, e.message);
        if (this_thread::get_id() != callerThread) startupOnCaller = false;
    });
    const fs::path logPath = fs::temp_directory_path() /
        ("trussc_mcpHttpGuard_" +
         to_string(chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    check("log file opened", setLogFile(logPath).ok());

    mcp::startHttpServer(0, "127.0.0.1", kToken);
    mcp::setToolLevel(mcp::ToolLevel::ReadOnly);
    check("startup logs run on the calling thread", startupOnCaller.load());
    int port = 0;
    for (int i = 0; i < 500 && port <= 0; i++) {
        this_thread::sleep_for(chrono::milliseconds(10));
        port = mcp::getHttpPort();
    }
    check("server bound to a port", port > 0);
    if (port <= 0) return 1;
    const string p = to_string(port);

    // The port line is delivered synchronously before startHttpServer returns.
    {
        const string portLine = "[MCP] HTTP server listening on http://127.0.0.1:" + p + "/mcp";
        vector<pair<LogLevel, string>> found;
        {
            lock_guard<mutex> lock(logMutex);
            for (auto& line : logLines) {
                if (line.second.find("HTTP server listening on") != string::npos) {
                    found.push_back(line);
                }
            }
        }
        check("port line goes through the Logger, once", found.size() == 1,
              to_string(found.size()) + " line(s)");
        check("port line text is \"" + portLine + "\"",
              !found.empty() && found[0].second == portLine,
              found.empty() ? "" : found[0].second);
        check("port line is a Notice",
              !found.empty() && found[0].first == LogLevel::Notice);

        closeLogFile();
        ifstream in(logPath);
        stringstream text;
        text << in.rdbuf();
        in.close();
        check("port line lands in the log file",
              text.str().find("[NOTICE] " + portLine + "\n") != string::npos, text.str());
        error_code ec;
        fs::remove(logPath, ec);
    }

    atomic<bool> done{false};
    thread worker([&] {
        // Connect to the listener's actual address. "localhost" tries ::1 first
        // on Windows, adding a fallback delay for an IPv4 listener.
        httplib::Client cli("127.0.0.1", port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(10);
        cli.set_bearer_token_auth(kToken);

        auto post = [&](const httplib::Headers& h, const string& contentType) {
            return cli.Post("/mcp", h, kListTools, contentType);
        };
        auto status = [](const httplib::Result& r) { return r ? r->status : -1; };

        // Native client: JSON, no Origin, Host filled in by the client.
        auto r = post({}, "application/json");
        check("native client (no Origin, application/json) -> 200", status(r) == 200, to_string(status(r)));
        check("native client gets a JSON-RPC reply", r && r->body.find("\"result\"") != string::npos, r ? r->body : "");
        check("Content-Type with charset -> 200", status(post({}, "application/json; charset=utf-8")) == 200);

        // Content-Type
        check("Content-Type text/plain -> 415", status(post({}, "text/plain")) == 415);
        check("Content-Type form-urlencoded -> 415", status(post({}, "application/x-www-form-urlencoded")) == 415);

        // Origin
        check("foreign Origin -> 403", status(post({{"Origin", "http://evil.example"}}, "application/json")) == 403);
        check("Origin null -> 403", status(post({{"Origin", "null"}}, "application/json")) == 403);
        check("own origin localhost -> 200", status(post({{"Origin", "http://localhost:" + p}}, "application/json")) == 200);
        check("own origin 127.0.0.1 -> 200", status(post({{"Origin", "http://127.0.0.1:" + p}}, "application/json")) == 200);
        check("localhost on another port -> 403", status(post({{"Origin", "http://localhost:5173"}}, "application/json")) == 403);

        // Host (DNS rebinding: the page's own name, resolving to 127.0.0.1)
        check("rebinding Host -> 403", status(post({{"Host", "evil.example:" + p}}, "application/json")) == 403);
        check("Host localhost:port -> 200", status(post({{"Host", "localhost:" + p}}, "application/json")) == 200);
        check("Host 127.0.0.1 -> 200", status(post({{"Host", "127.0.0.1"}}, "application/json")) == 200);
        check("Host [::1]:port -> 200", status(post({{"Host", "[::1]:" + p}}, "application/json")) == 200);
        check("Host localhost.evil.example -> 403", status(post({{"Host", "localhost.evil.example"}}, "application/json")) == 403);

        // Bearer token on /mcp. `raw` sends only the headers given here.
        {
            httplib::Client raw("127.0.0.1", port);
            raw.set_connection_timeout(5);
            raw.set_read_timeout(10);
            auto postAuth = [&](const string& value) {
                return status(raw.Post("/mcp", {{"Authorization", value}}, kListTools, "application/json"));
            };
            string sameLength = kToken;
            sameLength.back() = (sameLength.back() == 'x') ? 'y' : 'x';
            check("correct token -> 200", postAuth("Bearer " + kToken) == 200);
            check("no Authorization header -> 401",
                  status(raw.Post("/mcp", kListTools, "application/json")) == 401);
            check("same-length wrong token -> 401", postAuth("Bearer " + sameLength) == 401);
            check("token prefix -> 401", postAuth("Bearer " + kToken.substr(0, kToken.size() - 1)) == 401);
            check("token plus a byte -> 401", postAuth("Bearer " + kToken + "x") == 401);
            check("scheme only -> 401", postAuth("Bearer ") == 401);
            check("token without scheme -> 401", postAuth(kToken) == 401);
            check("GET / needs no token -> 200", status(raw.Get("/")) == 200);
        }

        // GET / (server info) takes the Host / Origin checks too
        check("GET / -> 200", status(cli.Get("/")) == 200);
        check("GET / rebinding Host -> 403", status(cli.Get("/", {{"Host", "evil.example"}})) == 403);
        check("GET / foreign Origin -> 403", status(cli.Get("/", {{"Origin", "http://evil.example"}})) == 403);

        done = true;
    });

    // The frame loop's part: answer queued requests on this thread.
    auto deadline = chrono::steady_clock::now() + chrono::seconds(30);
    while (!done && chrono::steady_clock::now() < deadline) {
        mcp::processHttpQueue();
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    check("finished before the deadline", done.load());
    if (!done) {
        worker.detach();
        return 1;
    }
    worker.join();
    mcp::stopHttpServer();
    logListener.disconnect();

    testPortInUse();
    testDefaultHost();
    testPortLogOutputs();

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
