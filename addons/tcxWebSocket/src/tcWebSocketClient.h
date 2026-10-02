#pragma once

#include "tc/network/tcTcpClient.h"
#ifndef __EMSCRIPTEN__
#include "tcTlsClient.h"
#endif
#include <string>
#include <vector>
#include <memory>
#include <atomic>
#include <chrono>

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
    //
    // THREADING (native): these events fire on the internal threads of the
    // TcpClient / TlsClient underneath, not on the main thread:
    //  - onOpen, onMessage: the receive thread.
    //  - onClose: the receive thread when the server closes the connection or
    //    it fails; the calling thread, before the call returns, when the app
    //    calls disconnect() or connect(). For the handshake timeout below:
    //    the receive thread when it runs out during the TLS handshake, the
    //    main thread when it runs out waiting for the 101.
    //  - onError: the connect or receive thread for a failed connect or TLS
    //    handshake (the TLS handshake timeout included), the receive thread
    //    for a protocol error, the main thread for the 101 timeout.
    // A listener that touches the Node tree, GPU resources, or unguarded app
    // state must opt into main-thread delivery:
    //
    //   listener = ws.onMessage.listen(fn, Deliver::Main);
    //
    // Plain listen(fn) runs inline on the firing thread (see tcTcpClient.h).
    // An inline onClose or onError listener may call connect() to reconnect:
    // the old client's receive thread stops without touching it again (#262).
    // On Emscripten the browser fires them all on the main thread.
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

    // Time allowed for the opening handshake, in seconds, counted from the
    // moment the TCP connection is up. Default 15. 0 = no deadline. One
    // deadline covers both stages: the TLS handshake (wss://) and then the
    // server's "101 Switching Protocols" answer to the upgrade request, so
    // the whole handshake never waits longer than this. When it runs out:
    //  - during the TLS handshake (TlsClient::setHandshakeTimeout(), checked
    //    on the receive thread): the connection is closed, then onError and
    //    onClose fire on the receive thread;
    //  - while waiting for the 101 (checked once per frame on the main
    //    thread, update event, so it needs the app's main loop): onError
    //    fires on the main thread, then the connection is closed and onClose
    //    fires there too, unless an onError listener already disconnected,
    //    reconnected or destroyed the client.
    // The TCP connect before it is not covered: the OS times that out.
    // Applies to the next connect(). No effect on Emscripten, where the
    // browser owns the handshake.
    void setHandshakeTimeout(float seconds) { handshakeTimeout_ = seconds > 0.0f ? seconds : 0.0f; }

private:
    void setupClient(bool useTls);
    void handleRawReceive(TcpReceiveEventArgs& args);
    void handleTcpConnect(TcpConnectEventArgs& args);
    void handleTcpDisconnect(TcpDisconnectEventArgs& args);

    // Main thread, every frame: fails the connection when the 101 has not
    // arrived within handshakeTimeout_ of the TCP connect (onError, then
    // disconnect() -> onClose, skipped when an onError listener already
    // disconnected, reconnected or destroyed the client)
    void checkHandshakeTimeout();

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

    // Default for setHandshakeTimeout(), in seconds. Why 15:
    //  - The OS never times out a server that accepted the TCP connection
    //    and then stays silent (a hung server process, a captive portal, a
    //    middlebox), so without a deadline the client stays Connecting
    //    forever and no event fires.
    //  - A TLS handshake plus the HTTP 101 is a few round trips and normally
    //    finishes in well under a few seconds, even on slow links.
    //  - The value is deliberately generous: a too-short timeout broke
    //    Schannel renegotiation in tcxCurl (commit a3b79116). A longer value
    //    only delays noticing a stalled server.
    // The TCP connect stage has an OS timeout of its own and is left to it.
    static constexpr float defaultHandshakeTimeout_ = 15.0f;

    std::unique_ptr<TcpClient> client_;
    EventListener receiveListener_;
    EventListener connectListener_;
    EventListener disconnectListener_;
    EventListener timeoutListener_;   // update event, checkHandshakeTimeout()

    // setHandshakeTimeout()'s value (atomic: read on the receive thread too)
    std::atomic<float> handshakeTimeout_{defaultHandshakeTimeout_};
    // Set when the upgrade request goes out (the TCP / TLS connection is up)
    // and cleared by whoever ends the wait first: the 101 on the receive
    // thread, the timeout on the main thread, the server closing the
    // connection (handleTcpDisconnect()), or disconnect().
    std::atomic<bool> awaitingUpgrade_{false};
    // Where the 101 deadline counts from, as steady_clock nanoseconds: the
    // TCP connect (for wss://, TlsClient::getTcpConnectTime())
    std::atomic<int64_t> upgradeStartNs_{0};

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
