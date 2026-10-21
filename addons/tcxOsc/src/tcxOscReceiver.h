#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/network/tcNetworkState.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcLog.h"
#include "tc/utils/tcOnceGate.h"
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
    friend struct OscDeliveryTestAccess;
public:
    // Events
    tc::Event<OscMessage> onMessageReceived;   // Message received
    // Bundle received. The OscBundle& is the parsed bundle itself (not a
    // copy) and is valid only during that call. A listener may edit that
    // bundle, but must not keep a reference to an enclosing bundle (from an
    // earlier call for the same packet) and modify it (e.g. addMessage() /
    // clear()) while a nested bundle of that packet is being dispatched.
    tc::Event<OscBundle> onBundleReceived;
    // Parse error: a packet that is not a valid OSC message or bundle (for
    // example an unknown type tag, data cut short, or a damaged message in a
    // bundle). The packet is not delivered, not even in part.
    // Also forwards socket errors; setup failures notify on the calling thread.
    tc::Event<std::string> onParseError;

    OscReceiver() {
        messageTarget_ = tc::internal::EventSnapshotDelivery<OscMessage>::bind(onMessageReceived, delivery_);
        bundleTarget_ = tc::internal::EventSnapshotDelivery<OscBundle>::bind(onBundleReceived, delivery_);
        errorTarget_ = tc::internal::EventSnapshotDelivery<std::string>::bind(onParseError, delivery_);
        state_->onMessageReceived.target = messageTarget_;
        state_->onBundleReceived.target = bundleTarget_;
        state_->onParseError.target = errorTarget_;
    }
    ~OscReceiver() { delivery_->close(); close(); }

    // Non-copyable
    OscReceiver(const OscReceiver&) = delete;
    OscReceiver& operator=(const OscReceiver&) = delete;

    // -------------------------------------------------------------------------
    // Setup
    // -------------------------------------------------------------------------

    // Bind exclusively and start receiving. A port in use returns false and
    // reports through onParseError and the socket log.
    bool setup(int port) {
        if (!prepareSocket(port) || !socket_.bind(port, true)) {
            close();
            return false;
        }
        return true;
    }

    // Share a multicast port: enable reuse, bind, join, then start receiving.
    // iface selects the NIC (""/"0.0.0.0" = default route). Any failed step
    // reports through onParseError and the socket log, closes and returns false.
    bool setupMulticast(int port, const std::string& group, const std::string& iface = "") {
        if (!prepareSocket(port) || !socket_.setReuseAddress(true) ||
            !socket_.setReusePort(true) || !socket_.bind(port, false) ||
            !socket_.joinMulticastGroup(group, iface)) {
            close();
            return false;
        }
        socket_.startReceiving();
        return true;
    }

    // -------------------------------------------------------------------------
    // Multicast (IPv4)
    // -------------------------------------------------------------------------
    // OSC is usually unicast, but some setups multicast it (one sender, many
    // listeners on a fixed group). Call AFTER setup() or setupMulticast(),
    // then the receiver also gets datagrams for `group` (e.g. "239.0.0.1").
    // Joining after setup() keeps the port exclusive; use setupMulticast() to share.
    // `iface` selects the NIC (""/"0.0.0.0" = default route).

    bool joinMulticast(const std::string& group, const std::string& iface = "") {
        return socket_.joinMulticastGroup(group, iface);
    }
    bool leaveMulticast(const std::string& group, const std::string& iface = "") {
        return socket_.leaveMulticastGroup(group, iface);
    }

    // Close
    void close() {
        ++state_->generation_;
        state_->port_ = 0;
        socket_.close();
        receiveListener_.disconnect();
        errorListener_.disconnect();
    }

    // -------------------------------------------------------------------------
    // Info
    // -------------------------------------------------------------------------

    int getPort() const { return state_->port_; }
    bool isListening() const { return socket_.isReceiving(); }

    // -------------------------------------------------------------------------
    // Polling API (queue enabled on the first hasNewMessage()/getNextMessage())
    // -------------------------------------------------------------------------
    // The queue holds up to getBufferSize() messages (default 1024). When a
    // new message arrives while it is full, the oldest one is dropped and
    // counted in getDroppedMessageCount(). Drops are also logged as a warning from
    // these two calls, at most once every 2 s, summed since the last report.

    bool hasNewMessage() { return state_->hasNewMessage(); }
    bool getNextMessage(OscMessage& msg) { return state_->getNextMessage(msg); }
    // Overflow and shrinking drop the oldest queued messages first.
    // Untrusted senders or a stalled reader can fill this queue; a fixed cap keeps memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
    void setBufferSize(size_t size) { state_->setBufferSize(size); }
    // Untrusted senders or a stalled reader can fill this queue; a fixed cap keeps memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
    size_t getBufferSize() const { return state_->getBufferSize(); }
    // Running total since construction, including drops caused by shrinking.
    uint64_t getDroppedMessageCount() const { return state_->getDroppedMessageCount(); }

