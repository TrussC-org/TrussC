#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcLog.h"
#include <queue>
#include <mutex>
#include <atomic>
#include <chrono>
#include <cstdint>

namespace tcx::osc {

// =============================================================================
// OscReceiver - OSC receiver class
// =============================================================================
class OscReceiver {
public:
    // Events
    tc::Event<OscMessage> onMessageReceived;   // Message received
    // Bundle received. The OscBundle& is the parsed bundle itself (not a
    // copy) and is valid only during that call. A listener may edit that
    // bundle, but must not keep a reference to an enclosing bundle (from an
    // earlier call for the same packet) and modify it (e.g. addMessage() /
    // clear()) while a nested bundle of that packet is being dispatched.
    tc::Event<OscBundle> onBundleReceived;
    tc::Event<std::string> onParseError;       // Parse error (for robustness)

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
        receiveListener_ = socket_.onReceive.listen([this](tc::UdpReceiveEventArgs& args) {
            handleReceive(args);
        });

        errorListener_ = socket_.onError.listen([this](tc::UdpErrorEventArgs& args) {
            std::string msg = "Socket error: " + args.message;
            onParseError.notify(msg);
        });

        // Allow multiple receivers on the same port (SO_REUSEADDR + SO_REUSEPORT).
        // setReusePort matters for multicast: several apps commonly share one
        // multicast port, and macOS/BSD need SO_REUSEPORT (not just REUSEADDR).
        socket_.create();
        socket_.setReuseAddress(true);
        socket_.setReusePort(true);

        return socket_.bind(port, true);  // Auto-start receive thread
    }

    // -------------------------------------------------------------------------
    // Multicast (IPv4)
    // -------------------------------------------------------------------------
    // OSC is usually unicast, but some setups multicast it (one sender, many
    // listeners on a fixed group). Call AFTER setup() so the socket is bound,
    // then the receiver also gets datagrams for `group` (e.g. "239.0.0.1").
    // `iface` selects the NIC (""/"0.0.0.0" = default route).

    bool joinMulticast(const std::string& group, const std::string& iface = "") {
        return socket_.joinMulticastGroup(group, iface);
    }
    bool leaveMulticast(const std::string& group, const std::string& iface = "") {
        return socket_.leaveMulticastGroup(group, iface);
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
    // Polling API (queue enabled on the first hasNewMessage()/getNextMessage())
    // -------------------------------------------------------------------------
    // The queue holds up to getBufferSize() messages (default 1024). When a
    // new message arrives while it is full, the oldest one is dropped and
    // counted in getDroppedMessages(). Drops are also logged as a warning from
    // these two calls, at most once every 2 s, summed since the last report.

    // Check if there are unread messages (queue enabled on first call)
    bool hasNewMessage() {
        bufferEnabled_ = true;
        reportDrops();
        std::lock_guard<std::mutex> lock(queueMutex_);
        return !messageQueue_.empty();
    }

    // Get next message (removes from queue; queue enabled on first call)
    bool getNextMessage(OscMessage& msg) {
        bufferEnabled_ = true;
        reportDrops();
        std::lock_guard<std::mutex> lock(queueMutex_);
        if (messageQueue_.empty()) return false;
        msg = std::move(messageQueue_.front());
        messageQueue_.pop();
        return true;
    }

    // Set the queue limit (default 1024). Past it, the oldest message is
    // dropped. Shrinking the limit discards the oldest queued messages now.
    void setBufferSize(size_t size) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        bufferMax_ = size;
        // Trim queue if over limit
        while (messageQueue_.size() > bufferMax_) {
            messageQueue_.pop();
        }
    }

    size_t getBufferSize() const { return bufferMax_; }

