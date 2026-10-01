// =============================================================================
// tcxWebSocket tests - headless behavioral test (no window; loopback only).
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
//
// It also checks how the native client handles fragmented messages
// (RFC 6455 5.4, #338) against a fake server: a core TcpServer on a fixed
// local port that answers the upgrade request with "101 Switching Protocols"
// and then writes unmasked frames by hand:
//   - text split into 3 frames with a Ping between frames 2 and 3 -> one
//     onMessage with the full text; one masked Pong echoing the Ping
//   - binary split into 2 frames, and a 100 KB text in 3 frames using the
//     64-bit, 16-bit and 7-bit length forms -> one onMessage each, intact
//   - a first fragment only, then disconnect() and connect() -> the next
//     message arrives alone; the partial one does not leak into it
//   - a continuation with nothing in progress, or a new Text frame while a
//     message is in progress -> Close 1002, onError, then onClose
//   - a frame, or fragments together, over the 64 MiB limit -> Close 1009,
//     onError, then onClose (checked from the frame header alone)
//   - an onError listener that disconnects -> onClose fires once
// =============================================================================

#include <TrussC.h>
#include <tcWebSocketClient.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace std;
using namespace tc;
using tcx::websocket::WebSocketClient;
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


// -----------------------------------------------------------------------------
// Loopback: fake WebSocket server
// -----------------------------------------------------------------------------
// TcpServer::start() keeps the requested port, so port 0 cannot be read back.
// The fake server tries fixed ports below Linux's ephemeral range
// (32768-60999), so a port the OS handed to an outgoing connection does not
// collide with it, and moves on to the next one if a port is taken.
static const int kFirstPort = 23380;
static const int kPortAttempts = 20;
static const uint64_t kMaxMessageSize = 64ull * 1024 * 1024;  // WebSocketClient's limit

static bool waitFor(const function<bool()>& cond, int ms = 3000) {
    auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (chrono::steady_clock::now() < deadline) {
        if (cond()) return true;
        this_thread::sleep_for(chrono::milliseconds(5));
    }
    return cond();
}

// One unmasked server frame
static vector<char> frame(uint8_t firstByte, const string& payload) {
    vector<char> f;
    f.push_back((char)firstByte);
    uint64_t len = payload.size();
    if (len < 126) {
        f.push_back((char)len);
    } else if (len < 65536) {
        f.push_back((char)126);
        f.push_back((char)((len >> 8) & 0xFF));
        f.push_back((char)(len & 0xFF));
    } else {
        f.push_back((char)127);
        for (int i = 7; i >= 0; --i) f.push_back((char)((len >> (i * 8)) & 0xFF));
    }
    f.insert(f.end(), payload.begin(), payload.end());
    return f;
}

// Only a header with the 64-bit length form; no payload follows
static vector<char> header64(uint8_t firstByte, uint64_t len) {
    vector<char> f;
    f.push_back((char)firstByte);
    f.push_back((char)127);
    for (int i = 7; i >= 0; --i) f.push_back((char)((len >> (i * 8)) & 0xFF));
    return f;
}

struct ClientFrame {
    int opcode = 0;
    bool fin = false;
    bool masked = false;
    string payload;
};

struct FakeServer {
    TcpServer server;
    EventListener connectL, receiveL;
    mutex m;
    int clientId = -1;
    string inbox;           // bytes from the current client
    bool handshakeDone = false;

    bool start() {
        connectL = server.onClientConnect.listen([this](TcpClientConnectEventArgs& a) {
            lock_guard<mutex> lock(m);
            clientId = a.clientId;
            inbox.clear();
            handshakeDone = false;
        });
        receiveL = server.onReceive.listen([this](TcpServerReceiveEventArgs& a) {
            lock_guard<mutex> lock(m);
            if (a.clientId != clientId) return;
            inbox.append(a.data.begin(), a.data.end());
        });
        for (int i = 0; i < kPortAttempts; ++i) {
            if (server.start(kFirstPort + i, 4)) {
                port = kFirstPort + i;
                return true;
            }
        }
        printf("could not start the fake server on ports %d-%d\n",
               kFirstPort, kFirstPort + kPortAttempts - 1);
        return false;
    }
    int port = 0;

