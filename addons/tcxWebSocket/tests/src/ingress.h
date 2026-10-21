#ifdef __EMSCRIPTEN__
#include <emscripten.h>
EM_JS(void, ingressInstallBrowser, (), {
    Module.wsIngressSockets = [];
    globalThis.WebSocket = class {
        constructor(url) { this.url = url; this.readyState = 0; this.closeCalls = 0; Module.wsIngressSockets.push(this); }
        close(code, reason) {
            if (code !== 1000 && !(code >= 3000 && code <= 4999)) throw new Error('Invalid close code');
            ++this.closeCalls; this.closeCode = code;
        }
    };
});
EM_JS(void, ingressOpenBrowser, (), {
    var s = Module.wsIngressSockets[Module.wsIngressSockets.length - 1];
    s.readyState = 1;
    s.onopen({});
});
EM_JS(int, ingressDeliverBrowser, (int kind), {
    var s = Module.wsIngressSockets[Module.wsIngressSockets.length - 1];
    var data = kind === 0 ? new ArrayBuffer(64 * 1024 * 1024 + 1)
             : kind === 1 ? 'a'.repeat(64 * 1024 * 1024 + 1)
             : kind === 2 ? new Uint8Array([0, 255, 0]).buffer
             : kind === 3 ? '\u65e5\u672c\u8a9e'
             : kind === 5 ? '\u65e5\u672c\u8a9ea'
             : kind === 6 ? new ArrayBuffer(64 * 1024 * 1024) : "";
    var original = _malloc;
    var largest = 0;
    _malloc = function(bytes) { largest = Math.max(largest, bytes); return original(bytes); };
    try {
        s.onmessage({data: data});
    } finally {
        _malloc = original;
    }
    return largest;
});
EM_JS(int, ingressBrowserCloseCount, (), {
    return Module.wsIngressSockets[Module.wsIngressSockets.length - 1].closeCalls;
});
EM_JS(int, ingressBrowserCloseCode, (), {
    return Module.wsIngressSockets[Module.wsIngressSockets.length - 1].closeCode || 0;
});
EM_JS(void, ingressReleaseBrowser, (), {
    // Emscripten retains this singleton for the process lifetime. Release it
    // after all mock sockets so LeakSanitizer can check the test's ownership.
    if (WS.socketEvent) { _free(WS.socketEvent); WS.socketEvent = null; }
    Module.wsIngressSockets = [];
});
EM_JS(void, ingressCloseBrowser, (), {
    var s = Module.wsIngressSockets[Module.wsIngressSockets.length - 1];
    s.onclose({wasClean: true, code: s.closeCode, reason: ""});
});
#endif

