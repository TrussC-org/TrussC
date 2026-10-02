// =============================================================================
// tcxCurl tests - headless console test (no window), no network needed.
//
// Built and run by CI on every push/PR across macOS / Windows / Linux via
// examples/build_all.py --addon-tests-only (exit 0 = pass, non-zero = fail).
//
// tcxCurl is listed in addons.make to compile the libcurl glue and check
// default SSL option bits. Also tests the code behind setVerbose():
//   - redactCredentialLine(): credential headers (HTTP/1 lines and the
//     [HTTP/2] / h2h3 / h2 info lines), curl's echo of an environment proxy
//     (lines curl cut at 2047 chars, at 2043 + "...", and error lines at
//     255 chars), and the user name on "Proxy/Server auth using" lines
//   - formatVerbose() / flushPendingHeaderOut(): recorded debug callback
//     sequences replayed, including a request header block split inside a
//     credential line, "Connection died, retrying", "Issue another request
//     to this URL" and a tail that never completes
//
// The fake secrets below must never appear in the output. The expected
// strings follow curl's line formats; a curl upgrade that changes a format
// has to update both tcxCurl.h and these cases.
// =============================================================================

#include "../../src/tcxCurl.h"

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace tcx::curl::detail;
using std::string;

static int g_pass = 0, g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-64s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    ok ? ++g_pass : ++g_fail;
}

// Prints both strings on a mismatch so a failure shows what changed.
static void checkEq(const string& name, const string& got, const string& want) {
    check(name, got == want);
    if (got != want) {
        std::printf("    got:  [%s]\n    want: [%s]\n", got.c_str(), want.c_str());
    }
}

static bool contains(const string& s, const string& part) {
    return s.find(part) != string::npos;
}

// ---------------------------------------------------------------------------
// Replaying debug callbacks
// ---------------------------------------------------------------------------

struct Step {
    VerboseKind kind;
    string data;
};
static Step text(string s) { return {VerboseKind::Text, std::move(s)}; }
static Step out(string s) { return {VerboseKind::HeaderOut, std::move(s)}; }
static Step in(string s) { return {VerboseKind::HeaderIn, std::move(s)}; }

// Feeds the steps through formatVerbose() like curl's debug callback would,
// then the end-of-transfer flush HttpClient::request() does.
static string replay(const std::vector<Step>& steps, bool endFlush = true) {
    VerboseState state;
    string result;
    for (const auto& s : steps) formatVerbose(&state, s.kind, s.data, result);
    if (endFlush) flushPendingHeaderOut(state, result);
    return result;
}

// Same, with every HeaderOut step cut into pieces of `piece` bytes.
static string replayPieces(const std::vector<Step>& steps, size_t piece) {
    std::vector<Step> split;
    for (const auto& s : steps) {
        if (s.kind != VerboseKind::HeaderOut) {
            split.push_back(s);
            continue;
        }
        for (size_t i = 0; i < s.data.size(); i += piece) {
            split.push_back(out(s.data.substr(i, piece)));
        }
    }
    return replay(split);
}

