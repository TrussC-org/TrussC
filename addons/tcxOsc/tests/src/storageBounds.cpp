#include "../../../../core/tests/common/tcTestFailure.h"
#include <tcxOsc.h>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>

namespace {
thread_local bool rejectAllocations = false;
thread_local bool failReceiveAllocation = false;
}
// Allocation fault injection is confined to this headless test executable.
// The receive buffer is 65536 bytes; MSVC's std::allocator pads large blocks
// for alignment (+39 on x64), so match a small range above it.
void* operator new(size_t bytes) {
    if (rejectAllocations || (failReceiveAllocation && bytes >= 65536 && bytes < 65536 + 64)) {
        failReceiveAllocation = false;
        throw std::bad_alloc();
    }
    if (void* result = std::malloc(bytes ? bytes : 1)) return result;
    throw std::bad_alloc();
}
void operator delete(void* ptr) noexcept { std::free(ptr); }
void operator delete(void* ptr, size_t) noexcept { std::free(ptr); }
void* operator new[](size_t bytes) { return ::operator new(bytes); }
void operator delete[](void* ptr) noexcept { ::operator delete(ptr); }
void operator delete[](void* ptr, size_t) noexcept { ::operator delete(ptr); }

namespace tcx::osc {
struct OscReceiverTestAccess {
    static auto& socket(OscReceiver& receiver) { return receiver.socket_; }
    static auto& queue(OscReceiver& receiver) { return receiver.state_->polling_; }
    static void parse(OscReceiver& receiver, const std::vector<uint8_t>& bytes) {
        receiver.state_->parsePacket(bytes.data(), bytes.size(), receiver.state_->generation_);
    }
    static void receive(OscReceiver& receiver, const std::vector<uint8_t>& bytes) {
        tc::UdpReceiveEventArgs packet;
        packet.data.assign(bytes.begin(), bytes.end());
        receiver.state_->handleReceive(packet, receiver.state_->generation_);
    }
    static uint64_t rejected(OscReceiver& receiver) { return receiver.state_->rejectedDatagrams_; }
};
}
namespace trussc::internal {
struct UdpSocketTestAccess {
    static size_t bufferBytes(UdpSocket& socket) {
        return internal::sharedLoad(socket.state_)->receiveBuffer_.capacity();
    }
    static auto run(UdpSocket& socket) { return internal::sharedLoad(socket.state_); }
};
}
namespace {
using namespace tcx::osc;
using Access = OscReceiverTestAccess;
void require(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::abort(); }
}
OscMessage arguments(size_t count, char tag) {
    OscMessage message("/bounds");
    for (size_t i = 0; i < count; ++i) {
        if (tag == 's') message.addString("");
        else if (tag == 'b') { const uint8_t empty = 0; message.addBlob(&empty, 0); }
        else if (tag == 'D' && i < (count / 6) * 6) {
            switch (i % 6) {
                case 0: message.addBool(true); break;
                case 1: message.addBool(false); break;
                case 2: message.addNil(); break;
                case 3: message.addImpulse(); break;
                case 4: message.addArrayBegin(); break;
                case 5: message.addArrayEnd(); break;
            }
        } else message.addBool(true);
    }
    return message;
}
void preflight() {
    OscReceiver receiver;
    int messages = 0, bundles = 0, errors = 0, warnings = 0;
    const auto mainThread = std::this_thread::get_id();
    auto messageListener = receiver.onMessageReceived.listen([&](OscMessage&) { ++messages; });
    auto bundleListener = receiver.onBundleReceived.listen([&](OscBundle&) { ++bundles; });
    auto errorListener = receiver.onParseError.listen([&](std::string&) { ++errors; });
    auto log = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level != tc::LogLevel::Warning || event.message.find("OSC datagram preflight") == std::string::npos) return;
        ++warnings;
        require(std::this_thread::get_id() == mainThread, "preflight warning on main thread");
        require(event.message.find("limit 4096") != std::string::npos &&
                event.message.find("needed 4097 work units") != std::string::npos, "preflight warning details");
    });
    for (char tag : {'s', 'b', 'T', 'D'}) {
        for (size_t count : {4096u, 4097u}) {
            auto bytes = arguments(count, tag).toBytes();
            bool ok = false;
            auto parsed = OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
            require(ok == (count == 4096), "public message parser enforces work bound");
            require(!ok || parsed.getArgCount() == 4096, "all boundary arguments present");
            if (count == 4097 && !tcTest::skipPersistentAllocationFailure("OSC message result at work limit")) {
                bool allocated = false;
                rejectAllocations = true;
                try { OscMessage::fromBytes(bytes.data(), bytes.size(), ok); }
                catch (const std::bad_alloc&) { allocated = true; }
                rejectAllocations = false;
                require(!allocated && !ok, "over-limit message rejects before allocation");
            }
            const int before = messages;
            std::thread producer([&] { Access::parse(receiver, bytes); });
            producer.join();
            require(messages == before + (count == 4096), "message accepted/rejected whole via events");
        }
    }
    // Aggregate work includes both an inner-bundle element and its message.
    for (size_t count : {4094u, 4095u}) {
        OscBundle child, outer;
        child.addMessage(arguments(count, 'T'));
        outer.addBundle(child);
        auto bytes = outer.toBytes();
        const int beforeMessages = messages, beforeBundles = bundles;
        bool ok = false;
        OscBundle::fromBytes(bytes.data(), bytes.size(), ok);
        require(ok == (count == 4094), "public bundle parser shares aggregate work budget");
        if (count == 4095 && !tcTest::skipPersistentAllocationFailure("OSC bundle result at work limit")) {
            bool allocated = false;
            rejectAllocations = true;
            try { OscBundle::fromBytes(bytes.data(), bytes.size(), ok); }
            catch (const std::bad_alloc&) { allocated = true; }
            rejectAllocations = false;
            require(!allocated && !ok, "nested over-limit packet rejects before allocation");
        }
        Access::parse(receiver, bytes);
        require(messages == beforeMessages + (count == 4094) &&
                bundles == beforeBundles + (count == 4094 ? 2 : 0), "nested outer packet rejects without prefix delivery");
    }
    // Element count alone uses the same aggregate budget.
    for (size_t count : {4096u, 4097u}) {
        OscBundle outer;
        for (size_t i = 0; i < count; ++i) outer.addMessage(OscMessage("/"));
        const auto bytes = outer.toBytes();
        const int before = messages;
        Access::parse(receiver, bytes);
        require(messages == before + (count == 4096 ? 4096 : 0), "element boundary observed through events");
    }
    require(Access::rejected(receiver) == 6 && errors == 6 && receiver.getDroppedMessageCount() == 0,
            "datagram losses have separate exact counter");
    receiver.close();
    require(receiver.setup(0), "reopen receiver after datagram rejection");
    Access::parse(receiver, arguments(4097, 'T').toBytes());
    const int beforeNext = messages;
    Access::parse(receiver, OscMessage("/next").toBytes());
    tc::internal::drainMainThreadQueue();
    require(Access::rejected(receiver) == 7 && warnings == 1 && messages == beforeNext + 1,
            "next datagram works; counter and gate survive reopen without polling");
}
void packetSizeAndDepth() {
    OscReceiver receiver;
    int messages = 0, bundles = 0, warnings = 0;
    auto messageListener = receiver.onMessageReceived.listen([&](OscMessage&) { ++messages; });
    auto bundleListener = receiver.onBundleReceived.listen([&](OscBundle&) { ++bundles; });
    auto log = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level != tc::LogLevel::Warning || event.message.find("OSC datagram preflight") == std::string::npos) return;
        ++warnings;
        require(event.message.find("limit 65536, needed 65537 bytes") != std::string::npos, "packet byte warning details");
    });
    const std::vector<uint8_t> payload(65525, 7);
    OscMessage boundary("/");
    boundary.addBlob(payload.data(), 65524);
    auto bytes = boundary.toBytes();
    require(bytes.size() == 65536, "packet byte boundary fixture");
    bool ok = false;
    OscMessage::fromBytes(bytes.data(), bytes.size(), ok);
    require(ok, "public parser accepts packet byte boundary");
    Access::parse(receiver, bytes);
    boundary.clear();
    boundary.setAddress("/");
    boundary.addBlob(payload.data(), payload.size());
    bytes = boundary.toBytes();
    bytes.resize(65537); // complete blob, with its optional final padding absent
    if (!tcTest::skipPersistentAllocationFailure("OSC message result at packet limit")) {
        bool allocated = false;
        rejectAllocations = true;
        try { OscMessage::fromBytes(bytes.data(), bytes.size(), ok); }
        catch (const std::bad_alloc&) { allocated = true; }
        rejectAllocations = false;
        require(!ok && !allocated, "packet byte overflow rejects whole before allocation");
    }
    std::thread producer([&] { Access::parse(receiver, bytes); });
    producer.join();
    OscBundle nested;
    nested.addMessage(OscMessage("/depth"));
    for (int depth = 2; depth <= 17; ++depth) {
        OscBundle outer;
        outer.addMessage(OscMessage("/prefix"));
        outer.addBundle(nested);
        nested = std::move(outer);
    }
    bytes = nested.toBytes();
    if (!tcTest::skipPersistentAllocationFailure("OSC bundle result at depth limit")) {
        bool allocated = false;
        rejectAllocations = true;
        try { OscBundle::fromBytes(bytes.data(), bytes.size(), ok); }
        catch (const std::bad_alloc&) { allocated = true; }
        rejectAllocations = false;
        require(!ok && !allocated, "depth overflow rejects whole before allocation");
    }
    Access::parse(receiver, bytes);
    Access::parse(receiver, OscMessage("/next").toBytes());
    tc::internal::drainMainThreadQueue();
    require(messages == 2 && bundles == 0 && Access::rejected(receiver) == 2 && warnings == 1,
            "size/depth rejection is whole, counted once, recoverable and reported per store");
}
// Exercise allocation rounding even when this host's string copy constructor
// happens to allocate exactly size(). This models a different library policy.
struct RoundedStringStorage {
    static constexpr const char* module = "tcxOsc tests";
    static constexpr const char* store = "rounded-copy polling queue";
    static std::string copy(const std::string& value) {
        std::string result(value);
        result.reserve(!value.empty() && value.front() == '!' ? 64 * 1024 * 1024 : value.size() + 32);
        return result;
    }
    static size_t copyStorage(const std::string& value) {
        if (!value.empty() && value.front() == '?') throw std::bad_alloc();
        return tc::internal::copiedStringStorage(value);
    }
    static size_t retainedStorage(const std::string& value) {
        return tc::internal::retainedStringStorage(value);
    }
};
void roundedCopyAdmission() {
    tc::internal::PollingQueue<std::string, RoundedStringStorage> queue;
    queue.hasNewMessage();
    const std::string value(31, 'x');
    queue.enqueue(value);
    const size_t retained = queue.retainedBytes();
    require(queue.messageCount() == 1 && queue.getDroppedMessageCount() == 0 &&
            retained > queue.copyCharge(value), "rounded copy admitted above minimum estimate");
    queue.enqueue(std::string(31, '!'));
    require(queue.messageCount() == 1 && queue.getDroppedMessageCount() == 1 &&
            queue.retainedBytes() == retained, "actual over-budget capacity rejects whole without eviction");
    bool escaped = false;
    try { queue.enqueue(std::string(31, '?')); }
    catch (...) { escaped = true; }
    require(!escaped && queue.messageCount() == 1 && queue.getDroppedMessageCount() == 2 &&
            queue.retainedBytes() == retained, "measurement failure counts one drop without eviction");
    std::string out;
    require(queue.getNextMessage(out) && out == value &&
            retained == queue.copyCharge(std::string{}) + RoundedStringStorage::retainedStorage(out) &&
            queue.retainedBytes() == 0, "rounded copy charges actual storage and preserves previous message");
}
void pollingStrings() {
    // Parsed strings can cross the inline/heap boundary without following the
    // capacity growth of strings assembled with addString()/addBool().
    for (size_t length : {15u, 16u, 17u, 29u, 30u, 31u}) {
        for (int field = 0; field < 3; ++field) {
            OscMessage message("/strings");
            if (field == 0) message.setAddress("/" + std::string(length - 1, 'a'));
            else if (field == 1) message.addString(std::string(length, 's'));
            else for (size_t i = 0; i < length; ++i) message.addBool(true);
            const auto bytes = message.toBytes();
            OscReceiver receiver;
            auto& queue = Access::queue(receiver);
            require(!receiver.hasNewMessage() && queue.retainedBytes() == 0,
                    "string boundary starts with empty polling queue");
            size_t delivered = 0;
            auto listener = receiver.onMessageReceived.listen([&](OscMessage& parsed) {
                ++delivered;
                require(parsed.toBytes() == bytes, "parsed string boundary event is complete");
            });
            Access::receive(receiver, bytes);
            require(delivered == 1 && Access::rejected(receiver) == 0 &&
                    queue.messageCount() == 1 && receiver.getDroppedMessageCount() == 0,
                    "parsed address/string/type-tag boundary queues with zero drops");
            const size_t retained = queue.retainedBytes();
            OscMessage out;
            require(receiver.getNextMessage(out) && out.toBytes() == bytes,
                    "parsed address/string/type-tag boundary polls intact");
            const size_t nodeBytes = queue.copyCharge(OscMessage{});
            require(retained == nodeBytes + osc_internal::PollingStorage::retainedStorage(out),
                    "string boundary charged actual copied capacities");
            require(!receiver.hasNewMessage() && queue.retainedBytes() == 0 &&
                    receiver.getDroppedMessageCount() == 0,
                    "string boundary drain releases all storage with zero drops");
        }
    }
}
void polling() {
    OscReceiver receiver;
    auto& queue = Access::queue(receiver);
    receiver.hasNewMessage();
    size_t warnings = 0;
    auto log = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning && event.message.find("OSC polling queue") != std::string::npos) {
            ++warnings;
            require(event.message.find("67108864") != std::string::npos && event.message.find("bytes") != std::string::npos,
                    "storage warning includes byte limit and units");
        }
    });
    auto heavy = arguments(4096, 's');
    queue.enqueue(heavy);
    const size_t charge = queue.retainedBytes();
    OscMessage sample;
    require(receiver.getNextMessage(sample) && sample.toBytes() == heavy.toBytes() &&
            receiver.getDroppedMessageCount() == 0, "measure retained copy for storage boundary");
    require(charge > 4096 * sizeof(std::string), "empty strings charge argument objects");
    const size_t keep = queue.storageLimit / charge;
    const size_t pendingBefore = tc::internal::mainThreadQueue().size();
    std::thread producer([&] {
        for (size_t i = 0; i < keep + 7; ++i) {
            heavy.setAddress("/" + std::to_string(i));
            queue.enqueue(heavy);
        }
    });
    producer.join();
    require(tc::internal::mainThreadQueue().size() == pendingBefore + 1, "one bounded pending report despite repeated losses");
    tc::internal::drainMainThreadQueue();
    require(queue.messageCount() == keep && receiver.getDroppedMessageCount() == 7 &&
            queue.retainedBytes() == keep * charge && warnings == 1, "exact storage eviction, automatic warning without poll");
    OscMessage first;
    require(receiver.getNextMessage(first) && first.getAddress() == "/7" && first.getArgCount() == 4096,
            "storage pressure evicts oldest whole OSC messages");
    queue.enqueue(heavy); // restore the count without a loss
    receiver.close();
    require(receiver.setup(0) && receiver.getDroppedMessageCount() == 7 && queue.messageCount() == keep,
            "reopen preserves queued storage and counter");
    receiver.setBufferSize(0);
    require(receiver.getDroppedMessageCount() == keep + 7 && queue.retainedBytes() == 0, "shrink releases all storage and counts whole messages");
    queue.enqueue(heavy);
    require(receiver.getDroppedMessageCount() == keep + 8, "zero keeps zero");
    receiver.setBufferSize(1024);
    OscMessage blob("/blob");
    const std::vector<uint8_t> payload(64000, 5);
    blob.addBlob(payload.data(), payload.size());
    for (int i = 0; i < 1024; ++i) queue.enqueue(blob);
    require(queue.messageCount() == 1024 && receiver.getDroppedMessageCount() == keep + 8,
            "1024 64 KB blobs fit with object and container storage");
    OscMessage huge("/large");
    const std::vector<uint8_t> largePayload(queue.storageLimit, 1);
    huge.addBlob(largePayload.data(), largePayload.size());
    bool copied = false;
    rejectAllocations = true;
    try { queue.enqueue(huge); }
    catch (const std::bad_alloc&) { copied = true; }
    rejectAllocations = false;
    require(!copied, "over-budget message rejected before allocating a queue copy");
    require(queue.messageCount() == 1024 && receiver.getDroppedMessageCount() == keep + 9,
            "single over-budget copy rejected before evicting existing messages");
    copied = false;
    const size_t retainedBeforeFailure = queue.retainedBytes();
    rejectAllocations = true;
    try { queue.enqueue(blob); }
    catch (...) { copied = true; }
    rejectAllocations = false;
    require(!copied && queue.messageCount() == 1024 && queue.retainedBytes() == retainedBeforeFailure &&
            receiver.getDroppedMessageCount() == keep + 10,
            "polling measurement/copy exhaustion counts one drop and preserves queued messages");
    OscMessage out;
    require(receiver.getNextMessage(out) && out.getAddress() == "/blob" && out.getArgAsBlob(0).size() == 64000,
            "whole queued message remains readable");
    receiver.setBufferSize(0);
    tc::internal::drainMainThreadQueue();
    require(warnings == 1, "one queue warning across shrink and close");
}
void reportingLifetime() {
    size_t queueWarnings = 0, preflightWarnings = 0;
    std::string lastQueueWarning;
    auto log = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level != tc::LogLevel::Warning) return;
        if (event.message.find("OSC polling queue") != std::string::npos) {
            ++queueWarnings;
            lastQueueWarning = event.message;
        }
        if (event.message.find("OSC datagram preflight") != std::string::npos) ++preflightWarnings;
    });
    {
        OscReceiver receiver;
        receiver.hasNewMessage();
        receiver.setBufferSize(0);
        const auto valid = OscMessage("/ok").toBytes();
        const auto overWork = arguments(4097, 'T').toBytes();
        std::thread producer([&] {
            for (int i = 0; i < 20; ++i) {
                Access::parse(receiver, valid);
                Access::parse(receiver, overWork);
            }
        });
        producer.join();
        require(queueWarnings == 0 && preflightWarnings == 0, "worker only schedules bounded warning state");
        require(receiver.getDroppedMessageCount() == 20 && Access::rejected(receiver) == 20,
                "different stores keep separate units and totals");
    } // A pending warning must not reference a destroyed receiver.
    tc::internal::drainMainThreadQueue();
    require(queueWarnings == 1 && preflightWarnings == 1, "each store warns once, safely after owner destruction, without polling");
    OscReceiver shrink;
    shrink.hasNewMessage();
    for (int i = 0; i < 5; ++i) Access::parse(shrink, OscMessage("/shrink").toBytes());
    shrink.setBufferSize(2);
    shrink.close();
    require(shrink.getDroppedMessageCount() == 3 && queueWarnings == 2 &&
            lastQueueWarning.find("limit 2, needed 5 messages") != std::string::npos,
            "first shrink loss reports immediately and close adds no artificial loss");
    shrink.setBufferSize(0);
    require(shrink.getDroppedMessageCount() == 5 && queueWarnings == 2, "later shrink counts losses without a second warning");
}
void udpStorage() {
    using UdpAccess = tc::internal::UdpSocketTestAccess;
    tc::UdpSocket sender;
    require(UdpAccess::bufferBytes(sender) == 0, "new socket allocates no receive storage");
    require(sender.create() && sender.connect("127.0.0.1", 19999), "send-only socket setup");
    require(UdpAccess::bufferBytes(sender) == 0, "send-only socket allocates no receive buffer");
    for (bool worker : {false, true}) {
        tc::UdpSocket binding;
        binding.setUseThread(worker);
        failReceiveAllocation = true;
        const bool bound = binding.bind(0);
        require(!failReceiveAllocation && !bound && !binding.isReceiving() &&
                UdpAccess::bufferBytes(binding) == 0,
                "bind reports receive allocation failure in each mode");
        binding.close();

        for (bool multicast : {false, true}) {
            OscReceiver osc;
            auto& socket = Access::socket(osc);
            socket.setUseThread(worker);
            int errors = 0;
            auto error = osc.onParseError.listen([&](std::string&) { ++errors; });
            const auto setup = [&] {
                return multicast ? osc.setupMulticast(0, "239.77.0.1", "127.0.0.1") : osc.setup(0);
            };
            failReceiveAllocation = true;
            const bool listening = setup();
            require(!failReceiveAllocation && !listening && !osc.isListening() &&
                    !socket.isValid() && osc.getPort() == 0 &&
                    UdpAccess::bufferBytes(socket) == 0 && errors == 1,
                    "OSC setup reports allocation failure, forwards error and closes in each mode");
            require(setup() && osc.isListening() && socket.isValid() &&
                    UdpAccess::bufferBytes(socket) == 65536 && errors == 1,
                    "OSC setup retries successfully after receive allocation failure");
            osc.close();
        }

        tc::UdpSocket receiver;
        receiver.setUseThread(worker);
        require(receiver.bind(0, false) && UdpAccess::bufferBytes(receiver) == 0, "bound stopped socket has no receive buffer");
        failReceiveAllocation = true;
        receiver.startReceiving();
        require(!failReceiveAllocation && !receiver.isReceiving() && UdpAccess::bufferBytes(receiver) == 0,
                "allocation failure leaves receiving false and no receive buffer");
        receiver.startReceiving();
        require(receiver.isReceiving() && UdpAccess::bufferBytes(receiver) == 65536, "each receive mode allocates before publishing");
        auto oldRun = UdpAccess::run(receiver);
        const auto* oldBuffer = oldRun->receiveBuffer_.data();
        receiver.stopReceiving();
        require(UdpAccess::bufferBytes(receiver) == 0, "stopped replacement state starts without receive storage");
        receiver.startReceiving();
        require(receiver.isReceiving() && UdpAccess::bufferBytes(receiver) == 65536 &&
                oldRun->receiveBuffer_.data() == oldBuffer, "restart never resizes old run's buffer");
        receiver.close();
    }
}
}
void testStorageBounds() {
    preflight();
    packetSizeAndDepth();
    roundedCopyAdmission();
    pollingStrings();
    polling();
    reportingLifetime();
    udpStorage();
    std::puts("OSC storage/preflight tests passed");
}
