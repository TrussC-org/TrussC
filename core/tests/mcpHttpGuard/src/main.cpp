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
// comparison helper (detail::constantTimeEquals) is also checked directly.
//
// The main thread pumps mcp::processHttpQueue() the way the frame loop does;
// the requests run on a worker with a deadline.
//
// Also guards the port line (#311): once the server has bound its port it
// logs "[MCP] HTTP server listening on http://HOST:PORT/mcp" through the
// Logger at Notice, exactly once, with the actual (here OS-assigned) port, so
// the line reaches onLog listeners and the log file, not only stderr.
//
// Also checks that the MCP server reports a port that is already in use: on a
// fixed port another server listens on, it fails to bind, logs exactly one
// "Failed to bind" error, and the other server keeps answering.
// =============================================================================

#include <TrussC.h>

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
    this_thread::sleep_for(chrono::milliseconds(100)); // a second error would land here
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

static const string kListTools = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";
static const string kToken = "test-token-0123456789abcdef";

int main() {
    // Token comparison helper, no server needed.
    {
        using mcp::detail::constantTimeEquals;
        using mcp::detail::bearerTokenMatches;
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

    // Capture the Logger's lines (onLog runs on the logging thread, here the
    // server thread) and send them to a log file too.
    mutex logMutex;
    vector<pair<LogLevel, string>> logLines;
    EventListener logListener = getLogger().onLog.listen([&](LogEventArgs& e) {
        lock_guard<mutex> lock(logMutex);
        logLines.emplace_back(e.level, e.message);
    });
    const fs::path logPath = fs::temp_directory_path() /
        ("trussc_mcpHttpGuard_" +
         to_string(chrono::steady_clock::now().time_since_epoch().count()) + ".log");
    check("log file opened", setLogFile(logPath));

    mcp::startHttpServer(0, "localhost", kToken);
    int port = 0;
    for (int i = 0; i < 500 && port <= 0; i++) {
        this_thread::sleep_for(chrono::milliseconds(10));
        port = mcp::getHttpPort();
    }
    check("server bound to a port", port > 0);
    if (port <= 0) return 1;
    const string p = to_string(port);

    // The port line: logged by the server thread right after it stores the
    // port, so wait for it.
    {
        const string portLine = "[MCP] HTTP server listening on http://localhost:" + p + "/mcp";
        vector<pair<LogLevel, string>> found;
        auto lineDeadline = chrono::steady_clock::now() + chrono::seconds(5);
        while (found.empty() && chrono::steady_clock::now() < lineDeadline) {
            this_thread::sleep_for(chrono::milliseconds(10));
            lock_guard<mutex> lock(logMutex);
            for (auto& line : logLines) {
                if (line.second.find("HTTP server listening on") != string::npos) {
                    found.push_back(line);
                }
            }
        }
        // A moment more, so a duplicate line would show up too.
        this_thread::sleep_for(chrono::milliseconds(100));
        {
            lock_guard<mutex> lock(logMutex);
            found.clear();
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
        // Same name the server bound to: "localhost" resolves to ::1 first on
        // some hosts (CI runners), where 127.0.0.1 would find nothing.
        httplib::Client cli("localhost", port);
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
            httplib::Client raw("localhost", port);
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

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