    // Waits for the upgrade request of a new client and answers it
    bool acceptUpgrade() {
        bool got = waitFor([this] {
            lock_guard<mutex> lock(m);
            return clientId >= 0 && !handshakeDone && inbox.find("\r\n\r\n") != string::npos;
        });
        if (!got) return false;
        int id;
        {
            lock_guard<mutex> lock(m);
            inbox.erase(0, inbox.find("\r\n\r\n") + 4);
            handshakeDone = true;
            id = clientId;
        }
        return server.send(id, string("HTTP/1.1 101 Switching Protocols\r\n"
                                      "Upgrade: websocket\r\nConnection: Upgrade\r\n\r\n"));
    }

    bool send(const vector<char>& bytes) {
        int id;
        { lock_guard<mutex> lock(m); id = clientId; }
        return server.send(id, bytes);
    }

    // Parses the frames the client sent so far (after the upgrade request)
    vector<ClientFrame> frames() {
        lock_guard<mutex> lock(m);
        vector<ClientFrame> out;
        size_t p = 0;
        const auto* b = reinterpret_cast<const unsigned char*>(inbox.data());
        while (inbox.size() - p >= 2) {
            ClientFrame f;
            f.fin = (b[p] & 0x80) != 0;
            f.opcode = b[p] & 0x0F;
            f.masked = (b[p + 1] & 0x80) != 0;
            uint64_t len = b[p + 1] & 0x7F;
            size_t h = 2;
            if (len == 126) {
                if (inbox.size() - p < 4) break;
                len = (b[p + 2] << 8) | b[p + 3];
                h = 4;
            } else if (len == 127) {
                if (inbox.size() - p < 10) break;
                len = 0;
                for (int i = 0; i < 8; ++i) len = (len << 8) | b[p + 2 + i];
                h = 10;
            }
            unsigned char mask[4] = {0, 0, 0, 0};
            if (f.masked) {
                if (inbox.size() - p < h + 4) break;
                memcpy(mask, b + p + h, 4);
                h += 4;
            }
            if (inbox.size() - p < h + len) break;
            for (uint64_t i = 0; i < len; ++i) f.payload.push_back((char)(b[p + h + i] ^ mask[i % 4]));
            out.push_back(f);
            p += h + len;
        }
        return out;
    }
};

// Records the client's events, which fire on its receive thread
struct Recorder {
    mutex m;
    vector<string> events;          // "open", "message", "error", "close"
    vector<WebSocketEventArgs> messages;
    vector<string> errors;
    EventListener openL, msgL, errL, closeL;

    void attach(WebSocketClient& c) {
        openL = c.onOpen.listen([this] { lock_guard<mutex> l(m); events.push_back("open"); });
        msgL = c.onMessage.listen([this](WebSocketEventArgs& a) {
            lock_guard<mutex> l(m);
            events.push_back("message");
            messages.push_back(a);
        });
        errL = c.onError.listen([this](TcpErrorEventArgs& a) {
            lock_guard<mutex> l(m);
            events.push_back("error");
            errors.push_back(a.message);
        });
        closeL = c.onClose.listen([this] { lock_guard<mutex> l(m); events.push_back("close"); });
    }
    size_t count(const string& e) {
        lock_guard<mutex> l(m);
        size_t n = 0;
        for (auto& x : events) n += (x == e);
        return n;
    }
    vector<string> eventsAfterOpen() {
        lock_guard<mutex> l(m);
        vector<string> out;
        bool seen = false;
        for (auto& x : events) {
            if (seen) out.push_back(x);
            if (x == "open") seen = true;
        }
        return out;
    }
    void reset() {
        lock_guard<mutex> l(m);
        events.clear();
        messages.clear();
        errors.clear();
    }
};

