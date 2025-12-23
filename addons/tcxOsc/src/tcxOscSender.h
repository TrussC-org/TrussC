#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"

namespace trussc {

// =============================================================================
// OscSender - OSC sender class
// =============================================================================
class OscSender {
public:
    OscSender() = default;
    ~OscSender() { close(); }

    // Non-copyable
    OscSender(const OscSender&) = delete;
    OscSender& operator=(const OscSender&) = delete;

    // -------------------------------------------------------------------------
    // Setup
    // -------------------------------------------------------------------------

    // Set destination
    bool setup(const std::string& host, int port) {
        host_ = host;
        port_ = port;
        return socket_.connect(host, port);
    }

    // Close
    void close() {
        socket_.close();
        host_.clear();
        port_ = 0;
    }

    // -------------------------------------------------------------------------
    // Send
    // -------------------------------------------------------------------------

    // Send message
    bool send(const OscMessage& msg) {
        auto bytes = msg.toBytes();
        return socket_.send(bytes.data(), bytes.size());
    }

    // Send bundle
    bool send(const OscBundle& bundle) {
        auto bytes = bundle.toBytes();
        return socket_.send(bytes.data(), bytes.size());
    }

    // -------------------------------------------------------------------------
    // Info
    // -------------------------------------------------------------------------

    const std::string& getHost() const { return host_; }
    int getPort() const { return port_; }
    bool isConnected() const { return socket_.isValid(); }

private:
    UdpSocket socket_;
    std::string host_;
    int port_ = 0;
};

}  // namespace trussc
