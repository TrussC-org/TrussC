#pragma once

#include "tcxOscMessage.h"
#include "tcxOscBundle.h"
#include "tcxOscPreflight.h"
#include "tc/utils/tcPollingQueue.h"
#include "tc/network/tcUdpSocket.h"
#include "tc/network/tcNetworkState.h"
#include "tc/events/tcEvent.h"
#include "tc/events/tcEventListener.h"
#include "tc/utils/tcLog.h"
#include "tc/utils/tcOnceGate.h"
#include <mutex>
#include <atomic>
#include <cstdint>
#include <type_traits>

namespace tcx::osc {
namespace osc_internal {
struct PollingStorage {
    static constexpr const char* module = "tcxOsc";
    static constexpr const char* store = "OSC polling queue";
    static OscMessage copy(const OscMessage& msg) {
        OscMessage result;
        result.address_ = tc::internal::pollingStringCopy(msg.address_);
        result.typeTags_ = tc::internal::pollingStringCopy(msg.typeTags_);
        result.args_.reserve(msg.args_.size());
        for (const auto& arg : msg.args_) {
            auto value = std::visit([](const auto& stored) -> OscMessage::ArgVariant {
                using Value = std::decay_t<decltype(stored)>;
                if constexpr (std::is_same_v<Value, std::string>)
                    return tc::internal::pollingStringCopy(stored);
                else return stored;
            }, arg.value);
            result.args_.push_back({arg.index, std::move(value)});
        }
        return result;
    }
    static size_t copyStorage(const OscMessage& msg) {
        using namespace tc::internal;
        size_t bytes = storageAdd(copiedStringStorage(msg.address_), copiedStringStorage(msg.typeTags_));
        bytes = storageAdd(bytes, storageMultiply(msg.args_.size(), sizeof(OscMessage::StoredArg)));
        for (const auto& arg : msg.args_) {
            if (const auto* value = std::get_if<std::string>(&arg.value))
                bytes = storageAdd(bytes, copiedStringStorage(*value));
            else if (const auto* value = std::get_if<std::vector<uint8_t>>(&arg.value))
                bytes = storageAdd(bytes, value->size());
        }
        return bytes;
    }
    static size_t retainedStorage(const OscMessage& msg) {
        using namespace tc::internal;
        size_t bytes = storageAdd(retainedStringStorage(msg.address_), retainedStringStorage(msg.typeTags_));
        bytes = storageAdd(bytes, storageMultiply(msg.args_.capacity(), sizeof(OscMessage::StoredArg)));
        for (const auto& arg : msg.args_) {
            if (const auto* value = std::get_if<std::string>(&arg.value))
                bytes = storageAdd(bytes, retainedStringStorage(*value));
            else if (const auto* value = std::get_if<std::vector<uint8_t>>(&arg.value))
                bytes = storageAdd(bytes, value->capacity());
        }
        return bytes;
    }
};
}


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
    // Also reports preflight size/depth rejection and forwards socket
    // errors; setup failures notify on the calling thread.
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
        if (!prepareSocket(port) || !socket_.bind(port, true) || !socket_.isReceiving()) {
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
        if (!socket_.isReceiving()) {
            close();
            return false;
        }
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
    // The queue holds up to getBufferSize() messages (default 1024) and
    // 64 MiB of retained storage, including objects, capacities and nodes.
    // Overflow evicts oldest whole messages; a single over-budget message is
    // rejected. getDroppedMessageCount() counts both, including shrink losses.
    // The first queue loss warns automatically on the main thread, once over
    // this receiver's lifetime, including close/reopen, without another poll.

    bool hasNewMessage() { return state_->hasNewMessage(); }
    bool getNextMessage(OscMessage& msg) { return state_->getNextMessage(msg); }
    // Overflow and shrinking drop the oldest queued messages first.
    // Untrusted senders or a stalled reader can fill this queue; count and 64 MiB retained-storage caps keep memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
    void setBufferSize(size_t size) { state_->setBufferSize(size); }
    // Untrusted senders or a stalled reader can fill this queue; count and 64 MiB retained-storage caps keep memory finite. Overflow and shrinking drop the oldest messages and increment the drop count.
    size_t getBufferSize() const { return state_->getBufferSize(); }
    // Running total since construction, including drops caused by shrinking.
    uint64_t getDroppedMessageCount() const { return state_->getDroppedMessageCount(); }

private:
    friend struct OscReceiverTestAccess;
    struct State {
        // A listener may close and restart while its old packet is dispatching.
        // Keep the polling counters, but never continue that retired packet.
        std::atomic<uint64_t> generation_{0};
        std::atomic<int> port_{0};
        tc::internal::EventSnapshotDelivery<OscMessage> onMessageReceived;
        tc::internal::EventSnapshotDelivery<OscBundle> onBundleReceived;
        tc::internal::EventSnapshotDelivery<std::string> onParseError;
        using Queue = tc::internal::PollingQueue<OscMessage, osc_internal::PollingStorage>;
        Queue polling_;
        bool hasNewMessage() { return polling_.hasNewMessage(); }
        bool getNextMessage(OscMessage& msg) { return polling_.getNextMessage(msg); }
        void setBufferSize(size_t size) { polling_.setBufferSize(size); }
        size_t getBufferSize() const { return polling_.getBufferSize(); }
        uint64_t getDroppedMessageCount() const { return polling_.getDroppedMessageCount(); }
        void enqueue(const OscMessage& msg) { polling_.enqueue(msg); }

        void handleReceive(tc::UdpReceiveEventArgs& args, uint64_t generation) {
            if (args.data.empty() || generation != generation_) return;

            const uint8_t* data = reinterpret_cast<const uint8_t*>(args.data.data());
            size_t size = args.data.size();

            parsePacket(data, size, generation);
        }

        void parsePacket(const uint8_t* data, size_t size, uint64_t generation) {
            osc_internal::PacketPreflight preflight;
            if (!preflight.scan(data, size)) {
                ++rejectedDatagrams_;
                preflightWarning_.report("tcxOsc", "OSC datagram preflight",
                    {preflight.limit, preflight.needed, preflight.units});
                std::string err = "OSC datagram preflight rejected packet";
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
                    ++rejectedDatagrams_;
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
                    ++rejectedDatagrams_;
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

        // Dispatch the messages inside a bundle, depth first. Walks the parsed
        // tree in place (no per-level copy) with a heap stack, not recursion:
        // the depth of an untrusted packet must not decide call-stack use.
        void dispatchBundle(const OscBundle& bundle, uint64_t generation) {
            struct Level { const OscBundle* bundle; size_t next; };
            std::vector<Level> stack;
            // Event<T>::notify() takes T&, and listeners could always edit the
            // bundle before its elements are dispatched. Every node belongs to
            // the non-const bundle parsePacket() owns, so the cast is well-defined.
            onBundleReceived.notifyExternal(const_cast<OscBundle&>(bundle));
            stack.push_back({&bundle, 0});

            while (!stack.empty() && generation == generation_) {
                Level& level = stack.back();
                // Re-read the count each time: a listener may have edited the bundle.
                if (level.next >= level.bundle->getElementCount()) {
                    stack.pop_back();
                    continue;
                }
                const size_t i = level.next++;
                if (level.bundle->isMessage(i)) {
                    OscMessage msg = level.bundle->getMessageAt(i);
                    enqueue(msg);  // only if the polling queue is enabled
                    onMessageReceived.notifyExternal(msg);
                }
                else if (const OscBundle* child = level.bundle->bundleAt(i)) {
                    onBundleReceived.notifyExternal(const_cast<OscBundle&>(*child));
                    stack.push_back({child, 0});   // invalidates `level`
                }
            }
        }

        std::atomic<uint64_t> rejectedDatagrams_{0}; // datagrams, never queue messages
        tc::internal::FirstStoreLoss preflightWarning_;

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
