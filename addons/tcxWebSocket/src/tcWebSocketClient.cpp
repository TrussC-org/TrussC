#include "tcWebSocketClient.h"
#include <TrussC.h>
#include <algorithm>
#include <random>
#include <cstring>

using namespace std;
using namespace tc;

namespace trussc {

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

WebSocketClient::WebSocketClient() {}

WebSocketClient::~WebSocketClient() {
    disconnect();
}

bool WebSocketClient::connect(const std::string& url) {
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

    state_ = State::Connecting;
    setupClient(useTls_);

    client_->connectAsync(host_, port_);
    return true;
#endif
}

void WebSocketClient::disconnect() {
#ifdef __EMSCRIPTEN__
    if (wsHandle_ > 0) {
        emscripten_websocket_close(wsHandle_, 1000, "Normal closure");
        emscripten_websocket_delete(wsHandle_);
        wsHandle_ = 0;
    }
#else
    if (client_) {
        client_->disconnect();
    }
#endif
    state_ = State::Disconnected;
    receiveBuffer_.clear();
}

void WebSocketClient::setupClient(bool useTls) {
#ifndef __EMSCRIPTEN__
    if (useTls) {
        auto tls = std::make_unique<TlsClient>();
        tls->setVerifyNone(); // Default for creative coding, maybe add config later
        client_ = std::move(tls);
    } else {
        client_ = std::make_unique<TcpClient>();
    }

    // Connect event
    client_->onConnect.listen(connectListener_, this, &WebSocketClient::handleTcpConnect);
    // Receive event
    client_->onReceive.listen(receiveListener_, this, &WebSocketClient::handleRawReceive);
    // Disconnect event
    client_->onDisconnect.listen(disconnectListener_, this, &WebSocketClient::handleTcpDisconnect);
#endif
}

void WebSocketClient::handleTcpConnect(TcpConnectEventArgs& args) {
#ifndef __EMSCRIPTEN__
    if (args.success) {
        sendHandshake();
    } else {
        state_ = State::Disconnected;
        TcpErrorEventArgs err;
        err.message = "TCP Connection failed: " + args.message;
        onError.notify(err);
    }
#endif
}

void WebSocketClient::handleTcpDisconnect(TcpDisconnectEventArgs& args) {
#ifndef __EMSCRIPTEN__
    state_ = State::Disconnected;
    onClose.notify();
#endif
}

void WebSocketClient::sendHandshake() {
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

void WebSocketClient::handleRawReceive(TcpReceiveEventArgs& args) {
#ifndef __EMSCRIPTEN__
    receiveBuffer_.insert(receiveBuffer_.end(), args.data.begin(), args.data.end());

    if (state_ == State::Connecting) {
        // Look for end of HTTP header
        std::string bufferStr(receiveBuffer_.begin(), receiveBuffer_.end());
        size_t headerEnd = bufferStr.find("\r\n\r\n");
        if (headerEnd != std::string::npos) {
            std::string header = bufferStr.substr(0, headerEnd);
            receiveBuffer_.erase(receiveBuffer_.begin(), receiveBuffer_.begin() + headerEnd + 4);
            processHandshake(header);
        }
    } else if (state_ == State::Open) {
        processFrame();
    }
#endif
}

void WebSocketClient::processHandshake(const std::string& header) {
#ifndef __EMSCRIPTEN__
    if (header.find("101 Switching Protocols") != std::string::npos) {
        state_ = State::Open;
        onOpen.notify();
        
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

void WebSocketClient::processFrame() {
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
            if (receiveBuffer_.size() < 4) return;
            payloadLen = ((unsigned char)receiveBuffer_[2] << 8) | (unsigned char)receiveBuffer_[3];
            headerSize = 4;
        } else if (payloadLen == 127) {
            if (receiveBuffer_.size() < 10) return;
            payloadLen = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLen = (payloadLen << 8) | (unsigned char)receiveBuffer_[2 + i];
            }
            headerSize = 10;
        }

        uint8_t maskingKey[4] = {0, 0, 0, 0};
        if (masked) {
            if (receiveBuffer_.size() < headerSize + 4) return;
            memcpy(maskingKey, &receiveBuffer_[headerSize], 4);
            headerSize += 4;
        }

        if (receiveBuffer_.size() < headerSize + payloadLen) return;

        std::vector<char> payload(payloadLen);
        if (payloadLen > 0) {
            memcpy(payload.data(), &receiveBuffer_[headerSize], payloadLen);
            if (masked) {
                for (size_t i = 0; i < payloadLen; ++i) {
                    payload[i] ^= maskingKey[i % 4];
                }
            }
        }

        // Remove processed frame from buffer
        receiveBuffer_.erase(receiveBuffer_.begin(), receiveBuffer_.begin() + headerSize + payloadLen);

        // Handle Opcode
        if (opcode == 0x1 || opcode == 0x2) { // Text or Binary
            WebSocketEventArgs args;
            args.isBinary = (opcode == 0x2);
            args.data = payload;
            if (!args.isBinary) {
                args.message.assign(payload.begin(), payload.end());
            }
            onMessage.notify(args);
        } else if (opcode == 0x8) { // Close
            disconnect();
        } else if (opcode == 0x9) { // Ping
            // Send Pong (not implemented yet)
        }

        if (!fin) {
            // Continuation frame handling needed for full implementation
            logWarning() << "WebSocket: Continuation frames not yet fully supported";
        }
    }
#endif
}

bool WebSocketClient::send(const std::string& message) {
#ifdef __EMSCRIPTEN__
    if (wsHandle_ <= 0) return false;
    EMSCRIPTEN_RESULT res = emscripten_websocket_send_utf8_text(wsHandle_, message.c_str());
    return (res == EMSCRIPTEN_RESULT_SUCCESS);
#else
    return send(std::vector<char>(message.begin(), message.end()));
#endif
}

bool WebSocketClient::send(const std::vector<char>& data) {
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

#ifdef __EMSCRIPTEN__
EM_BOOL WebSocketClient::onEmscriptenOpen(int eventType, const EmscriptenWebSocketOpenEvent *websocketEvent, void *userData) {
    WebSocketClient* self = static_cast<WebSocketClient*>(userData);
    self->state_ = State::Open;
    self->onOpen.notify();
    return EM_TRUE;
}

EM_BOOL WebSocketClient::onEmscriptenMessage(int eventType, const EmscriptenWebSocketMessageEvent *websocketEvent, void *userData) {
    WebSocketClient* self = static_cast<WebSocketClient*>(userData);
    WebSocketEventArgs args;
    args.isBinary = !websocketEvent->isText;
    
    // Copy data
    if (websocketEvent->numBytes > 0) {
        args.data.assign(websocketEvent->data, websocketEvent->data + websocketEvent->numBytes);
        if (!args.isBinary) {
            args.message.assign(reinterpret_cast<char*>(websocketEvent->data), websocketEvent->numBytes);
        }
    }
    
    self->onMessage.notify(args);
    return EM_TRUE;
}

EM_BOOL WebSocketClient::onEmscriptenClose(int eventType, const EmscriptenWebSocketCloseEvent *websocketEvent, void *userData) {
    WebSocketClient* self = static_cast<WebSocketClient*>(userData);
    self->state_ = State::Disconnected;
    self->onClose.notify();
    return EM_TRUE;
}

EM_BOOL WebSocketClient::onEmscriptenError(int eventType, const EmscriptenWebSocketErrorEvent *websocketEvent, void *userData) {
    WebSocketClient* self = static_cast<WebSocketClient*>(userData);
    TcpErrorEventArgs args;
    args.message = "WebSocket Error";
    self->onError.notify(args);
    return EM_TRUE;
}
#endif

} // namespace trussc