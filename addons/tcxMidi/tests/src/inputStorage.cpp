namespace trussc {}
namespace tc = trussc;
#include "tcxMidiIn.h"
#include <cstdio>
#include <thread>

namespace tcx::midi {
struct MidiInTestAccess {
    static auto& queue(MidiIn& input) { return *input.polling_; }
};
}
namespace {
using namespace tcx::midi;
void require(bool condition, const char* label) {
    if (!condition) { std::fprintf(stderr, "FAIL: %s\n", label); std::abort(); }
}
}
void testInputStorage() {
    tc::getMainThreadId();
    const auto mainThread = std::this_thread::get_id();
    MidiIn input;
    auto& queue = MidiInTestAccess::queue(input);
    require(input.getBufferSize() == 1024 && !input.hasNewMessage(), "default count, lazy empty queue");
    size_t warnings = 0;
    auto log = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level != tc::LogLevel::Warning || event.message.find("MIDI polling queue") == std::string::npos) return;
        ++warnings;
        require(std::this_thread::get_id() == mainThread, "queue warning runs on main thread");
        require(event.message.find("limit 67108864") != std::string::npos &&
                event.message.find("needed ") != std::string::npos && event.message.find("bytes") != std::string::npos,
                "warning names storage, limit, needed amount and units");
    });
    // Large metadata is retained in every queued copy, even for a short message.
    const std::string portName(2 * 1024 * 1024, 'p');
    require(input.openVirtualPort(portName), "open input with copied port metadata");
    auto callback = libremidi::test::inputCallback;
    MidiMessage example({0x90, 60, 100});
    example.portName = portName;
    queue.enqueue(example);
    const size_t charge = queue.retainedBytes();
    MidiMessage sample;
    require(input.getNextMessage(sample) && sample.portName == portName && sample.bytes == example.bytes &&
            input.getDroppedMessageCount() == 0, "measure retained copy for storage boundary");
    require(charge > portName.size(), "port-name allocation charged");
    const size_t keep = queue.storageLimit / charge;
    size_t delivered = 0;
    auto listener = input.onMessage.listen([&](MidiMessage&) { ++delivered; });
    std::thread producer([&] {
        for (size_t i = 0; i < keep + 5; ++i)
            callback(libremidi::message{{0x90, static_cast<unsigned char>(i), 100}, 0});
    });
    producer.join();
    tc::internal::drainMainThreadQueue(); // no polling call since the input began
    require(queue.messageCount() == keep && queue.retainedBytes() == keep * charge &&
            input.getDroppedMessageCount() == 5 && delivered == keep + 5 && warnings == 1,
            "exact storage eviction, events preserved, one warning without polling");
    input.closePort();
    require(input.openPort(0), "reopen preserves queue counter and first-loss gate");
    MidiMessage out;
    require(input.getNextMessage(out) && out.bytes[1] == 5 && out.portName == portName,
            "oldest whole messages evicted; metadata survives close/reopen");
    input.setBufferSize(0);
    require(queue.retainedBytes() == 0 && input.getDroppedMessageCount() == keep + 4,
            "shrink releases storage and counts exact remaining messages");
    libremidi::test::inputCallback(libremidi::message{{0xFA}, 0});
    require(queue.messageCount() == 0 && input.getDroppedMessageCount() == keep + 5, "zero retains zero messages");
    input.setBufferSize(1024);
    const libremidi::message blob{std::vector<unsigned char>(64000, 1), 0};
    callback = libremidi::test::inputCallback;
    for (int i = 0; i < 1024; ++i) callback(blob);
    require(queue.messageCount() == 1024 && input.getDroppedMessageCount() == keep + 5,
            "1024 64 KB messages fit including copied metadata and queue nodes");
    const libremidi::message huge{std::vector<unsigned char>(queue.storageLimit, 1), 0};
    callback(huge);
    require(queue.messageCount() == 1024 && input.getDroppedMessageCount() == keep + 6 && delivered == keep + 1031,
            "single over-budget message rejected whole; inline delivery independent");
    require(input.getNextMessage(out) && out.bytes == blob.bytes, "existing queue survives rejected single message");
    input.setBufferSize(0);
    input.closePort();
    tc::internal::drainMainThreadQueue();
    require(warnings == 1, "first-loss warning stays once over close/reopen and shrinking");
    std::puts("MIDI input storage tests passed");
}
