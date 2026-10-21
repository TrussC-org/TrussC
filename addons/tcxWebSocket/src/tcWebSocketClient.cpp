#include "tcWebSocketClient.h"
#include <TrussC.h>
#include <algorithm>
#include <random>
#include <cstring>
#include <cstdio>
#include <limits>
#include <array>

using namespace std;
using namespace tc;

namespace tcx::websocket {

#ifndef __EMSCRIPTEN__
namespace {
// Grow only for bytes being appended, amortizing small deliveries/fragments.
// Admission guarantees required <= allowance; avoid doubling past that cap.
void reserveIngress(std::vector<char>& buffer, size_t required, size_t allowance) {
    if (required <= buffer.capacity()) return;
    size_t capacity = buffer.capacity();
    buffer.reserve(std::max(required, capacity + std::min(capacity, allowance - capacity)));
}
}
#endif

#ifdef __EMSCRIPTEN__
// The SDK's message callback allocates in WASM before calling C++. Wrap it
// at the browser event boundary so rejected messages never reach that copy.
using WebSocketLimitCallback = void (*)(void*);
EM_JS_DEPS(webSocketIngressDeps, "$WS,$lengthBytesUTF8,$getWasmTableEntry");
EM_JS(void, installWebSocketIngressLimit,
      (int socketId, size_t limit, void* userData, WebSocketLimitCallback reject), {
    // wasm32 size_t arrives as a signed i32; preserve its unsigned allowance.
    limit = limit >>> 0;
    var socket = WS.getSocket(socketId);
    var deliver = socket.onmessage;
    var rejected = false;
    socket.onmessage = function(event) {
        if (rejected) return;
        var bytes = typeof event.data === 'string'
            ? lengthBytesUTF8(event.data) : event.data.byteLength;
        // Bound (security): delivered messages need a finite WASM memory allowance. Overflow rejects the message with Close 4009, warns once and is counted.
        if (bytes > limit) {
            rejected = true;
            getWasmTableEntry(reject)(userData);
            return;
        }
        deliver(event);
    };
});
#endif

// =============================================================================
// SHA-1 Implementation (Minimal for WebSocket handshake)
// =============================================================================
namespace sha1 {
    static uint32_t rol(uint32_t value, uint32_t bits) {
        return (value << bits) | (value >> (32 - bits));
    }

    static void transform(uint32_t state[5], const unsigned char buffer[64]) {
        uint32_t block[80];
        for (int i = 0; i < 16; ++i) {
            block[i] = (buffer[i * 4] << 24) | (buffer[i * 4 + 1] << 16) | (buffer[i * 4 + 2] << 8) | (buffer[i * 4 + 3]);
        }
        for (int i = 16; i < 80; ++i) {
            block[i] = rol(block[i - 3] ^ block[i - 8] ^ block[i - 14] ^ block[i - 16], 1);
        }

        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], e = state[4];

        for (int i = 0; i < 80; ++i) {
            uint32_t f, k;
            if (i < 20) {
                f = (b & c) | (~b & d); k = 0x5A827999;
            } else if (i < 40) {
                f = b ^ c ^ d; k = 0x6ED9EBA1;
            } else if (i < 60) {
                f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC;
            } else {
                f = b ^ c ^ d; k = 0xCA62C1D6;
            }
            uint32_t temp = rol(a, 5) + f + e + k + block[i];
            e = d; d = c; c = rol(b, 30); b = a; a = temp;
        }

        state[0] += a; state[1] += b; state[2] += c; state[3] += d; state[4] += e;
    }