int main() {
    // TLS defaults: certificate verification remains enabled; only unavailable
    // revocation information becomes best-effort on Windows.
#ifdef _WIN32
    check("SSL defaults: native CA and best-effort revocation",
          defaultSslOptions() == static_cast<long>(CURLSSLOPT_NATIVE_CA | CURLSSLOPT_REVOKE_BEST_EFFORT));
    check("SSL defaults: revocation is not disabled",
          (defaultSslOptions() & CURLSSLOPT_NO_REVOKE) == 0);
#else
    check("SSL defaults: other platforms keep curl defaults", defaultSslOptions() == 0L);
#endif
    const string SECRET = "TOPSECRET42";

    // --- HTTP/1 request header lines ----------------------------------------
    {
        const char* names[] = {
            "Authorization", "AUTHORIZATION", "authorization", "AuThOrIzAtIoN",
            "Proxy-Authorization", "PROXY-AUTHORIZATION", "proxy-Authorization",
            "X-Api-Key", "X-API-KEY", "x-api-key",
            "Api-Key", "API-KEY", "api-key",
        };
        for (const char* n : names) {
            string line = string(n) + ": Bearer " + SECRET + "\r\n";
            checkEq(string("header masked: ") + n, redactCredentialLine(line),
                    string(n) + ": <redacted>\r\n");
        }
        checkEq("header without a value masked", redactCredentialLine("Authorization:" + SECRET + "\n"),
                "Authorization: <redacted>\n");
        checkEq("other header kept", redactCredentialLine("X-Request-Id: abc123\r\n"),
                "X-Request-Id: abc123\r\n");
        checkEq("header name only as a prefix kept", redactCredentialLine("Authorization-Hint: abc\r\n"),
                "Authorization-Hint: abc\r\n");

        string block = "GET /v1 HTTP/1.1\r\nHost: example.com\r\nauthorization: Bearer " + SECRET +
                       "\r\nX-API-Key: " + SECRET + "\r\nAccept: */*\r\n\r\n";
        string got = redactCredentialHeaders(block);
        checkEq("header block: each credential line masked", got,
                "GET /v1 HTTP/1.1\r\nHost: example.com\r\nauthorization: <redacted>\r\n"
                "X-API-Key: <redacted>\r\nAccept: */*\r\n\r\n");
    }

    // --- HTTP/2 and HTTP/3 info lines ---------------------------------------
    {
        checkEq("[HTTP/2] value with ']' masked to the last ']'",
                redactCredentialLine("[HTTP/2] [1] [authorization: a]b]\n"),
                "[HTTP/2] [1] [authorization: <redacted>]\n");
        checkEq("[HTTP/2] bearer token masked",
                redactCredentialLine("[HTTP/2] [3] [Authorization: Bearer " + SECRET + "]\n"),
                "[HTTP/2] [3] [Authorization: <redacted>]\n");
        checkEq("[HTTP/3] x-api-key masked",
                redactCredentialLine("[HTTP/3] [0] [x-api-key: " + SECRET + "]\n"),
                "[HTTP/3] [0] [x-api-key: <redacted>]\n");
        checkEq("h2h3 form masked",
                redactCredentialLine("h2h3 [authorization: Bearer " + SECRET + "]\n"),
                "h2h3 [authorization: <redacted>]\n");
        checkEq("h2 form masked",
                redactCredentialLine("h2 [proxy-authorization: Basic " + SECRET + "]\n"),
                "h2 [proxy-authorization: <redacted>]\n");
        checkEq("[HTTP/2] other header kept",
                redactCredentialLine("[HTTP/2] [1] [user-agent: x/1.0]\n"),
                "[HTTP/2] [1] [user-agent: x/1.0]\n");
    }

    // --- Proxy echo lines ----------------------------------------------------
    {
        const string uses = "Uses proxy env variable https_proxy == '";

        checkEq("proxy echo with userinfo masked",
                redactCredentialLine(uses + "http://user:" + SECRET + "@proxy:3128'\n"),
                uses + "<redacted>'\n");
        checkEq("proxy echo, upper-case variable, masked",
                redactCredentialLine("Uses proxy env variable HTTPS_PROXY == 'http://u:" + SECRET + "@p'\n"),
                "Uses proxy env variable HTTPS_PROXY == '<redacted>'\n");
        checkEq("proxy echo without '@' kept readable",
                redactCredentialLine(uses + "http://proxy.local:3128'\n"),
                uses + "http://proxy.local:3128'\n");
        checkEq("proxy echo with a quote in the password masked",
                redactCredentialLine(uses + "http://u:ab'" + SECRET + "@p:1'\n"),
                uses + "<redacted>'\n");
        checkEq("no_proxy line kept",
                redactCredentialLine("Uses proxy env variable no_proxy == 'localhost,127.0.0.1'\n"),
                "Uses proxy env variable no_proxy == 'localhost,127.0.0.1'\n");
        checkEq("NO_PROXY line kept",
                redactCredentialLine("Uses proxy env variable NO_PROXY == 'a@b,example.com'\n"),
                "Uses proxy env variable NO_PROXY == 'a@b,example.com'\n");
        check("no_proxy / NO_PROXY are not proxy echo lines",
              !isProxyEchoLine("Uses proxy env variable no_proxy == 'x'") &&
              !isProxyEchoLine("Uses proxy env variable NO_PROXY == 'x'"));

        // curl <= 8.12 (8.5 on Ubuntu 24.04) cuts the line at 2047 chars with
        // no "...". The password has a quote exactly at the cut, and the '@'
        // comes after it, so the kept part looks like a complete value.
        string head = uses + "http://user:";
        string cut2047 = head + string(2046 - head.size(), 'S') + "'";
        check("2047-char line built as intended", cut2047.size() == 2047 && cut2047.back() == '\'');
        string got = redactCredentialLine(cut2047 + "\n");
        checkEq("cut at 2047 with a quote at the cut: value masked", got, uses + "<redacted>\n");
        check("cut at 2047: no password characters left", !contains(got, "SSSS"));

        // Same without the trailing newline (eol == size)
        got = redactCredentialLine(cut2047);
        checkEq("cut at 2047, no newline: value masked", got, uses + "<redacted>");

        // One char shorter, ending in the closing quote, no '@': a complete
        // value without userinfo, kept readable.
        string full2046 = head + string(2045 - head.size(), 'h') + "'";
        checkEq("complete 2046-char value without '@' kept",
                redactCredentialLine(full2046 + "\n"), full2046 + "\n");

        // curl 8.13+: 2043 chars + "..."
        string cut2043 = head + string(2043 - head.size(), 'S') + "...";
        check("2043 + \"...\" line built as intended", cut2043.size() == 2046);
        got = redactCredentialLine(cut2043 + "\n");
        checkEq("cut at 2043 + \"...\": value masked", got, uses + "<redacted>\n");
        check("cut at 2043: no password characters left", !contains(got, "SSSS"));

        // Error lines are cut at 255 chars. A quote inside the password makes
        // the kept part look closed; the '@' is past the cut.
        const string uns = "Unsupported proxy syntax in '";
        checkEq("unsupported proxy with userinfo masked",
                redactCredentialLine(uns + "http://user:" + SECRET + "@host': bad port\n"),
                uns + "<redacted>': bad port\n");
        checkEq("unsupported proxy without '@' kept",
                redactCredentialLine(uns + "http://host:99999': bad port\n"),
                uns + "http://host:99999': bad port\n");
        string uhead = uns + "http://user:SS'";
        string cut255 = uhead + string(255 - uhead.size(), 'S');
        check("255-char error line built as intended", cut255.size() == 255);
        got = redactCredentialLine(cut255 + "\n");
        checkEq("error line cut at 255 with a quote inside: value masked", got, uns + "<redacted>\n");
        check("cut at 255: no password characters left", !contains(got, "SS"));
        string uopen = uns + "http://user:" + string(200, 'S');
        got = redactCredentialLine(uopen + "\n");
        checkEq("error line without a closing quote: value masked", got, uns + "<redacted>\n");
    }

    // --- Auth user lines -----------------------------------------------------
    {
        checkEq("proxy auth user masked",
                redactCredentialLine("Proxy auth using Basic with user '" + SECRET + "'\n"),
                "Proxy auth using Basic with user '<redacted>'\n");
        checkEq("server auth user masked",
                redactCredentialLine("Server auth using Digest with user 'a'" + SECRET + "'\n"),
                "Server auth using Digest with user '<redacted>'\n");
        checkEq("auth user line without a quote masked after the scheme",
                redactCredentialLine("Server auth using Basic " + SECRET + "\n"),
                "Server auth using <redacted>\n");
    }

    // --- Replayed callback sequences ----------------------------------------
    {
        const string req = "GET /v1 HTTP/1.1\r\nHost: example.com\r\nAuthorization: Bearer " + SECRET +
                           "\r\nAccept: */*\r\n\r\n";
        const string reqOut = "> GET /v1 HTTP/1.1\r\n> Host: example.com\r\n> Authorization: <redacted>\r\n"
                              "> Accept: */*\r\n> \r\n";

        checkEq("replay: whole header block", replay({out(req)}), reqOut);

        // Split inside the credential value
        size_t cutAt = req.find(SECRET) + 4;
        string got = replay({out(req.substr(0, cutAt)), out(req.substr(cutAt))});
        checkEq("replay: block split inside the credential", got, reqOut);

        bool allSame = true;
        for (size_t piece = 1; piece <= 9; ++piece) {
            string g = replayPieces({out(req)}, piece);
            if (g != reqOut) {
                allSame = false;
                std::printf("    piece %zu: [%s]\n", piece, g.c_str());
            }
        }
        check("replay: block split into 1..9-byte pieces", allSame);

        // Prefixes per kind, and text/header-in passed through
        checkEq("replay: text, header in and header out prefixes",
                replay({text("Connected to example.com (127.0.0.1) port 80\n"), out(req),
                        in("HTTP/1.1 200 OK\r\n"), in("Content-Length: 2\r\n"), in("\r\n")}),
                "* Connected to example.com (127.0.0.1) port 80\n" + reqOut +
                "< HTTP/1.1 200 OK\r\n< Content-Length: 2\r\n< \r\n");

        // Response headers after the request (curl 8.7+ may send the end of
        // the request headers after the response has started)
        size_t mid = req.find("Accept");
        checkEq("replay: request tail after response headers",
                replay({out(req.substr(0, mid)), in("HTTP/1.1 100 Continue\r\n"), out(req.substr(mid))}),
                "> GET /v1 HTTP/1.1\r\n> Host: example.com\r\n> Authorization: <redacted>\r\n"
                "< HTTP/1.1 100 Continue\r\n> Accept: */*\r\n> \r\n");

        // A tail that never completes: flushed, redacted, and noted
        string partial = "GET /v1 HTTP/1.1\r\nAuthorization: Bearer " + SECRET.substr(0, 5);
        got = replay({out(partial)});
        checkEq("replay: tail never completes", got,
                "> GET /v1 HTTP/1.1\r\n> Authorization: <redacted>\n"
                "* (request header output ended mid-line)\n");
        check("replay: tail never completes, no secret part", !contains(got, SECRET.substr(0, 5)));

        // Without the end-of-transfer flush, the tail is held back
        checkEq("replay: tail held until the flush", replay({out(partial)}, false),
                "> GET /v1 HTTP/1.1\r\n");

        // Connection died: the pending tail is flushed before the info line,
        // and the rest of that line, if curl still sends it, stays hidden.
        const string rest = SECRET.substr(5) + "\r\nAccept: */*\r\n\r\n";
        got = replay({out(partial), text("Connection died, retrying a fresh connect (retry count: 1)\n"),
                      out(rest)});
        checkEq("replay: connection died, rest of the cut line hidden", got,
                "> GET /v1 HTTP/1.1\r\n> Authorization: <redacted>\n"
                "* (request header output ended mid-line)\n"
                "* Connection died, retrying a fresh connect (retry count: 1)\n"
                "> <redacted>\n> Accept: */*\r\n> \r\n");
        check("replay: connection died, no secret part", !contains(got, SECRET.substr(0, 5)) &&
                                                          !contains(got, SECRET.substr(5)));

        // The rest arriving in pieces after "Connection died"
        got = replay({out(partial), text("Connection died, retrying a fresh connect (retry count: 1)\n"),
                      out(SECRET.substr(5, 2)), out(SECRET.substr(7)), out("\r\n\r\n")});
        checkEq("replay: connection died, rest in pieces hidden", got,
                "> GET /v1 HTTP/1.1\r\n> Authorization: <redacted>\n"
                "* (request header output ended mid-line)\n"
                "* Connection died, retrying a fresh connect (retry count: 1)\n"
                "> <redacted>\n> \r\n");

        // Issue another request: the old tail is flushed, and the new
        // request's first line is shown (not taken as the rest of the cut line).
        got = replay({out(partial), text("Connection died, retrying a fresh connect (retry count: 1)\n"),
                      text("Issue another request to this URL: 'http://example.com/v1'\n"), out(req)});
        checkEq("replay: connection died, then another request", got,
                "> GET /v1 HTTP/1.1\r\n> Authorization: <redacted>\n"
                "* (request header output ended mid-line)\n"
                "* Connection died, retrying a fresh connect (retry count: 1)\n"
                "* Issue another request to this URL: 'http://example.com/v1'\n" + reqOut);

        got = replay({out(partial), text("Issue another request to this URL: 'http://example.com/v1'\n"),
                      out(req)});
        checkEq("replay: another request without connection died", got,
                "> GET /v1 HTTP/1.1\r\n> Authorization: <redacted>\n"
                "* (request header output ended mid-line)\n"
                "* Issue another request to this URL: 'http://example.com/v1'\n" + reqOut);

        // Info lines through the callback
        checkEq("replay: [HTTP/2] info line",
                replay({text("[HTTP/2] [1] [authorization: Bearer " + SECRET + "]\n")}),
                "* [HTTP/2] [1] [authorization: <redacted>]\n");
        checkEq("replay: proxy echo with a newline in the value",
                replay({text("Uses proxy env variable https_proxy == 'http://u:a\n" + SECRET + "@p:1'\n")}),
                "* Uses proxy env variable https_proxy == '<redacted>'\n");
        checkEq("replay: auth user line with a newline in the name",
                replay({text("Proxy auth using Basic with user 'a\n" + SECRET + "'\n")}),
                "* Proxy auth using Basic with user '<redacted>'\n");
        checkEq("replay: text without a trailing newline gets one",
                replay({text("Closing connection")}), "* Closing connection\n");
    }

    std::printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
