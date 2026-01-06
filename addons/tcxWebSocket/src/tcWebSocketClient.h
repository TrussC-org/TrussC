#pragma once

#include "tc/network/tcTcpClient.h"
#include "tcTlsClient.h"
#include <string>
#include <vector>
#include <memory>

#ifdef __EMSCRIPTEN__
#include <emscripten/websocket.h>
#endif

using namespace std;
using namespace tc;

namespace trussc {

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

#ifdef __EMSCRIPTEN__
    EMSCRIPTEN_WEBSOCKET_T wsHandle_ = 0;
    static EM_BOOL onEmscriptenOpen(int eventType, const EmscriptenWebSocketOpenEvent *websocketEvent, void *userData);
    static EM_BOOL onEmscriptenMessage(int eventType, const EmscriptenWebSocketMessageEvent *websocketEvent, void *userData);
    static EM_BOOL onEmscriptenClose(int eventType, const EmscriptenWebSocketCloseEvent *websocketEvent, void *userData);
    static EM_BOOL onEmscriptenError(int eventType, const EmscriptenWebSocketErrorEvent *websocketEvent, void *userData);
#endif
};

} // namespace trussc

namespace tc = trussc;
