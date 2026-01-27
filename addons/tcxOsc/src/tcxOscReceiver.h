#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include <queue>
#include <mutex>
#include <atomic>

namespace trussc {

// =============================================================================
// OscReceiver - OSC receiver class
// =============================================================================
class OscReceiver {
public:
    // Events
    Event<OscMessage> onMessageReceived;   // Message received
    Event<OscBundle> onBundleReceived;     // Bundle received
    Event<std::string> onParseError;       // Parse error (for robustness)

    OscReceiver() = default;
    ~OscReceiver() { close(); }

    // Non-copyable
    OscReceiver(const OscReceiver&) = delete;
    OscReceiver& operator=(const OscReceiver&) = delete;

    // -------------------------------------------------------------------------
    // Setup
    // -------------------------------------------------------------------------

    // Start receiving
    bool setup(int port) {
        port_ = port;

        // Set receive event handler
        receiveListener_ = socket_.onReceive.listen([this](UdpReceiveEventArgs& args) {
            handleReceive(args);
        });

        errorListener_ = socket_.onError.listen([this](UdpErrorEventArgs& args) {
            std::string msg = "Socket error: " + args.message;
            onParseError.notify(msg);
        });

        return socket_.bind(port, true);  // Auto-start receive thread
    }

    // Close
    void close() {
        socket_.close();
        receiveListener_.disconnect();
        errorListener_.disconnect();
        port_ = 0;
    }

    // -------------------------------------------------------------------------
    // Info
    // -------------------------------------------------------------------------

    int getPort() const { return port_; }
    bool isListening() const { return socket_.isReceiving(); }

    // -------------------------------------------------------------------------
    // Polling API (buffer enabled on first call)
    // -------------------------------------------------------------------------

    // Check if there are unread messages (buffer enabled on first call)
    bool hasNewMessage() {
        bufferEnabled_ = true;
        std::lock_guard<std::mutex> lock(queueMutex_);
        return !messageQueue_.empty();
    }

    // Get next message (removes from queue)
    bool getNextMessage(OscMessage& msg) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (messageQueue_.empty()) return false;
        msg = std::move(messageQueue_.front());
        messageQueue_.pop();
        return true;
    }

    // Set buffer size (default 100)
    void setBufferSize(size_t size) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        bufferMax_ = size;
        // Trim queue if over limit
        while (messageQueue_.size() > bufferMax_) {
            messageQueue_.pop();
        }
    }

    size_t getBufferSize() const { return bufferMax_; }

private:
    void handleReceive(UdpReceiveEventArgs& args) {
        if (args.data.empty()) return;

        const uint8_t* data = reinterpret_cast<const uint8_t*>(args.data.data());
        size_t size = args.data.size();

        parsePacket(data, size);
    }

    void parsePacket(const uint8_t* data, size_t size) {
        if (size < 4) {
            std::string err = "Packet too small";
            onParseError.notify(err);
            return;
        }

        // Determine if bundle or message
        if (OscBundle::isBundle(data, size)) {
            bool ok = false;
            OscBundle bundle = OscBundle::fromBytes(data, size, ok);
            if (ok) {
                // Dispatch messages inside bundle individually
                dispatchBundle(bundle);
            }
            else {
                std::string err = "Failed to parse bundle";
                onParseError.notify(err);
            }
        }
        else {
            bool ok = false;
            OscMessage msg = OscMessage::fromBytes(data, size, ok);
            if (ok) {
                // Add to queue if buffer enabled
                if (bufferEnabled_) {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    messageQueue_.push(msg);
                    while (messageQueue_.size() > bufferMax_) {
                        messageQueue_.pop();
                    }
                }
                // Always notify listeners
                onMessageReceived.notify(msg);
            }
            else {
                std::string err = "Failed to parse message";
                onParseError.notify(err);
            }
        }
    }

    // Recursively dispatch messages inside bundle
    void dispatchBundle(OscBundle bundle) {
        onBundleReceived.notify(bundle);

        for (size_t i = 0; i < bundle.getElementCount(); ++i) {
            if (bundle.isMessage(i)) {
                OscMessage msg = bundle.getMessageAt(i);
                // Add to queue if buffer enabled
                if (bufferEnabled_) {
                    std::lock_guard<std::mutex> lock(queueMutex_);
                    messageQueue_.push(msg);
                    while (messageQueue_.size() > bufferMax_) {
                        messageQueue_.pop();
                    }
                }
                onMessageReceived.notify(msg);
            }
            else if (bundle.isBundle(i)) {
                dispatchBundle(bundle.getBundleAt(i));
            }
        }
    }

    UdpSocket socket_;
    int port_ = 0;
    EventListener receiveListener_;
    EventListener errorListener_;

    // Polling buffer
    std::queue<OscMessage> messageQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> bufferEnabled_{false};
    size_t bufferMax_ = 100;
};

}  // namespace trussc
