#pragma once

#include "tc/network/tcTcpClient.h"
#ifndef __EMSCRIPTEN__
#include "tcTlsClient.h"
#endif
#include <string>
#include <vector>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/websocket.h>
#endif

using namespace std;
using namespace tc;

namespace tcx::websocket {

// =============================================================================
// WebSocket Event Args
// =============================================================================
struct WebSocketEventArgs {
    std::string message;
    std::vector<char> data;
    bool isBinary = false;
};

namespace detail {
// Fills args from a message as Emscripten's WebSocket API reports it. For a
// text message, Emscripten writes the UTF-8 string NUL-terminated and counts
// the terminator in numBytes; it is dropped here so the web build delivers
// the same message and data as native ("hello" -> 5 bytes). Binary messages
// are copied as they are.
inline void fillMessageArgs(WebSocketEventArgs& args, const unsigned char* data,
                            size_t numBytes, bool isText) {
    args.isBinary = !isText;
    args.data.clear();
    args.message.clear();
    if (numBytes == 0 || data == nullptr) return;
    if (isText && data[numBytes - 1] == 0) --numBytes;
    args.data.assign(data, data + numBytes);
    if (isText) args.message.assign(reinterpret_cast<const char*>(data), numBytes);
}
}  // namespace detail

// =============================================================================
// WebSocketClient
// =============================================================================
class WebSocketClient {
public:
    enum class State {
        Disconnected,
        Connecting,
        Open,
        Closing
    };

    // -------------------------------------------------------------------------
    // Events
    // -------------------------------------------------------------------------
    Event<void> onOpen;
    Event<WebSocketEventArgs> onMessage;
    Event<void> onClose;
    Event<TcpErrorEventArgs> onError;

    // -------------------------------------------------------------------------
    // Constructor / Destructor
    // -------------------------------------------------------------------------
    WebSocketClient();
    ~WebSocketClient();

    // -------------------------------------------------------------------------
    // Management
    // -------------------------------------------------------------------------
    bool connect(const std::string& url);
    void disconnect();

    bool send(const std::string& message);
    bool send(const std::vector<char>& data);

    // Send a Ping. Answering the server's Pings keeps a reachable connection
    // alive, but it cannot reveal one that is gone: when the network drops
    // silently mid-path, the server's Ping and its close never arrive, so the
    // client stays Open and never reconnects. A long-lived client should ping
    // on a timer, so a dead connection surfaces through TCP -- a retransmission
    // timeout, or an RST once the path is back -- instead of hanging until the
    // app restarts. It also keeps idle timeouts in proxies from firing when the
    // server does not ping.
    //
    // The Pong is not reported to the app; a failure shows up as a disconnect.
    // Payload is capped at 125 bytes (RFC 6455 5.5: control frames).
    // Returns false when not connected, and on Emscripten, where the browser
    // owns the connection and exposes no ping.
    bool sendPing(const std::string& payload = "keepalive");

    State getState() const { return state_; }
    bool isConnected() const { return state_ == State::Open; }

    // TLS certificate verification (wss:// only, no effect on Emscripten).
    // Default: verification is REQUIRED. Calling setTlsVerifyNone() before connect()
    // disables verification — intended only for development against self-signed
    // certs. Never use in production as it silently allows MITM.
    void setTlsVerifyNone(bool disable = true) { tlsVerifyNone_ = disable; }
    // Optional custom CA certificate (PEM). Applied to the underlying TlsClient on
    // connect(). Empty string means use the system default trust store.
    void setTlsCACertificate(const std::string& pem) { tlsCaPem_ = pem; }

private:
    void setupClient(bool useTls);
    void handleRawReceive(TcpReceiveEventArgs& args);
    void handleTcpConnect(TcpConnectEventArgs& args);
    void handleTcpDisconnect(TcpDisconnectEventArgs& args);

    void sendHandshake();
    void processHandshake(const std::string& header);
    void processFrame();
    void sendPong(const std::vector<char>& payload);

    // One masked control frame (Ping / Pong / Close), sent in a single write.
    bool sendControl(uint8_t opcode, const char* data, size_t len);

    // Fails the connection (RFC 6455 7.1.7): sends Close with statusCode,
    // notifies onError, then disconnect() (which fires onClose). disconnect()
    // is skipped when the onError listener already disconnected, reconnected
    // or destroyed the client. The caller must return right after without
    // touching members: any of the listeners may have done so (#262).
    void failConnection(uint16_t statusCode, const std::string& reason);

    // Largest message accepted, in bytes: one frame, or all fragments of a
    // fragmented message together. A server that sends more gets Close 1009
    // (Message Too Big) and the app gets onError then onClose. The size is
    // checked from the frame header, before the payload is buffered.
    // 64 MiB is far above typical WebSocket traffic (JSON, commands, encoded
    // images or audio chunks) and keeps the memory one message can take
    // bounded to a few such buffers (receive buffer, reassembled message, the
    // copy handed to onMessage), also in a long-running app.
    static constexpr uint64_t maxMessageSize_ = 64ull * 1024 * 1024;

    std::unique_ptr<TcpClient> client_;
    EventListener receiveListener_;
    EventListener connectListener_;
    EventListener disconnectListener_;

    State state_ = State::Disconnected;
    std::string host_;
    std::string path_ = "/";
    int port_ = 80;
    bool useTls_ = false;

    std::vector<char> receiveBuffer_;
    // Fragmented message in progress (RFC 6455 5.4): payload of the frames so
    // far and the opcode of its first frame (0x1 text, 0x2 binary; 0 = none).
    std::vector<char> fragmentBuffer_;
    int fragmentOpcode_ = 0;

    // Lets failConnection() see, after notifying, whether a listener destroyed
    // the client (alive_ false) or disconnected / reconnected it (connection_
    // changed). alive_ is copied before the notification and checked first.
    std::shared_ptr<bool> alive_ = std::make_shared<bool>(true);
    unsigned connection_ = 0;
    std::string handshakeNonce_;

    bool tlsVerifyNone_ = false;
    std::string tlsCaPem_;

#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_WEBSOCKET_T wsHandle_ = 0;
    static EM_BOOL onEmscriptenOpen(int eventType, const EmscriptenWebSocketOpenEvent *websocketEvent, void *userData);
    static EM_BOOL onEmscriptenMessage(int eventType, const EmscriptenWebSocketMessageEvent *websocketEvent, void *userData);
    static EM_BOOL onEmscriptenClose(int eventType, const EmscriptenWebSocketCloseEvent *websocketEvent, void *userData);
    static EM_BOOL onEmscriptenError(int eventType, const EmscriptenWebSocketErrorEvent *websocketEvent, void *userData);
#endif
};

}  // namespace tcx::websocket

// -----------------------------------------------------------------------------
// Backward compatibility. The canonical namespace is now `tcx::websocket`. These
// silent aliases keep older code compiling: flat `tcx::WebSocketClient` and
// legacy `trussc::WebSocketClient`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]] attribute: under the usual `using namespace tc;` it would
//  warn on idiomatic unqualified use too. See tcxWebSocket README for migration.)
// -----------------------------------------------------------------------------
namespace tcx    { using websocket::WebSocketEventArgs; using websocket::WebSocketClient; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::websocket::WebSocketEventArgs; using tcx::websocket::WebSocketClient; } // deprecated: remove at v1.0.0