// Deterministic ingress checks use the real parser with a recording transport.
namespace tcx::websocket::detail {
struct WebSocketIngressTest {
    using State = WebSocketClient::ConnectionState;
#ifdef __EMSCRIPTEN__
    static void runWeb() {
        ingressInstallBrowser();
        for (int kind = 0; kind < 2; ++kind) {
            WebSocketClient ws;
            int warnings = 0, errors = 0, closes = 0, messages = 0;
            auto log = getLogger().onLog.listen([&](auto& a) { if (a.level == LogLevel::Warning) ++warnings; });
            auto error = ws.onError.listen([&](auto&) { ++errors; });
            auto close = ws.onClose.listen([&] { ++closes; });
            auto message = ws.onMessage.listen([&](auto&) { ++messages; });
            check("browser client connects", ws.connect("ws://localhost/"));
            ingressOpenBrowser();
            int allocated = ingressDeliverBrowser(kind);
            check(kind ? "oversized browser text rejected before WASM allocation" : "oversized browser binary rejected before WASM allocation", allocated == 0 && messages == 0);
            ingressDeliverBrowser(kind);
            check("browser rejection sends Close 4009 once", ingressBrowserCloseCount() == 1 && ingressBrowserCloseCode() == 4009);
            check("browser rejection warns once and reports one error", warnings == 1 && errors == 1);
#ifndef WS_BASELINE
            auto s = tc::internal::sharedLoad(ws.state_);
            check("browser rejection counters are exact", s->rejections_->upgrades == 0 && s->rejections_->messages == 1 && s->rejections_->controls == 0);
#endif
            ingressCloseBrowser();
            ws.disconnect();
            check("browser close reports once", closes == 1 && ingressBrowserCloseCount() == 1);
        }
        {
            WebSocketClient ws;
            vector<WebSocketEventArgs> delivered;
#ifndef WS_BASELINE
            ws.setMaxMessageSize(9);
#endif
            auto message = ws.onMessage.listen([&](auto& a) { delivered.push_back(a); });
            ws.connect("ws://localhost/"); ingressOpenBrowser();
            ingressDeliverBrowser(2); ingressDeliverBrowser(3); ingressDeliverBrowser(4);
            check("browser binary retains trailing zero", delivered.size() == 3 && delivered[0].isBinary && delivered[0].data == vector<char>({0, char(255), 0}));
            check("browser UTF-8 and empty text omit terminator", delivered.size() == 3 && delivered[1].message == "日本語" && delivered[1].data.size() == 9 && delivered[2].data.empty());
#ifndef WS_BASELINE
            int warnings = 0;
            auto log = getLogger().onLog.listen([&](auto& a) { if (a.level == LogLevel::Warning) ++warnings; });
            check("browser UTF-8 byte limit applies before WASM copying", ingressDeliverBrowser(5) == 0 && delivered.size() == 3);
            ws.connect("ws://localhost/"); ingressOpenBrowser();
            ingressDeliverBrowser(5);
            check("browser rejection counter survives reconnect", ws.rejections_->messages == 2 && warnings == 2 && ingressBrowserCloseCount() == 1);
#endif
        }
        {
            WebSocketClient ws;
            size_t delivered = 0;
            auto message = ws.onMessage.listen([&](auto& a) { delivered = a.data.size(); });
            ws.connect("ws://localhost/"); ingressOpenBrowser();
            ingressDeliverBrowser(6);
            check("browser message at 64 MiB is accepted", ws.isConnected() && delivered == 64 * 1024 * 1024);
        }
#ifndef WS_BASELINE
        for (size_t limit : {size_t(2147483648ULL), size_t(4294967280ULL)}) {
            WebSocketClient ws;
            vector<WebSocketEventArgs> delivered;
            auto message = ws.onMessage.listen([&](auto& a) { delivered.push_back(a); });
            check("browser accepts unsigned wasm32 message allowance", ws.setMaxMessageSize(limit));
            ws.connect("ws://localhost/"); ingressOpenBrowser();
            ingressDeliverBrowser(2); ingressDeliverBrowser(3);
            printf("browser unsigned limit=%zu delivered=%zu closes=%d\n", limit, delivered.size(), ingressBrowserCloseCount());
            check("browser unsigned allowance delivers tiny binary and UTF-8 messages",
                  ws.isConnected() && delivered.size() == 2 &&
                  delivered[0].data == vector<char>({0, char(255), 0}) &&
                  delivered[1].message == "日本語" && ingressBrowserCloseCount() == 0);
        }
#endif
        ingressReleaseBrowser();
    }
#endif

#ifndef __EMSCRIPTEN__
    struct Transport : TcpClient {
        int closes = 0, disconnects = 0, code = 0, sends = 0;
        size_t lastPayloadSize = 0;
        string lastPayload;
        bool send(const void* data, size_t size) override {
            ++sends;
            auto* p = static_cast<const unsigned char*>(data);
            lastPayloadSize = size >= 2 ? p[1] & 0x7F : 0;
            lastPayload.clear();
            for (size_t i = 0; i < lastPayloadSize && i + 6 < size; ++i)
                lastPayload.push_back(char(p[i + 6] ^ p[2 + i % 4]));
            if (size >= 8 && (p[0] & 15) == 8) {
                ++closes;
                code = ((p[6] ^ p[2]) << 8) | (p[7] ^ p[3]);
            }
            return true;
        }
        void disconnect() override {
            ++disconnects;
            TcpDisconnectEventArgs args;
            onDisconnect.notify(args);
        }
    };
    static void receive(const std::shared_ptr<State>& s, const vector<char>& bytes) {
        TcpReceiveEventArgs args;
        args.data = bytes;
        s->handleRawReceive(args);
    }
    static vector<char> header(uint8_t opcode, uint64_t bytes) {
        vector<char> result{char(opcode), 127};
        for (int i = 7; i >= 0; --i) result.push_back(char(bytes >> (i * 8)));
        return result;
    }
    static void runNative() {
        const size_t limit = 64 * 1024 * 1024;
        const string upgrade = "HTTP/1.1 101 Switching Protocols\r\n\r\n";
        auto makeState = [](WebSocketClient& ws, bool connecting = false) {
            auto s = tc::internal::sharedLoad(ws.state_);
            s->state_ = connecting ? WebSocketClient::State::Connecting : WebSocketClient::State::Open;
            s->awaitingUpgrade_ = connecting;
#ifndef WS_BASELINE
            s->rejections_ = ws.rejections_;
#endif
            s->onOpen.target = ws.openTarget_; s->onClose.target = ws.closeTarget_;
            s->onMessage.target = ws.messageTarget_; s->onError.target = ws.errorTarget_;
            s->client_ = std::make_unique<Transport>();
            s->disconnectListener_ = s->client_->onDisconnect.listen([s](auto& args) { s->handleTcpDisconnect(args); });
            return s;
        };
#ifndef WS_BASELINE
        {
            tc::getMainThreadId();
            WebSocketClient ws;
            int closes = 0;
            auto close = ws.onClose.listen([&] { ++closes; }, tc::Deliver::Main);
            std::thread worker([&] {
                auto s = makeState(ws);
                for (int i = 0; i < 1024; ++i) s->onClose.notifyExternal();
                for (int i = 0; i < 1025; ++i) {
                    s = makeState(ws);
                    ws.disconnect();
                }
            });
            worker.join();
            auto store = tc::internal::ExternalEventDelivery<void>::diagnostics(ws.onClose);
            check("local WebSocket closes bypass saturated external delivery", store && store->pending == 1024 && store->rejectedCopies == 0);
            tc::internal::drainMainThreadQueue();
            check("all local WebSocket closes reach Main", closes == 2049 && store->pending == 0);
        }
#endif
        {
            WebSocketClient ws;
            auto s = makeState(ws, true);
            auto* transport = static_cast<Transport*>(s->client_.get());
            int warnings = 0, errors = 0, closes = 0;
            auto log = getLogger().onLog.listen([&](LogEventArgs& a) { if (a.level == LogLevel::Warning) ++warnings; });
            auto error = ws.onError.listen([&](auto&) { ++errors; });
            auto close = ws.onClose.listen([&] { ++closes; });
            receive(s, vector<char>(65536, 'a'));
            check("upgrade at limit stays within allowance", s->receiveBuffer_.size() == 65536);
            receive(s, vector<char>(65536, 'b'));
            printf("upgrade buffered=%zu capacity=%zu warnings=%d errors=%d closes=%d\n", s->receiveBuffer_.size(), s->receiveBuffer_.capacity(), warnings, errors, closes);
            check("endless upgrade header is rejected before growth", s->receiveBuffer_.size() <= 65536 && ws.getState() == WebSocketClient::State::Disconnected);
            receive(s, vector<char>(65536, 'c'));
            check("upgrade rejection closes once and warns once", transport->disconnects == 1 && closes == 1 && errors == 1 && warnings == 1);
#ifndef WS_BASELINE
            check("upgrade rejection counters are exact", s->rejections_->upgrades == 1 && s->rejections_->messages == 0 && s->rejections_->controls == 0);
#endif
        }
        for (size_t split : {size_t(0), upgrade.size() - 3, upgrade.size() - 2, upgrade.size() - 1}) {
            WebSocketClient ws;
            auto s = makeState(ws, true);
            string padded = upgrade.substr(0, upgrade.size() - 4);
            padded += string(65536 - upgrade.size(), ' ');
            padded += "\r\n\r\n";
            vector<char> bytes(padded.begin(), padded.end());
            auto frame = header(0x82, 70000);
            frame.insert(frame.end(), 70000, 'q');
            bytes.insert(bytes.end(), frame.begin(), frame.end());
            int opens = 0, messages = 0;
            auto open = ws.onOpen.listen([&] { ++opens; });
            auto message = ws.onMessage.listen([&](auto& a) { if (a.data == vector<char>(70000, 'q')) ++messages; });
            size_t cut = split ? padded.size() - (upgrade.size() - split) : 0;
            receive(s, vector<char>(bytes.begin(), bytes.begin() + cut));
            receive(s, vector<char>(bytes.begin() + cut, bytes.end()));
            check("limit-sized upgrade and coalesced frame are accepted", opens == 1 && messages == 1 && ws.isConnected());
        }
        {
            WebSocketClient ws;
            auto s = makeState(ws);
            size_t delivered = 0;
            auto message = ws.onMessage.listen([&](auto& a) { delivered = a.data.size(); });
            auto bytes = header(0x82, limit);
            receive(s, bytes);
            check("message allowance is admitted from the header", ws.isConnected() && s->receiveBuffer_.size() == 10);
            printf("advertised payload=%zu header-only capacity=%zu\n", limit, s->receiveBuffer_.capacity());
            check("advertised length alone retains only header storage", s->receiveBuffer_.capacity() <= 10);
            vector<char> chunk(65536, 'd');
            for (size_t i = 0; i < limit; i += chunk.size()) receive(s, chunk);
            check("message at 64 MiB is accepted", delivered == limit && ws.isConnected());
        }
        {
            WebSocketClient ws;
            auto s = makeState(ws);
            const size_t fragments = 32768;
#ifndef WS_BASELINE
            // A non-power-of-two allowance also exercises the growth cap.
            s->maxMessageSize_ = fragments + 1;
#endif
            vector<char> baseGrowth;
            size_t reallocations = 0, baseReallocations = 0, copied = 0;
            bool bounded = true;
            for (size_t i = 0; i < fragments; ++i) {
                size_t capacity = s->fragmentBuffer_.capacity();
                size_t baseCapacity = baseGrowth.capacity();
                receive(s, vector<char>{char(i ? 0x00 : 0x02), 1, 'f'});
                baseGrowth.push_back('f');
                if (s->fragmentBuffer_.capacity() != capacity) { ++reallocations; copied += i; }
                if (baseGrowth.capacity() != baseCapacity) ++baseReallocations;
                bounded = bounded && s->fragmentBuffer_.capacity() <= fragments + 1;
            }
            printf("fragments=%zu reallocations=%zu base=%zu copied=%zu capacity=%zu\n",
                   fragments, reallocations, baseReallocations, copied, s->fragmentBuffer_.capacity());
            check("one-byte fragments grow geometrically comparable to base",
                  reallocations <= baseReallocations + 1 && copied <= 2 * fragments);
            check("fragment capacity stays within message allowance", bounded && s->fragmentBuffer_.size() == fragments);
            size_t delivered = 0;
            auto message = ws.onMessage.listen([&](auto& a) { delivered = a.data.size(); });
            receive(s, vector<char>{char(0x80), 1, 'f'});
            check("fragment growth accepts final byte at non-power-of-two allowance", delivered == fragments + 1 && ws.isConnected());
        }
        for (int kind = 0; kind < 3; ++kind) {
            WebSocketClient ws;
            auto s = makeState(ws);
            auto* transport = static_cast<Transport*>(s->client_.get());
            int warnings = 0, messages = 0, errors = 0, closes = 0;
            auto log = getLogger().onLog.listen([&](auto& a) { if (a.level == LogLevel::Warning) ++warnings; });
            auto message = ws.onMessage.listen([&](auto&) { ++messages; });
            auto error = ws.onError.listen([&](auto&) { ++errors; });
            auto close = ws.onClose.listen([&] { ++closes; });
            if (kind == 1) receive(s, vector<char>{0x02, 10, 'a','a','a','a','a','a','a','a','a','a'});
            auto bytes = header(kind == 2 ? 0x89 : kind == 1 ? 0x80 : 0x82,
                                kind == 2 ? 126 : kind == 1 ? limit - 9 : limit + 1);
            // Extra bytes in the delivery must not be retained for a rejected frame.
            bytes.insert(bytes.end(), 65536, 'x');
            receive(s, bytes);
            check(kind == 0 ? "oversized frame is rejected before payload allocation" : kind == 1 ? "fragmented sum over limit is rejected" : "oversized control frame is rejected",
                  transport->closes == 1 && transport->code == (kind == 2 ? 1002 : 1009) && s->receiveBuffer_.capacity() < 65536);
            receive(s, bytes);
            printf("frame kind=%d capacity=%zu closeCode=%d warnings=%d errors=%d closes=%d\n", kind, s->receiveBuffer_.capacity(), transport->code, warnings, errors, closes);
            check("frame rejection warns once and closes once", warnings == 1 && messages == 0 && errors == 1 && closes == 1 && transport->disconnects == 1 && transport->closes == 1);
#ifndef WS_BASELINE
            check("frame rejection counters are exact", s->rejections_->upgrades == 0 && s->rejections_->messages == (kind != 2 ? 1 : 0) && s->rejections_->controls == (kind == 2 ? 1 : 0));
#endif
        }
#ifndef WS_BASELINE
        {
            WebSocketClient ws;
            auto s = makeState(ws);
            auto* transport = static_cast<Transport*>(s->client_.get());
            int warnings = 0;
            auto log = getLogger().onLog.listen([&](auto& a) { if (a.level == LogLevel::Warning) ++warnings; });
            check("outgoing control truncation retains 125 bytes", ws.sendPing(string(126, 'p')) && transport->lastPayloadSize == 125);
            check("second outgoing control truncation retains 125 bytes", ws.sendPing(string(126, 'p')) && transport->lastPayload == string(125, 'p'));
            printf("two truncations warnings=%d count=%llu\n", warnings, (unsigned long long)s->rejections_->controls.load());
            check("two truncations warn once and count twice", warnings == 1 && s->rejections_->controls == 2);
            ws.connect("invalid"); // Replace the run without starting a network transport.
            s = makeState(ws);
            check("truncation warning gate and counter survive reconnect", ws.sendPing(string(126, 'p')) && warnings == 1 && s->rejections_->controls == 3);
        }
        for (int action = 0; action < 4; ++action) {
            struct Owner { WebSocketClient ws; string ping = string(126, 'p'); };
            auto owner = std::make_unique<Owner>();
            auto s = makeState(owner->ws);
            auto* transport = static_cast<Transport*>(s->client_.get());
            int warnings = 0;
            auto log = getLogger().onLog.listen([&](auto& a) {
                if (a.level != LogLevel::Warning) return;
                ++warnings;
                if (action == 0) owner.reset();
                else if (action == 1) owner->ws.disconnect();
                else if (action == 2) { owner->ws.connect("invalid"); makeState(owner->ws); }
                else { string replacement(256, 'x'); owner->ping.swap(replacement); }
            });
            bool sent = owner->ws.sendPing(owner->ping);
            check(action == 0 ? "truncation Warning may destroy payload and client owner" :
                  action == 1 ? "truncation Warning may disconnect client" :
                  action == 2 ? "truncation Warning may replace connection" :
                                "truncated bytes are owned before Warning changes payload",
                  warnings == 1 && s->rejections_->controls == 1 &&
                  (action == 3 ? sent && transport->lastPayload == string(125, 'p') : !sent && transport->sends == 0));
        }
        {
            WebSocketClient ws;
            int warnings = 0;
            auto log = getLogger().onLog.listen([&](auto& a) { if (a.level == LogLevel::Warning) ++warnings; });
            check("zero message limit is ignored", !ws.setMaxMessageSize(0) && ws.maxMessageSize_ == limit);
            check("short upgrade limit is ignored", !ws.setMaxHandshakeSize(3) && ws.maxHandshakeSize_ == 65536);
            check("invalid configuration warnings and counters are exact", warnings == 2 && ws.rejections_->configurations == 2);
            check("message and upgrade limits can be configured", ws.setMaxMessageSize(1024) && ws.setMaxHandshakeSize(128) && ws.maxMessageSize_ == 1024 && ws.maxHandshakeSize_ == 128);
        }
#endif
    }
#endif
};
} // namespace tcx::websocket::detail

static void runIngressTests() {
#ifndef __EMSCRIPTEN__
    tcx::websocket::detail::WebSocketIngressTest::runNative();
#else
    tcx::websocket::detail::WebSocketIngressTest::runWeb();
#endif
}