    std::vector<unsigned char> calculate(const std::string& input) {
        uint32_t state[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
        std::vector<unsigned char> buf(input.begin(), input.end());
        uint64_t bitLen = buf.size() * 8;
        buf.push_back(0x80);
        while ((buf.size() + 8) % 64 != 0) buf.push_back(0x00);
        for (int i = 7; i >= 0; --i) buf.push_back((bitLen >> (i * 8)) & 0xFF);

        for (size_t i = 0; i < buf.size(); i += 64) {
            transform(state, &buf[i]);
        }

        std::vector<unsigned char> digest(20);
        for (int i = 0; i < 5; ++i) {
            digest[i * 4] = (state[i] >> 24) & 0xFF;
            digest[i * 4 + 1] = (state[i] >> 16) & 0xFF;
            digest[i * 4 + 2] = (state[i] >> 8) & 0xFF;
            digest[i * 4 + 3] = state[i] & 0xFF;
        }
        return digest;
    }
}

// =============================================================================
// WebSocketClient Implementation
// =============================================================================

WebSocketClient::WebSocketClient() {
    openTarget_ = tc::internal::EventSnapshotDelivery<void>::bind(onOpen, delivery_);
    closeTarget_ = tc::internal::EventSnapshotDelivery<void>::bind(onClose, delivery_);
    messageTarget_ = tc::internal::EventSnapshotDelivery<WebSocketEventArgs>::bind(onMessage, delivery_);
    errorTarget_ = tc::internal::EventSnapshotDelivery<TcpErrorEventArgs>::bind(onError, delivery_);
}

WebSocketClient::~WebSocketClient() {
    *alive_ = false;
    delivery_->close();
    onOpen.clear(); onClose.clear(); onMessage.clear(); onError.clear();
    auto state = tc::internal::sharedLoad(state_);
    *state->alive_ = false;
    state->disconnect();
    state->receiveListener_.disconnect(); state->connectListener_.disconnect();
    state->disconnectListener_.disconnect(); state->timeoutListener_.disconnect();
}

bool WebSocketClient::connect(const std::string& url) {
    auto alive = alive_;
    disconnect();
    if (!*alive) return false;
    auto previous = tc::internal::sharedLoad(state_);
    // A reconnect in onClose may have opened another run; stop it before replacement.
    if (previous->state_ != State::Disconnected) {
        previous->disconnectListener_.disconnect();
        previous->disconnect();
    }
    *previous->alive_ = false;
    previous->receiveListener_.disconnect(); previous->connectListener_.disconnect();
    previous->disconnectListener_.disconnect(); previous->timeoutListener_.disconnect();
    auto state = std::make_shared<ConnectionState>();
    state->tlsVerifyNone_ = tlsVerifyNone_; state->tlsCaPem_ = tlsCaPem_;
    state->connectTimeout_ = connectTimeout_; state->handshakeTimeout_ = handshakeTimeout_.load();
    state->maxMessageSize_ = maxMessageSize_; state->maxHandshakeSize_ = maxHandshakeSize_;
    state->rejections_ = rejections_;
    state->onOpen.target = openTarget_; state->onClose.target = closeTarget_;
    state->onMessage.target = messageTarget_; state->onError.target = errorTarget_;
    tc::internal::sharedStore(state_, state);
    return state->connect(url);
}
void WebSocketClient::disconnect() { auto state = tc::internal::sharedLoad(state_); state->disconnect(); }
bool WebSocketClient::send(const std::string& message) { auto state = tc::internal::sharedLoad(state_); return state->send(message); }
bool WebSocketClient::send(const std::vector<char>& data) { auto state = tc::internal::sharedLoad(state_); return state->send(data); }
bool WebSocketClient::sendPing(const std::string& payload) { auto state = tc::internal::sharedLoad(state_); return state->sendPing(payload); }

bool WebSocketClient::setMaxMessageSize(size_t bytes) {
    // Bound (platform): frame lengths must leave room for their header in size_t. Overflow rejects the configuration, warns once and is counted.
    if (bytes == 0 || bytes > std::numeric_limits<size_t>::max() - 14) {
        ++rejections_->configurations;
        logWarning("WebSocket") << "Invalid message limit; configuration unchanged";
        return false;
    }
    maxMessageSize_ = bytes;
    return true;
}

bool WebSocketClient::setMaxHandshakeSize(size_t bytes) {
    // Bound (platform): an upgrade header includes its four-byte terminator. Overflow rejects the configuration, warns once and is counted.
    if (bytes < 4) {
        ++rejections_->configurations;
        logWarning("WebSocket") << "Invalid upgrade header limit; configuration unchanged";
        return false;
    }
    maxHandshakeSize_ = bytes;
    return true;
}

bool WebSocketClient::ConnectionState::connect(const std::string& url) {
    disconnect();

#ifdef __EMSCRIPTEN__
    EmscriptenWebSocketCreateAttributes attr;
    emscripten_websocket_init_create_attributes(&attr);
    attr.url = url.c_str();
    
    wsHandle_ = emscripten_websocket_new(&attr);
    if (wsHandle_ <= 0) {
        logError() << "Failed to create WebSocket";
        return false;
    }

    emscripten_websocket_set_onopen_callback(wsHandle_, this, onEmscriptenOpen);
    emscripten_websocket_set_onmessage_callback(wsHandle_, this, onEmscriptenMessage);
    installWebSocketIngressLimit(wsHandle_, maxMessageSize_, this, onEmscriptenMessageLimit);
    emscripten_websocket_set_onclose_callback(wsHandle_, this, onEmscriptenClose);
    emscripten_websocket_set_onerror_callback(wsHandle_, this, onEmscriptenError);

    state_ = State::Connecting;
    return true;
#else
    // Parse URL: ws://host:port/path
    std::string protocol = "ws";
    size_t protocolEnd = url.find("://");
    if (protocolEnd == std::string::npos) return false;
    protocol = url.substr(0, protocolEnd);
    useTls_ = (protocol == "wss");

    std::string remaining = url.substr(protocolEnd + 3);
    size_t pathStart = remaining.find('/');
    if (pathStart == std::string::npos) {
        host_ = remaining;
        path_ = "/";
    } else {
        host_ = remaining.substr(0, pathStart);
        path_ = remaining.substr(pathStart);
    }

    size_t portStart = host_.find(':');
    if (portStart == std::string::npos) {
        port_ = useTls_ ? 443 : 80;
    } else {
        port_ = std::stoi(host_.substr(portStart + 1));
        host_ = host_.substr(0, portStart);
    }

    setupClient(useTls_);
    state_ = State::Connecting;

    // The 101 deadline is checked on the main thread
    if (!timeoutListener_.isConnected()) {
        timeoutListener_ = events().update.listen([state = shared_from_this()] { state->checkHandshakeTimeout(); }, EventPriority::BeforeApp);
    }

    client_->connectAsync(host_, port_);
    return true;
#endif
}

void WebSocketClient::ConnectionState::disconnect() {
#ifdef __EMSCRIPTEN__
    if (wsHandle_ > 0) {
        if (state_ != State::Closing && state_ != State::Disconnected)
            emscripten_websocket_close(wsHandle_, 1000, "Normal closure");
        emscripten_websocket_delete(wsHandle_);
        wsHandle_ = 0;
    }
#else
    awaitingUpgrade_ = false;
    if (client_) {
        // An onClose listener can replace the handle's current run. This
        // State is retained by the caller until the notification returns.
        std::shared_ptr<std::atomic<bool>> alive = alive_;
        unsigned connection = connection_;
        client_->disconnect();
        if (!*alive || connection_ != connection) return;
    }
    receiveListener_.disconnect();
    connectListener_.disconnect();
    disconnectListener_.disconnect();
    timeoutListener_.disconnect();
#endif
    state_ = State::Disconnected;
    receiveBuffer_.clear();
    frameBytesNeeded_ = 2;
    // A partial fragmented message must not leak into the next connection
    fragmentBuffer_.clear();
    fragmentOpcode_ = 0;
    ++connection_;
}

void WebSocketClient::ConnectionState::setupClient(bool useTls) {
#ifndef __EMSCRIPTEN__
    // connect() installs the transport before publishing Connecting.
    if (state_ != State::Disconnected) return;
    if (useTls) {
        auto tls = std::make_unique<TlsClient>();
        // Cert verification is REQUIRED by default; user must opt in to skip it
        // via setTlsVerifyNone(). Silently disabling verification made wss://
        // trivially MITM-able.
        if (tlsVerifyNone_) {
            tls->setVerifyNone();
        } else if (!tlsCaPem_.empty()) {
            tls->setCACertificate(tlsCaPem_);
        }
        tls->setHandshakeTimeout(handshakeTimeout_);
        client_ = std::move(tls);
    } else {
        client_ = std::make_unique<TcpClient>();
    }

    client_->setConnectTimeout(connectTimeout_);

    // Connect event
    connectListener_ = client_->onConnect.listen([state = shared_from_this()](auto& args) { state->handleTcpConnect(args); });
    // Receive event
    receiveListener_ = client_->onReceive.listen([state = shared_from_this()](auto& args) { state->handleRawReceive(args); });
    // Disconnect event
    disconnectListener_ = client_->onDisconnect.listen([state = shared_from_this()](auto& args) { state->handleTcpDisconnect(args); });
#endif
}

void WebSocketClient::ConnectionState::handleTcpConnect(TcpConnectEventArgs& args) {
#ifndef __EMSCRIPTEN__
    if (args.success) {
        // The 101 deadline counts from the TCP connect (#262). For ws:// that
        // is now. For wss:// it is when TlsClient's TCP connection came up,
        // so the TLS handshake and the 101 share one deadline instead of
        // getting one each. This runs on the thread that ran the handshake,
        // which is where getTcpConnectTime() may be read.
        auto start = std::chrono::steady_clock::now();
        if (auto* tls = dynamic_cast<TlsClient*>(client_.get())) start = tls->getTcpConnectTime();
        upgradeStartNs_ = std::chrono::duration_cast<std::chrono::nanoseconds>(
            start.time_since_epoch()).count();
        awaitingUpgrade_ = true;
        sendHandshake();
    } else {
        state_ = State::Disconnected;
        // TlsClient's handshake deadline ran out: like the 101 timeout,
        // onError and then onClose. Any other failed connect reports onError.
        const bool timedOut = (args.message == "TLS handshake timeout");
        std::shared_ptr<std::atomic<bool>> alive = alive_;
        unsigned connection = connection_;
        TcpErrorEventArgs err;
        err.message = "TCP Connection failed: " + args.message;
        onError.notify(err);
        // An onError listener may have reconnected or destroyed the client
        if (!timedOut || !*alive || connection_ != connection) return;
        onClose.notify();
    }
#endif
}

void WebSocketClient::ConnectionState::handleTcpDisconnect(TcpDisconnectEventArgs& args) {
#ifndef __EMSCRIPTEN__
    // The server closed before the 101: the wait is over. Otherwise the
    // main thread's deadline check would still fire onError and
    // disconnect() after this onClose.
    awaitingUpgrade_ = false;
    state_ = State::Disconnected;
    onClose.notify();
#endif
}

void WebSocketClient::ConnectionState::checkHandshakeTimeout() {
#ifndef __EMSCRIPTEN__
    // Open: the deadline no longer applies. Stop polling here, on the main
    // thread that owns timeoutListener_, rather than from the receive thread
    // that saw the 101; this also releases the listener's hold on the State.
    if (state_ == State::Open) { timeoutListener_.disconnect(); return; }
    if (!awaitingUpgrade_) return;
    const float timeout = handshakeTimeout_;
    if (timeout <= 0.0f) return;
    const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    if (now - upgradeStartNs_ < static_cast<int64_t>(timeout * 1e9)) return;
    // The 101 may arrive on the receive thread at the same moment: only one
    // side ends the wait
    if (!awaitingUpgrade_.exchange(false)) return;

    // Stop and join the transport before user callbacks can reconnect. Suppress
    // its synchronous onDisconnect so onError still precedes onClose.
    disconnectListener_.disconnect();
    disconnect();
    std::shared_ptr<std::atomic<bool>> alive = alive_;
    unsigned connection = connection_;
    char seconds[32];
    snprintf(seconds, sizeof(seconds), "%g", timeout);
    TcpErrorEventArgs err;
    err.message = std::string("WebSocket handshake timeout: no 101 response within ") +
                  seconds + " s of the TCP connect";
    onError.notify(err);
    if (!*alive || connection_ != connection) return;
    onClose.notify();
#endif
}

void WebSocketClient::ConnectionState::sendHandshake() {
#ifndef __EMSCRIPTEN__
    // Generate Sec-WebSocket-Key
    unsigned char randomBytes[16];
    std::random_device rd;
    for (int i = 0; i < 16; ++i) randomBytes[i] = rd() & 0xFF;
    handshakeNonce_ = toBase64(randomBytes, 16);

    std::string handshake = 
        "GET " + path_ + " HTTP/1.1\r\n"
        "Host: " + host_ + "\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: " + handshakeNonce_ + "\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    client_->send(handshake);
#endif
}

void WebSocketClient::ConnectionState::handleRawReceive(TcpReceiveEventArgs& args) {
#ifndef __EMSCRIPTEN__
    size_t offset = 0;
    while (offset < args.data.size() && !ingressRejected_) {
        if (state_ == State::Connecting) {
            // Scan only new bytes. The terminator may span TCP deliveries;
            // bytes after it belong to the first frame, not to the header.
            // Bound (security): upgrade metadata needs a finite memory allowance. Overflow rejects the upgrade, closes once, warns once and is counted.
            if (receiveBuffer_.size() == maxHandshakeSize_) {
                rejectIngress(IngressLimit::Upgrade, 1009, "WebSocket upgrade header limit exceeded");
                return;
            }
            receiveBuffer_.push_back(args.data[offset++]);
            if (receiveBuffer_.size() < 4 ||
                !std::equal(receiveBuffer_.end() - 4, receiveBuffer_.end(), "\r\n\r\n")) continue;
            std::string header(receiveBuffer_.begin(), receiveBuffer_.end() - 4);
            receiveBuffer_.clear();
            auto alive = alive_;
            unsigned connection = connection_;
            processHandshake(header);
            if (!*alive || connection_ != connection) return;
        } else if (state_ == State::Open) {
            // First collect only the frame header. processFrame validates its
            // length before admitting any payload from this TCP delivery.
            size_t count = std::min(frameBytesNeeded_ - receiveBuffer_.size(), args.data.size() - offset);
            reserveIngress(receiveBuffer_, receiveBuffer_.size() + count, frameBytesNeeded_);
            receiveBuffer_.insert(receiveBuffer_.end(), args.data.begin() + offset,
                                  args.data.begin() + offset + count);
            offset += count;
            auto alive = alive_;
            unsigned connection = connection_;
            processFrame();
            if (!*alive || connection_ != connection) return;
        } else {
            return;
        }
    }
#endif
}

void WebSocketClient::ConnectionState::processHandshake(const std::string& header) {
#ifndef __EMSCRIPTEN__
    // The handshake timeout on the main thread may have ended the wait
    // already; it closes the connection
    if (!awaitingUpgrade_.exchange(false)) return;
    if (header.find("101 Switching Protocols") != std::string::npos) {
        state_ = State::Open;
        std::shared_ptr<std::atomic<bool>> alive = alive_;
        unsigned connection = connection_;
        onOpen.notify();
        // An onOpen listener may have disconnected, reconnected or
        // destroyed the client
        if (!*alive || connection_ != connection) return;

        // If there's more data in buffer, process it as a frame
        if (!receiveBuffer_.empty()) {
            processFrame();
        }
    } else {
        logError() << "WebSocket handshake failed:\n" << header;
        disconnect();
    }
#endif
}

void WebSocketClient::ConnectionState::processFrame() {
#ifndef __EMSCRIPTEN__
    while (receiveBuffer_.size() >= 2) {
        unsigned char b1 = (unsigned char)receiveBuffer_[0];
        unsigned char b2 = (unsigned char)receiveBuffer_[1];

        bool fin = (b1 & 0x80) != 0;
        int opcode = b1 & 0x0F;
        bool masked = (b2 & 0x80) != 0;
        uint64_t payloadLen = b2 & 0x7F;

        size_t headerSize = 2;
        if (payloadLen == 126) {
            if (receiveBuffer_.size() < 4) { frameBytesNeeded_ = 4; return; }
            payloadLen = ((unsigned char)receiveBuffer_[2] << 8) | (unsigned char)receiveBuffer_[3];
            headerSize = 4;
        } else if (payloadLen == 127) {
            if (receiveBuffer_.size() < 10) { frameBytesNeeded_ = 10; return; }
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLen = (payloadLen << 8) | (unsigned char)receiveBuffer_[2 + i];
            }
            headerSize = 10;
        }

        // RFC 6455 requires failing these frames, as Chrome and Firefox do.
        // The web build already uses the browser WebSocket; native matches
        // that established behavior (no extensions are negotiated).
        if (b1 & 0x70) {
            failConnection(1002, "WebSocket protocol error: RSV bits set");
            return;
        }
        if ((opcode >= 0x3 && opcode <= 0x7) || opcode >= 0xB) {
            failConnection(1002, "WebSocket protocol error: reserved opcode");
            return;
        }
        if (opcode >= 0x8) {
            if (!fin) {
                failConnection(1002, "WebSocket protocol error: fragmented control frame (FIN=0)");
                return;
            }
            // Bound (security): control frames must fit the RFC 6455 allowance. Overflow rejects the frame with Close 1002, warns once and is counted.
            if (payloadLen > 125) {
                rejectIngress(IngressLimit::Control, 1002, "WebSocket protocol error: control frame payload over 125 bytes");
                return;
            }
        }
        if (masked) {
            failConnection(1002, "WebSocket protocol error: masked frame from server");
            return;
        }

        // Size and fragmentation checks need only the header, so a bad frame
        // fails the connection before its payload is buffered. The size check
        // comes first, so headerSize + payloadLen below cannot overflow.
        bool isData = (opcode == 0x0 || opcode == 0x1 || opcode == 0x2);
        uint64_t alreadyBuffered = (opcode == 0x0) ? fragmentBuffer_.size() : 0;
        // Bound (security): frame and fragmented totals share the message allowance. Overflow rejects the message with Close 1009, warns once and is counted.
        if (isData && (payloadLen > maxMessageSize_ ||
                       payloadLen > maxMessageSize_ - alreadyBuffered)) {
            rejectIngress(IngressLimit::Message, 1009, "WebSocket message too big: message limit exceeded");
            return;
        }
        if (opcode == 0x0 && fragmentOpcode_ == 0) {
            failConnection(1002, "WebSocket protocol error: unexpected continuation frame");
            return;
        }
        if ((opcode == 0x1 || opcode == 0x2) && fragmentOpcode_ != 0) {
            failConnection(1002, "WebSocket protocol error: new message started "
                                 "before the fragmented message finished");
            return;
        }

        frameBytesNeeded_ = headerSize + static_cast<size_t>(payloadLen);
        if (receiveBuffer_.size() < frameBytesNeeded_) {
            // An advertised length alone does not allocate its payload storage.
            return;
        }

        receiveBuffer_.erase(receiveBuffer_.begin(), receiveBuffer_.begin() + headerSize);
        std::vector<char> payload = std::move(receiveBuffer_);
        receiveBuffer_.clear();
        frameBytesNeeded_ = 2;

        // Handle Opcode. A message may be split into a Text/Binary frame with
        // FIN=0 and continuation frames (opcode 0), the last with FIN=1
        // (RFC 6455 5.4); it is delivered once, on FIN, with the first frame's
        // type. Control frames may arrive between fragments and are handled
        // as usual without touching the message in progress.
        int messageOpcode = 0;
        if (opcode == 0x1 || opcode == 0x2) { // Text or Binary
            if (fin) {
                messageOpcode = opcode;
            } else {
                reserveIngress(fragmentBuffer_, payload.size(), maxMessageSize_);
                fragmentBuffer_.assign(payload.begin(), payload.end());
                fragmentOpcode_ = opcode;
            }
        } else if (opcode == 0x0) { // Continuation
            reserveIngress(fragmentBuffer_, fragmentBuffer_.size() + payload.size(), maxMessageSize_);
            fragmentBuffer_.insert(fragmentBuffer_.end(), payload.begin(), payload.end());
            if (fin) {
                messageOpcode = fragmentOpcode_;
                payload = std::move(fragmentBuffer_);
                fragmentBuffer_.clear();
                fragmentOpcode_ = 0;
            }
        } else if (opcode == 0x8) { // Close
            // disconnect() fires onClose, whose listener may reconnect or
            // destroy the client: nothing is read after it
            disconnect();
            return;
        } else if (opcode == 0x9) { // Ping
            sendPong(payload);       // RFC 6455 5.5.2/5.5.3: reply, echoing the payload
        }

        if (messageOpcode != 0) {
            WebSocketEventArgs args;
            args.isBinary = (messageOpcode == 0x2);
            if (!args.isBinary) {
                args.message.assign(payload.begin(), payload.end());
            }
            args.data = std::move(payload);
            std::shared_ptr<std::atomic<bool>> alive = alive_;
            unsigned connection = connection_;
            onMessage.notify(args);
            // An onMessage listener may have disconnected, reconnected or
            // destroyed the client
            if (!*alive || connection_ != connection) return;
        }
    }
#endif
}