    // Messages dropped from the full polling queue since this receiver was
    // created (a running total, never reset). Cheap to call from any thread.
    uint64_t getDroppedMessages() const {
        return droppedMessages_.load(std::memory_order_relaxed);
    }

private:
    // Receive thread: queue a message for polling, dropping the oldest while
    // the queue is over its limit. Drops are only counted here; the polling
    // calls log them on the caller's thread (reportDrops()).
    void enqueue(const OscMessage& msg) {
        if (!bufferEnabled_) return;
        uint64_t dropped = 0;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            messageQueue_.push(msg);
            while (messageQueue_.size() > bufferMax_) {
                messageQueue_.pop();
                ++dropped;
            }
        }
        if (dropped > 0) {
            droppedMessages_.fetch_add(dropped, std::memory_order_relaxed);
            unreportedDrops_.fetch_add(dropped, std::memory_order_relaxed);
        }
    }

    // Polling side: log the drops counted since the last report, at most
    // once every kDropReportInterval. Drops in between are summed into the
    // next line, so a queue that overflows every frame cannot flood the log.
    void reportDrops() {
        if (unreportedDrops_.load(std::memory_order_relaxed) == 0) return;
        const int64_t now = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        int64_t last = lastDropReportNs_.load(std::memory_order_relaxed);
        if (last != kNeverReported &&
            now - last < std::chrono::nanoseconds(kDropReportInterval).count()) return;
        // Only one polling thread reports a given interval
        if (!lastDropReportNs_.compare_exchange_strong(last, now, std::memory_order_relaxed)) return;
        const uint64_t n = unreportedDrops_.exchange(0, std::memory_order_relaxed);
        if (n == 0) return;
        size_t limit;
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            limit = bufferMax_;
        }
        tc::logWarning("tcxOsc") << "OscReceiver on port " << port_ << ": " << n
                                 << (n == 1 ? " message" : " messages")
                                 << " dropped since the last report (queue full at "
                                 << limit << ", oldest dropped first); raise setBufferSize()";
    }

    void handleReceive(tc::UdpReceiveEventArgs& args) {
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
                enqueue(msg);  // only if the polling queue is enabled
                // Always notify listeners
                onMessageReceived.notify(msg);
            }
            else {
                std::string err = "Failed to parse message";
                onParseError.notify(err);
            }
        }
    }

    // Recursively dispatch messages inside bundle. Walks the parsed tree in
    // place (no per-level copy); fromBytes() bounds the depth.
    void dispatchBundle(const OscBundle& bundle) {
        // Event<T>::notify() takes T&, and listeners could always edit the
        // bundle before its elements are dispatched. Every node belongs to
        // the non-const bundle parsePacket() owns, so the cast is well-defined.
        onBundleReceived.notify(const_cast<OscBundle&>(bundle));

        for (size_t i = 0; i < bundle.getElementCount(); ++i) {
            if (bundle.isMessage(i)) {
                OscMessage msg = bundle.getMessageAt(i);
                enqueue(msg);  // only if the polling queue is enabled
                onMessageReceived.notify(msg);
            }
            else if (const OscBundle* child = bundle.bundleAt(i)) {
                dispatchBundle(*child);
            }
        }
    }

    tc::UdpSocket socket_;
    int port_ = 0;
    tc::EventListener receiveListener_;
    tc::EventListener errorListener_;

    // Polling buffer
    std::queue<OscMessage> messageQueue_;
    std::mutex queueMutex_;
    std::atomic<bool> bufferEnabled_{false};
    size_t bufferMax_ = 1024;

    // Drop accounting: the receive thread only adds to the counters; the
    // polling calls read and log them.
    static constexpr std::chrono::seconds kDropReportInterval{2};
    static constexpr int64_t kNeverReported = INT64_MIN;
    std::atomic<uint64_t> droppedMessages_{0};   // running total
    std::atomic<uint64_t> unreportedDrops_{0};   // counted, not logged yet
    std::atomic<int64_t> lastDropReportNs_{kNeverReported};  // steady_clock
};

}  // namespace tcx::osc

// -----------------------------------------------------------------------------
// Backward compatibility: see tcxOscMessage.h. DEPRECATED — removed in v1.0.0.
// -----------------------------------------------------------------------------
namespace tcx    { using osc::OscReceiver; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::osc::OscReceiver; } // deprecated: remove at v1.0.0
