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
// - an Origin other than the server's own or an mcp::allowOrigin() one -> 403
// - a POST whose Content-Type is not application/json                  -> 415
// Native MCP clients (no Origin, JSON body, localhost Host) keep working.
//
// The main thread pumps mcp::processHttpQueue() the way the frame loop does;
// the requests run on a worker with a deadline.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <string>
#include <thread>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok, const string& detail = "") {
    printf("%-64s %s%s\n", name.c_str(), ok ? "PASS" : "FAIL",
           ok || detail.empty() ? "" : ("  -- " + detail).c_str());
    fflush(stdout);
    if (!ok) ++g_fail;
}

static const string kListTools = R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})";

int main() {
    mcp::startHttpServer(0, "localhost");
    int port = 0;
    for (int i = 0; i < 500 && port <= 0; i++) {
        this_thread::sleep_for(chrono::milliseconds(10));
        port = mcp::getHttpPort();
    }
    check("server bound to a port", port > 0);
    if (port <= 0) return 1;
    const string p = to_string(port);

    atomic<bool> done{false};
    thread worker([&] {
        // Same name the server bound to: "localhost" resolves to ::1 first on
        // some hosts (CI runners), where 127.0.0.1 would find nothing.
        httplib::Client cli("localhost", port);
        cli.set_connection_timeout(5);
        cli.set_read_timeout(10);

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
        mcp::allowOrigin("http://localhost:5173/");
        check("after allowOrigin -> 200", status(post({{"Origin", "http://localhost:5173"}}, "application/json")) == 200);

        // Host (DNS rebinding: the page's own name, resolving to 127.0.0.1)
        check("rebinding Host -> 403", status(post({{"Host", "evil.example:" + p}}, "application/json")) == 403);
        check("Host localhost:port -> 200", status(post({{"Host", "localhost:" + p}}, "application/json")) == 200);
        check("Host 127.0.0.1 -> 200", status(post({{"Host", "127.0.0.1"}}, "application/json")) == 200);
        check("Host [::1]:port -> 200", status(post({{"Host", "[::1]:" + p}}, "application/json")) == 200);
        check("Host localhost.evil.example -> 403", status(post({{"Host", "localhost.evil.example"}}, "application/json")) == 403);

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

    printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "OK", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}