static bool connectClient(WebSocketClient& client, FakeServer& fake, Recorder& rec) {
    size_t opens = rec.count("open");
    if (!client.connect("ws://127.0.0.1:" + to_string(fake.port) + "/")) return false;
    if (!fake.acceptUpgrade()) return false;
    return waitFor([&] { return rec.count("open") > opens && client.isConnected(); });
}

static bool hasCloseWithStatus(FakeServer& fake, int status) {
    for (auto& f : fake.frames()) {
        if (f.opcode == 0x8 && f.masked && f.payload.size() == 2 &&
            ((((unsigned char)f.payload[0]) << 8) | (unsigned char)f.payload[1]) == status) {
            return true;
        }
    }
    return false;
}

// One failure case: the server sends the given bytes; the client must send
// Close <status>, then report onError followed by onClose.
static void checkFailure(const string& name, const vector<vector<char>>& sends, int status) {
    FakeServer fake;
    if (!fake.start()) { check(name + ": server start", false); return; }
    WebSocketClient client;
    Recorder rec;
    rec.attach(client);
    if (!connectClient(client, fake, rec)) { check(name + ": connect", false); return; }
    for (auto& b : sends) fake.send(b);
    bool closed = waitFor([&] { return rec.count("close") >= 1; });
    this_thread::sleep_for(chrono::milliseconds(50));   // let any extra event arrive
    check(name + ": onClose fired", closed);
    check(name + ": events are onError then onClose",
          rec.eventsAfterOpen() == vector<string>{"error", "close"});
    check(name + ": no onMessage", rec.count("message") == 0);
    check(name + ": server got masked Close " + to_string(status),
          waitFor([&] { return hasCloseWithStatus(fake, status); }));
    check(name + ": client is Disconnected",
          client.getState() == WebSocketClient::State::Disconnected);
    {
        lock_guard<mutex> l(rec.m);
        if (!rec.errors.empty()) printf("    onError: %s\n", rec.errors[0].c_str());
    }
    client.disconnect();
    fake.server.stop();
}