bool WebSocketClient::ConnectionState::send(const std::string& message) {
#ifdef __EMSCRIPTEN__
    if (wsHandle_ <= 0) return false;
    EMSCRIPTEN_RESULT res = emscripten_websocket_send_utf8_text(wsHandle_, message.c_str());
    return (res == EMSCRIPTEN_RESULT_SUCCESS);
#else
    return send(std::vector<char>(message.begin(), message.end()));
#endif
}

bool WebSocketClient::ConnectionState::send(const std::vector<char>& data) {
    if (state_ != State::Open) return false;

#ifdef __EMSCRIPTEN__
    if (wsHandle_ <= 0) return false;
    EMSCRIPTEN_RESULT res = emscripten_websocket_send_binary(wsHandle_, (void*)data.data(), data.size());
    return (res == EMSCRIPTEN_RESULT_SUCCESS);
#else
    std::vector<unsigned char> frame;
    // FIN=1, Opcode=1 (Text) or 2 (Binary)
    // We'll use 0x81 (FIN + Text) for now as common case
    frame.push_back(0x81); 

    size_t len = data.size();
    if (len < 126) {
        frame.push_back(0x80 | (unsigned char)len);
    } else if (len < 65536) {
        frame.push_back(0x80 | 126);
        frame.push_back((len >> 8) & 0xFF);
        frame.push_back(len & 0xFF);
    } else {
        frame.push_back(0x80 | 127);
        for (int i = 7; i >= 0; --i) {
            frame.push_back((len >> (i * 8)) & 0xFF);
        }
    }

    // Client must mask payload
    unsigned char mask[4];
    std::random_device rd;
    for (int i = 0; i < 4; ++i) mask[i] = rd() & 0xFF;
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; ++i) {
        frame.push_back((unsigned char)data[i] ^ mask[i % 4]);
    }

    return client_->send(frame.data(), frame.size());
