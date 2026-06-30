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