static void runLoopbackTests() {
    // 1. Text in 3 fragments with a Ping between frames 2 and 3
    {
        FakeServer fake;
        if (!fake.start()) { check("loopback: server start", false); return; }
        WebSocketClient client;
        Recorder rec;
        rec.attach(client);
        check("text 3 fragments: connected", connectClient(client, fake, rec));
        fake.send(frame(0x01, "Hello"));            // Text, FIN=0
        fake.send(frame(0x00, ", wor"));            // Continuation, FIN=0
        fake.send(frame(0x89, "p"));                // Ping
        fake.send(frame(0x80, "ld"));               // Continuation, FIN=1
        bool got = waitFor([&] { return rec.count("message") >= 1; });
        this_thread::sleep_for(chrono::milliseconds(100));   // let any extra message arrive
        check("text 3 fragments: onMessage fired", got);
        {
            lock_guard<mutex> l(rec.m);
            check("text 3 fragments: exactly one onMessage", rec.messages.size() == 1);
            if (!rec.messages.empty()) {
                auto& a = rec.messages[0];
                check("text 3 fragments: message == \"Hello, world\"", a.message == "Hello, world");
                check("text 3 fragments: data holds the 12 bytes", dataEquals(a, "Hello, world"));
                check("text 3 fragments: isBinary false", !a.isBinary);
            }
        }
        waitFor([&] { return fake.frames().size() >= 1; });
        int pongs = 0;
        bool pongOk = false;
        for (auto& f : fake.frames()) {
            if (f.opcode == 0xA) {
                ++pongs;
                pongOk = f.masked && f.fin && f.payload == "p";
            }
        }
        check("text 3 fragments: exactly one masked Pong \"p\"", pongs == 1 && pongOk);
        check("text 3 fragments: no onError / onClose, still Open",
              rec.count("error") == 0 && rec.count("close") == 0 && client.isConnected());

        // 2. Binary in 2 fragments, with zero bytes inside
        rec.reset();
        const string bin1("\x00\x01\x02", 3), bin2("\xFF\x00", 2);
        fake.send(frame(0x02, bin1));               // Binary, FIN=0
        fake.send(frame(0x80, bin2));               // Continuation, FIN=1
        waitFor([&] { return rec.count("message") >= 1; });
        this_thread::sleep_for(chrono::milliseconds(100));
        {
            lock_guard<mutex> l(rec.m);
            check("binary 2 fragments: exactly one onMessage", rec.messages.size() == 1);
            if (!rec.messages.empty()) {
                auto& a = rec.messages[0];
                check("binary 2 fragments: isBinary true", a.isBinary);
                check("binary 2 fragments: data intact (5 bytes)", dataEquals(a, bin1 + bin2));
                check("binary 2 fragments: message empty", a.message.empty());
            }
        }

        // 2b. 100 KB text in 3 fragments (64-bit, 16-bit and 7-bit lengths),
        //     sent in one write so several frames arrive in one TCP read
        rec.reset();
        string big(100000, 'x');
        for (size_t i = 0; i < big.size(); ++i) big[i] = (char)('a' + i % 26);
        vector<char> all;
        auto a1 = frame(0x01, big.substr(0, 70000));
        auto a2 = frame(0x00, big.substr(70000, 29990));
        auto a3 = frame(0x80, big.substr(99990));
        all.insert(all.end(), a1.begin(), a1.end());
        all.insert(all.end(), a2.begin(), a2.end());
        all.insert(all.end(), a3.begin(), a3.end());
        fake.send(all);
        waitFor([&] { return rec.count("message") >= 1; });
        this_thread::sleep_for(chrono::milliseconds(100));
        {
            lock_guard<mutex> l(rec.m);
            check("text 100 KB in 3 fragments: exactly one onMessage", rec.messages.size() == 1);
            check("text 100 KB in 3 fragments: message intact",
                  !rec.messages.empty() && rec.messages[0].message == big &&
                  !rec.messages[0].isBinary);
        }

        // 3. First fragment only, then disconnect() and connect() again
        rec.reset();
        fake.send(frame(0x01, "partial"));          // Text, FIN=0; never finished
        this_thread::sleep_for(chrono::milliseconds(100));
        client.disconnect();
        check("reconnect after partial: connected again", connectClient(client, fake, rec));
        fake.send(frame(0x81, "next"));             // unfragmented Text
        waitFor([&] { return rec.count("message") >= 1; });
        this_thread::sleep_for(chrono::milliseconds(100));
        {
            lock_guard<mutex> l(rec.m);
            check("reconnect after partial: exactly one onMessage", rec.messages.size() == 1);
            check("reconnect after partial: message == \"next\"",
                  !rec.messages.empty() && rec.messages[0].message == "next");
        }
        client.disconnect();
        fake.server.stop();
    }

    // 4. Protocol errors -> Close 1002, onError, onClose
    checkFailure("continuation with nothing in progress", {frame(0x80, "x")}, 1002);
    checkFailure("new Text while a message is in progress",
                 {frame(0x01, "a"), frame(0x81, "b")}, 1002);

    // 5. Size limit -> Close 1009, onError, onClose. The header alone triggers it.
    checkFailure("single frame over the limit", {header64(0x82, kMaxMessageSize + 1)}, 1009);
    checkFailure("fragments together over the limit",
                 {frame(0x02, string(10, 'z')), header64(0x80, kMaxMessageSize - 9)}, 1009);

    // 6. An onError listener that disconnects: onClose fires once
    {
        FakeServer fake;
        if (!fake.start()) { check("onError listener disconnects: server start", false); return; }
        WebSocketClient client;
        Recorder rec;
        rec.attach(client);
        EventListener dl = client.onError.listen([&client](TcpErrorEventArgs&) { client.disconnect(); });
        check("onError listener disconnects: connected", connectClient(client, fake, rec));
        fake.send(frame(0x80, "x"));
        waitFor([&] { return rec.count("close") >= 1; });
        this_thread::sleep_for(chrono::milliseconds(100));
        check("onError listener disconnects: one onError, one onClose",
              rec.count("error") == 1 && rec.count("close") == 1);
        client.disconnect();
        fake.server.stop();
    }
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

    runLoopbackTests();

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