#endif
}

// Reply to a server Ping with a Pong. RFC 6455 requires the Pong to echo the
// Ping's application data. Skipping this let an idle command channel get reaped
// by proxies that keep connections alive via pings (e.g. Cloudflare): the app
// kept reporting "online" over its HTTP heartbeat while its WS silently dropped,
// so the server-side live flag went false and remote controls disappeared.
void WebSocketClient::ConnectionState::sendPong(const std::vector<char>& payload) {
    sendControl(0x0A, payload.data(), payload.size());
}

// Ping the peer. See the header for why a long-lived client needs this.
bool WebSocketClient::ConnectionState::sendPing(const std::string& payload) {
    return sendControl(0x09, payload.data(), payload.size());
}

// One masked control frame. Control frames carry at most 125 bytes (RFC 6455
// 5.5), so a single-byte length is always sufficient here.
//
// The whole frame ships in one client_->send(), which TcpClient serializes under
// its send mutex, so this stays frame-atomic against sends from other threads.
bool WebSocketClient::ConnectionState::sendControl(uint8_t opcode, const char* data, size_t len) {
#ifndef __EMSCRIPTEN__
    if (state_ != State::Open || !client_) return false;

    std::array<char, 125> truncated;
    // Bound (platform): RFC 6455 control frames permit at most 125 payload bytes. Overflow truncates the payload, warns once per owning client and counts every truncation.
    if (len > 125) {
        len = 125;
        // The synchronous log listener may destroy the owner of borrowed data.
        std::copy_n(data, len, truncated.data());
        data = truncated.data();
        ++rejections_->controls;
        if (!rejections_->controlTruncationWarned.exchange(true)) {
            auto alive = alive_;
            unsigned connection = connection_;
            logWarning("WebSocket") << "WebSocket control payload truncated to 125 bytes";
            if (!*alive || connection_ != connection || state_ != State::Open || !client_) return false;
        }
    }
    std::vector<unsigned char> frame;
    frame.reserve(6 + len);
    frame.push_back(0x80 | opcode);               // FIN + opcode
    frame.push_back(0x80 | (unsigned char)len);   // MASK bit + payload length

    unsigned char mask[4];
    std::random_device rd;
    for (int i = 0; i < 4; ++i) mask[i] = rd() & 0xFF;
    frame.insert(frame.end(), mask, mask + 4);

    for (size_t i = 0; i < len; ++i) {
        frame.push_back((unsigned char)data[i] ^ mask[i % 4]);
    }

    return client_->send(frame.data(), frame.size());
#else
    (void)opcode; (void)data; (void)len;
    return false;
#endif
}

