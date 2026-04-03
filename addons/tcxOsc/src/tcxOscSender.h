#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"
#include <vector>
#include <algorithm>

namespace trussc {

// =============================================================================
// OscSender - OSC sender class (multi-destination, sendTo-based)
// =============================================================================
class OscSender {
public:
    OscSender() = default;
    ~OscSender() { close(); }

    // Non-copyable
    OscSender(const OscSender&) = delete;
    OscSender& operator=(const OscSender&) = delete;

    struct Destination {
        std::string host;
        int port;
        bool operator==(const Destination& o) const {
            return host == o.host && port == o.port;
        }
    };

    // -------------------------------------------------------------------------
    // Destination management
    // -------------------------------------------------------------------------

    // Add destination (creates socket if needed, enables broadcast for .255)
    bool connect(const std::string& host, int port) {
        if (!socket_.isValid()) {
            socket_.create();
        }
        // Enable broadcast if needed
        if (host.size() >= 4 && host.substr(host.size() - 4) == ".255") {
            socket_.setBroadcast(true);
        }
        // Avoid duplicates
        Destination d{host, port};
        for (auto& existing : destinations_) {
            if (existing == d) return true;
        }
        destinations_.push_back(d);
        return true;
    }

    // Remove one destination
    void disconnect(const std::string& host, int port) {
        Destination d{host, port};
        destinations_.erase(
            std::remove(destinations_.begin(), destinations_.end(), d),
            destinations_.end());
    }

    // Remove all destinations (socket stays open)
    void disconnect() {
        destinations_.clear();
    }

    // Convenience: clear + add single destination
    bool setup(const std::string& host, int port) {
        disconnect();
        return connect(host, port);
    }

    // Close socket and clear destinations
    void close() {
        socket_.close();
        destinations_.clear();
    }

    // -------------------------------------------------------------------------
    // Send to all registered destinations
    // -------------------------------------------------------------------------

    bool send(const OscMessage& msg) {
        if (destinations_.empty()) return false;
        auto bytes = msg.toBytes();
        bool ok = true;
        for (auto& d : destinations_) {
            if (!socket_.sendTo(d.host, d.port, bytes.data(), bytes.size())) {
                ok = false;
            }
        }
        return ok;
    }

    bool send(const OscBundle& bundle) {
        if (destinations_.empty()) return false;
        auto bytes = bundle.toBytes();
        bool ok = true;
        for (auto& d : destinations_) {
            if (!socket_.sendTo(d.host, d.port, bytes.data(), bytes.size())) {
                ok = false;
            }
        }
        return ok;
    }

    // -------------------------------------------------------------------------
    // Send to a specific destination (ignores destination list)
    // -------------------------------------------------------------------------

    bool sendTo(const std::string& host, int port, const OscMessage& msg) {
        if (!socket_.isValid()) socket_.create();
        auto bytes = msg.toBytes();
        return socket_.sendTo(host, port, bytes.data(), bytes.size());
    }

    bool sendTo(const std::string& host, int port, const OscBundle& bundle) {
        if (!socket_.isValid()) socket_.create();
        auto bytes = bundle.toBytes();
        return socket_.sendTo(host, port, bytes.data(), bytes.size());
    }

    // -------------------------------------------------------------------------
    // Info
    // -------------------------------------------------------------------------

    const std::vector<Destination>& getConnectedAddresses() const {
        return destinations_;
    }

    bool isConnected() const { return !destinations_.empty(); }

private:
    UdpSocket socket_;
    std::vector<Destination> destinations_;
};

}  // namespace trussc