private:
    struct State {
        // A listener may close and restart while its old packet is dispatching.
        // Keep the polling counters, but never continue that retired packet.
        std::atomic<uint64_t> generation_{0};
        std::atomic<int> port_{0};
        tc::internal::EventSnapshotDelivery<OscMessage> onMessageReceived;
        tc::internal::EventSnapshotDelivery<OscBundle> onBundleReceived;
        tc::internal::EventSnapshotDelivery<std::string> onParseError;
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
        // dropped. Shrinking the limit discards the oldest queued messages now;
        // they count as dropped too.
        // Untrusted senders or a stalled reader can fill this queue; a fixed cap keeps memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
        void setBufferSize(size_t size) {
            uint64_t dropped = 0;
            {
                std::lock_guard<std::mutex> lock(queueMutex_);
                bufferMax_ = size;
                // Trim queue if over limit
                while (messageQueue_.size() > bufferMax_) {
                    messageQueue_.pop();
                    ++dropped;
                }
            }
            countDrops(dropped);
        }

        // Untrusted senders or a stalled reader can fill this queue; a fixed cap keeps memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
        size_t getBufferSize() const { return bufferMax_; }

        // Messages received but never handed to the app, since this receiver was
        // created: dropped from the full polling queue, or discarded when
        // setBufferSize() shrank it (a running total, never reset). Cheap to
        // call from any thread.
        uint64_t getDroppedMessageCount() const {
            return droppedMessages_.load(std::memory_order_relaxed);
        }
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
            countDrops(dropped);
        }

        // Any thread: add drops to the total and to the next report. Only
        // touches atomics (no lock, no log), so the receive thread can call it.
        void countDrops(uint64_t dropped) {
            if (dropped == 0) return;
            droppedMessages_.fetch_add(dropped, std::memory_order_relaxed);
            unreportedDrops_.fetch_add(dropped, std::memory_order_relaxed);
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
                                     << " dropped since the last report (queue limit "
                                     << limit << ", oldest dropped first); raise setBufferSize()";
        }

        void handleReceive(tc::UdpReceiveEventArgs& args, uint64_t generation) {
            if (args.data.empty() || generation != generation_) return;

            const uint8_t* data = reinterpret_cast<const uint8_t*>(args.data.data());
            size_t size = args.data.size();

            parsePacket(data, size, generation);
        }

        void parsePacket(const uint8_t* data, size_t size, uint64_t generation) {
            if (size < 4) {
                std::string err = "Packet too small";
                onParseError.notifyExternal(err);
                return;
            }

            // Determine if bundle or message
            if (OscBundle::isBundle(data, size)) {
                bool ok = false;
                bool paddingMissing = false;
                OscBundle bundle = OscBundle::fromBytes(data, size, ok, paddingMissing);
                if (ok) {
                    if (paddingMissing) warnPaddingMissing();
                    // Dispatch messages inside bundle individually
                    dispatchBundle(bundle, generation);
                }
                else {
                    std::string err = "Failed to parse bundle";
                    onParseError.notifyExternal(err);
                }
            }
            else {
                bool ok = false;
                bool paddingMissing = false;
                OscMessage msg = OscMessage::fromBytes(data, size, ok, paddingMissing);
                if (ok) {
                    if (paddingMissing) warnPaddingMissing();
                    enqueue(msg);  // only if the polling queue is enabled
                    // Always notify listeners
                    onMessageReceived.notifyExternal(msg);
                }
                else {
                    std::string err = "Failed to parse message";
                    onParseError.notifyExternal(err);
                }
            }
        }

        // A message that ends before its zero padding to 4 bytes is accepted;
        // this logs it once per receiver.
        void warnPaddingMissing() {
            if (!paddingWarned_.isFirstTime()) return;
            tc::logWarning("tcxOsc") << "OscReceiver on port " << port_
                                     << ": received a message that ends before its zero padding "
                                        "to 4 bytes; accepted (logged once per receiver)";
        }

        // Recursively dispatch messages inside bundle. Walks the parsed tree in
        // place (no per-level copy); fromBytes() bounds the depth.
        void dispatchBundle(const OscBundle& bundle, uint64_t generation) {
            // Event<T>::notify() takes T&, and listeners could always edit the
            // bundle before its elements are dispatched. Every node belongs to
            // the non-const bundle parsePacket() owns, so the cast is well-defined.
            onBundleReceived.notifyExternal(const_cast<OscBundle&>(bundle));

            for (size_t i = 0; i < bundle.getElementCount() && generation == generation_; ++i) {
                if (bundle.isMessage(i)) {
                    OscMessage msg = bundle.getMessageAt(i);
                    enqueue(msg);  // only if the polling queue is enabled
                    onMessageReceived.notifyExternal(msg);
                }
                else if (const OscBundle* child = bundle.bundleAt(i)) {
                    dispatchBundle(*child, generation);
                }
            }
        }

        // Polling buffer
        std::queue<OscMessage> messageQueue_;
        std::mutex queueMutex_;
        std::atomic<bool> bufferEnabled_{false};
        // Bound (security): untrusted senders or a stalled reader can fill this queue; a fixed cap keeps memory finite. Overflow drops the oldest and is counted.
        size_t bufferMax_ = 1024;

        // Drop accounting: the receive thread and setBufferSize() only add to
        // the counters (countDrops()); the polling calls read and log them.
        static constexpr std::chrono::seconds kDropReportInterval{2};
        static constexpr int64_t kNeverReported = INT64_MIN;
        std::atomic<uint64_t> droppedMessages_{0};   // running total
        std::atomic<uint64_t> unreportedDrops_{0};   // counted, not logged yet
        std::atomic<int64_t> lastDropReportNs_{kNeverReported};  // steady_clock

        tc::OnceGate paddingWarned_;  // warnPaddingMissing()
    };
    bool prepareSocket(int port) {
        close();
        auto state = state_;
        state->port_ = port;
        const auto generation = state->generation_.load();
        receiveListener_ = socket_.onReceive.listen([state, generation](tc::UdpReceiveEventArgs& args) {
            state->handleReceive(args, generation);
        });
        errorListener_ = socket_.onError.listen([state, generation](tc::UdpErrorEventArgs& args) {
            if (generation != state->generation_) return;
            std::string msg = "Socket error: " + args.message;
            if (tc::internal::ExternalEventScope::isExternal(&args)) state->onParseError.notifyExternal(msg);
            else state->onParseError.notify(msg);
        });
        return socket_.create();
    }

    std::shared_ptr<State> state_ = std::make_shared<State>();
    std::shared_ptr<tc::internal::NetworkDelivery> delivery_ = std::make_shared<tc::internal::NetworkDelivery>();
    std::shared_ptr<tc::internal::EventSnapshotDelivery<OscMessage>::Target> messageTarget_;
    std::shared_ptr<tc::internal::EventSnapshotDelivery<OscBundle>::Target> bundleTarget_;
    std::shared_ptr<tc::internal::EventSnapshotDelivery<std::string>::Target> errorTarget_;
    tc::UdpSocket socket_;
    tc::EventListener receiveListener_;
    tc::EventListener errorListener_;

};

}  // namespace tcx::osc

// -----------------------------------------------------------------------------
// Backward compatibility: see tcxOscMessage.h. DEPRECATED — removed in v1.0.0.
// -----------------------------------------------------------------------------
namespace tcx    { using osc::OscReceiver; } // deprecated: remove at v1.0.0
namespace trussc { using tcx::osc::OscReceiver; } // deprecated: remove at v1.0.0