void WebSocketClient::ConnectionState::failConnection(uint16_t statusCode, const std::string& reason) {
#ifndef __EMSCRIPTEN__
    // Close goes out first: sendControl() only sends while the state is Open.
    const char status[2] = {(char)((statusCode >> 8) & 0xFF), (char)(statusCode & 0xFF)};
    sendControl(0x08, status, 2);

    state_ = State::Closing;

    // onError fires before disconnect(), which fires onClose synchronously.
    // A listener may disconnect, reconnect or destroy the client (#262).
    std::shared_ptr<std::atomic<bool>> alive = alive_;
    unsigned connection = connection_;
    TcpErrorEventArgs err;
    err.message = reason;
    onError.notify(err);
    if (!*alive || connection_ != connection) return;
    disconnect();
#else
    // Browser close() permits 1000 or application codes 3000-4999. Use
    // application code 4009 for the same message-size rejection as native 1009.
    if (wsHandle_ > 0) emscripten_websocket_close(wsHandle_, statusCode == 1009 ? 4009 : 4000, reason.c_str());
    state_ = State::Closing;
    auto alive = alive_;
    unsigned connection = connection_;
    TcpErrorEventArgs err;
    err.message = reason;
    onError.notify(err);
    if (!*alive || connection_ != connection) return;
    // Keep the browser's close callback: it reports onClose once.
#endif
}

