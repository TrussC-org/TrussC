// =============================================================================
// tcxWebSocket tests - headless behavioral test (no window, no network).
//
// Built and run by the daily CI workflow (examples/build_all.py
// --addon-tests-only --include-daily; exit 0 = pass, non-zero = fail). It is
// marked daily-only because tcxWebSocket depends on tcxTls, which builds
// mbedTLS.
//
// It checks detail::fillMessageArgs, which the web build (Emscripten) uses to
// turn a received message into WebSocketEventArgs. Emscripten reports a text
// message as a NUL-terminated UTF-8 string with the terminator counted in
// numBytes; the web build must deliver it like native does:
//   - text "hello" (6 bytes incl. NUL) -> message == "hello", data 5 bytes
//   - an empty text message (just the NUL, or 0 bytes) -> empty message/data
//   - multi-byte UTF-8 "日本語" -> the 9 UTF-8 bytes, no NUL
//   - binary messages are copied as they are, a trailing 0 byte included
// The bytes are laid out here as Emscripten hands them over, so the check
// runs natively; the web handler itself is compiled by the web CI job.
// =============================================================================

#include <tcWebSocketClient.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace std;
using tcx::websocket::WebSocketEventArgs;
using tcx::websocket::detail::fillMessageArgs;

static int g_pass = 0, g_fail = 0;
static void check(const string& name, bool ok) {
    printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    fflush(stdout);  // flush each line so CI logs survive a later crash
    ok ? ++g_pass : ++g_fail;
}

// A text message as Emscripten reports it: the UTF-8 bytes plus a NUL,
// with numBytes counting the NUL.
static vector<unsigned char> emscriptenText(const string& s) {
    vector<unsigned char> bytes(s.begin(), s.end());
    bytes.push_back(0);
    return bytes;
}

static bool dataEquals(const WebSocketEventArgs& args, const string& s) {
    return args.data.size() == s.size() &&
           (s.empty() || memcmp(args.data.data(), s.data(), s.size()) == 0);
}

int main() {
    {
        auto bytes = emscriptenText("hello");
        WebSocketEventArgs args;
        fillMessageArgs(args, bytes.data(), bytes.size(), true);
        check("text \"hello\": message == \"hello\"", args.message == "hello");
        check("text \"hello\": message size 5", args.message.size() == 5);
        check("text \"hello\": data holds 5 bytes", dataEquals(args, "hello"));
        check("text \"hello\": isBinary false", !args.isBinary);
    }

    {
        // Empty text message: Emscripten reports just the terminator.
        auto bytes = emscriptenText("");
        WebSocketEventArgs args;
        fillMessageArgs(args, bytes.data(), bytes.size(), true);
        check("empty text (NUL only): message empty", args.message.empty());
        check("empty text (NUL only): data empty", args.data.empty());
        check("empty text (NUL only): isBinary false", !args.isBinary);
    }

    {
        // Empty text message reported with numBytes 0.
        unsigned char none = 0;
        WebSocketEventArgs args;
        fillMessageArgs(args, &none, 0, true);
        check("empty text (0 bytes): message and data empty",
              args.message.empty() && args.data.empty() && !args.isBinary);
    }

    {
        const string jp = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";  // "日本語" in UTF-8
        auto bytes = emscriptenText(jp);
        WebSocketEventArgs args;
        fillMessageArgs(args, bytes.data(), bytes.size(), true);
        check("text UTF-8 (9 bytes): message equals the UTF-8 bytes", args.message == jp);
        check("text UTF-8 (9 bytes): data holds 9 bytes", dataEquals(args, jp));
        check("text UTF-8 (9 bytes): no NUL in message",
              args.message.find('\0') == string::npos);
    }

    {
        // A text message without a terminator is kept whole.
        const string s = "abc";
        WebSocketEventArgs args;
        fillMessageArgs(args, reinterpret_cast<const unsigned char*>(s.data()), s.size(), true);
        check("text without trailing NUL: kept whole",
              args.message == "abc" && dataEquals(args, "abc"));
    }

    {
        // Binary data is unchanged, a trailing 0 byte included.
        const unsigned char bin[] = {0x01, 0x00, 0xFF, 0x00};
        WebSocketEventArgs args;
        fillMessageArgs(args, bin, sizeof(bin), false);
        check("binary: isBinary true", args.isBinary);
        check("binary: data holds all 4 bytes incl. trailing 0",
              args.data.size() == 4 && memcmp(args.data.data(), bin, 4) == 0);
        check("binary: message empty", args.message.empty());
    }

    {
        WebSocketEventArgs args;
        fillMessageArgs(args, nullptr, 0, false);
        check("empty binary: data and message empty",
              args.isBinary && args.data.empty() && args.message.empty());
    }

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