void WebSocketClient::ConnectionState::rejectIngress(IngressLimit limit, uint16_t statusCode,
                                                     const std::string& reason) {
    if (ingressRejected_) return;
#ifndef __EMSCRIPTEN__
    if (limit == IngressLimit::Upgrade && !awaitingUpgrade_.exchange(false)) return;
#endif
    ingressRejected_ = true;
    switch (limit) {
        case IngressLimit::Upgrade: ++rejections_->upgrades; break;
        case IngressLimit::Message: ++rejections_->messages; break;
        case IngressLimit::Control: ++rejections_->controls; break;
    }
    auto alive = alive_;
    unsigned connection = connection_;
    logWarning("WebSocket") << reason;
    if (!*alive || connection_ != connection) return;
    failConnection(statusCode, reason);
}

#ifdef __EMSCRIPTEN__
void WebSocketClient::ConnectionState::onEmscriptenMessageLimit(void* userData) {
    auto self = static_cast<ConnectionState*>(userData)->shared_from_this();
    if (!*self->alive_) return;
    self->rejectIngress(IngressLimit::Message, 1009, "WebSocket message too big: message limit exceeded");
}

EM_BOOL WebSocketClient::ConnectionState::onEmscriptenOpen(int eventType, const EmscriptenWebSocketOpenEvent *websocketEvent, void *userData) {
    auto keepAlive = static_cast<ConnectionState*>(userData)->shared_from_this();
    auto* self = keepAlive.get();
    if (!*self->alive_) return EM_TRUE;
    self->state_ = State::Open;
    self->onOpen.notify();
    return EM_TRUE;
}

EM_BOOL WebSocketClient::ConnectionState::onEmscriptenMessage(int eventType, const EmscriptenWebSocketMessageEvent *websocketEvent, void *userData) {
    auto keepAlive = static_cast<ConnectionState*>(userData)->shared_from_this();
    auto* self = keepAlive.get();
    if (!*self->alive_ || self->ingressRejected_ || self->state_ != State::Open) return EM_TRUE;
    // Defensive C++ admission as well; the JS boundary checks before WASM allocation.
    size_t bytes = websocketEvent->numBytes;
    if (websocketEvent->isText && bytes > 0 && websocketEvent->data[bytes - 1] == 0) --bytes;
    // Bound (security): delivered messages share the native message allowance. Overflow rejects the message with Close 4009, warns once and is counted.
    if (bytes > self->maxMessageSize_) {
        onEmscriptenMessageLimit(userData);
        return EM_TRUE;
    }
    WebSocketEventArgs args;
    // Text messages arrive NUL-terminated with the terminator counted in
    // numBytes; fillMessageArgs drops it so web matches native.
    detail::fillMessageArgs(args, websocketEvent->data, websocketEvent->numBytes,
                            websocketEvent->isText);

    self->onMessage.notify(args);
    return EM_TRUE;
}

EM_BOOL WebSocketClient::ConnectionState::onEmscriptenClose(int eventType, const EmscriptenWebSocketCloseEvent *websocketEvent, void *userData) {
    auto keepAlive = static_cast<ConnectionState*>(userData)->shared_from_this();
    auto* self = keepAlive.get();
    if (!*self->alive_) return EM_TRUE;
    self->state_ = State::Disconnected;
    self->onClose.notify();
    return EM_TRUE;
}

EM_BOOL WebSocketClient::ConnectionState::onEmscriptenError(int eventType, const EmscriptenWebSocketErrorEvent *websocketEvent, void *userData) {
    auto keepAlive = static_cast<ConnectionState*>(userData)->shared_from_this();
    auto* self = keepAlive.get();
    if (!*self->alive_) return EM_TRUE;
    TcpErrorEventArgs args;
    args.message = "WebSocket Error";
    self->onError.notify(args);
    return EM_TRUE;
}
#endif

}  // namespace tcx::websocket
