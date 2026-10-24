// Console-only tests of the real libremidi stream decoder and SysEx diagnostics.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiIn.h"
#include <libremidi/detail/midi_stream_decoder.hpp>
#if defined(LIBREMIDI_ALSA)
#include <libremidi/backends/alsa_seq/midi_in.hpp>
#endif
#include "allocation.h"
#include <cstdio>
#include <thread>

namespace {
int failures = 0;
int checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
constexpr size_t limit = 1048576;
using Bytes = std::vector<unsigned char>;
struct Decoder {
    libremidi::input_configuration config;
    std::vector<Bytes> received;
    std::vector<int64_t> timestamps;
    size_t losses = 0, needed = 0;
    libremidi::midi1::input_state_machine machine{config};
    bool multi;
    explicit Decoder(bool useMulti) : multi(useMulti) {
        config.ignore_sysex = false;
        config.ignore_timing = false;
        config.ignore_sensing = false;
        config.on_message = [&](libremidi::message&& msg) {
            received.emplace_back(msg.bytes.begin(), msg.bytes.end());
            timestamps.push_back(msg.timestamp);
        };
        config.on_sysex_overflow = [&](size_t amount) { ++losses; needed = amount; };
    }
    void feed(const Bytes& bytes, int64_t timestamp = 7) {
        if (multi) machine.on_bytes_multi(bytes, timestamp);
        else machine.on_bytes(bytes, timestamp);
        check(machine.message.size() <= limit, "logical assembly remains bounded");
        check(machine.message.bytes.capacity() <= limit, "assembly capacity remains bounded");
    }
};
Bytes sysex(size_t size, bool complete) {
    Bytes bytes(size, 1);
    bytes.front() = 0xF0;
    if (complete) bytes.back() = 0xF7;
    return bytes;
}
void decoderCases(bool multi) {
    for (bool fragmented : {false, true}) {
        Decoder d(multi);
        if (fragmented) {
            d.feed({1, 2, 3});
            d.feed({0xF7});
        } else {
            d.feed({1, 2, 3, 0xF7});
        }
        check(d.received.empty() && d.losses == 0,
              "orphan data and EOX deliver nothing and report no SysEx loss");
        d.feed({0x90, 60, 100, 0xF8, 0xF0, 1, 0xF7});
        check(d.received == std::vector<Bytes>{{0x90, 60, 100}, {0xF8}, {0xF0, 1, 0xF7}} &&
              d.losses == 0, "note clock and SysEx survive orphan tail");
    }
    {
        Decoder d(multi);
        d.feed({0xF7});
        check(d.received.empty() && d.losses == 0, "lone EOX is ignored without loss");
    }
    for (bool fragmented : {false, true}) {
        Decoder d(multi);
        if (fragmented) {
            d.feed({0x90, 60, 100});
            d.feed({0xF7});
            d.feed({0xF0, 1, 0xF7});
        } else {
            d.feed({0x90, 60, 100, 0xF7, 0xF0, 1, 0xF7});
        }
        check(d.received == std::vector<Bytes>{{0x90, 60, 100}, {0xF0, 1, 0xF7}} &&
              d.losses == 0, "stray EOX between valid messages adds no delivery or loss");
    }
    for (bool complete : {false, true}) {
        Decoder d(multi);
        auto bytes = sysex(limit + 1, complete);
        d.feed(bytes);
        check(d.losses == 1 && d.needed == limit + 1 && d.received.empty(),
              "initial assign rejects over limit before storage");
    }
    {
        Decoder d(multi);
        d.feed(sysex(limit, true));
        check(d.losses == 0 && d.received == std::vector<Bytes>{sysex(limit, true)},
              "exact limit complete message preserved");
    }
    {
        Decoder d(multi);
        d.feed(sysex(limit - 1, false), 21);
        d.feed({0xF7}, 22);
        check(d.received.size() == 1 && d.received[0].size() == limit &&
              d.timestamps[0] == 21 && d.losses == 0,
              "exact limit fragmented message and start timestamp preserved");
    }
    {
        Decoder d(multi);
        d.feed(sysex(limit - 1, false));
        d.feed({1, 0xF7});
        check(d.losses == 1 && d.needed == limit + 1 && d.received.empty(),
              "continuation rejects cumulative limit before insert");
    }
    {
        Decoder d(multi);
        d.feed(sysex(600 * 1024, false));
        d.feed(Bytes(300 * 1024, 1));
        check(d.machine.message.size() == 900 * 1024 && d.losses == 0,
              "valid growth retains all bytes without geometric capacity overshoot");
    }
    {
        Decoder d(multi);
        auto bytes = sysex(limit + 1, true);
        bytes.insert(bytes.begin() + bytes.size() / 2, 0xF8);
        d.feed(bytes);
        check(d.losses == 1 && d.received == std::vector<Bytes>{{0xF8}},
              "realtime before overflow survives while the whole SysEx is rejected");
        d.feed({0xF0, 1, 0xF7});
        check(d.received.back() == Bytes({0xF0, 1, 0xF7}), "valid SysEx follows interleaved overflow");
    }
    for (bool filtered : {false, true}) {
        Decoder d(multi);
        d.config.ignore_timing = filtered;
        d.config.ignore_sensing = filtered;
        d.feed(sysex(limit + 1, false));
        d.feed({1, 0xF8, 2, 0xFE, 3, 0xFA, 4});
        check(d.losses == 1, "discard counts once across interleaved realtime and data");
        d.feed({5, 0xF7, 0x90, 60, 100});
        std::vector<Bytes> expected;
        if (!filtered) expected = {{0xF8}, {0xFE}};
        expected.push_back({0xFA});
        expected.push_back({0x90, 60, 100});
        check(d.received == expected, "realtime filters and F7 mid-chunk resynchronization");
        d.feed(sysex(limit + 1, false));
        d.feed({1, 0x90, 61, 101});
        check(d.losses == 2 && !d.received.empty() && d.received.back() == Bytes({0x90, 61, 101}),
              "terminating channel status is reprocessed");
        d.feed(sysex(limit + 1, false));
        d.feed({1, 0xF0, 2, 0xF7, 0x90, 62, 102});
        check(d.losses == 3 && d.received.size() >= 2 && d.received[d.received.size()-2] == Bytes({0xF0, 2, 0xF7}) &&
              d.received.back() == Bytes({0x90, 62, 102}), "new F0 and following channel message survive");
    }
    for (bool f7 : {false, true}) {
        Decoder d(multi);
        // Raw/Android-size reads, including the read which first exceeds the cap.
        d.feed(sysex(1024, false), 11);
        for (size_t i = 0; i < limit / 1024; ++i) d.feed(Bytes(1024, 1), 12);
        check(d.losses == 1 && d.received.empty(), "chunked overflow counts once");
        d.feed(f7 ? Bytes{0xF7, 0x90, 60} : Bytes{0x90, 60}, 31);
        check(d.received.empty(), "recovery retains incomplete channel message");
        d.feed({0xF8, 100, 61}, 32);
        d.feed({101, 62, 102}, 33);
        check(d.received == std::vector<Bytes>{{0xF8}, {0x90, 60, 100},
              {0x90, 61, 101}, {0x90, 62, 102}},
              "split recovery and running status survive reads and realtime");
        check(d.timestamps.size() == 4 && d.timestamps[1] == 31 &&
              d.timestamps[2] == 32 && d.losses == 1,
              "recovered short messages retain start timestamp and loss count");
        d.feed({0xC0}, 34);
        d.feed({7, 8, 0xF1}, 35);
        d.feed({9, 10}, 36);
        check(d.received.size() == 7 && d.received[4] == Bytes({0xC0, 7}) &&
              d.received[5] == Bytes({0xC0, 8}) && d.received[6] == Bytes({0xF1, 9}),
              "two-byte running status and split system common cancel running status");
        d.feed({0x90, 60}, 37);
        d.machine.reset();
        d.feed({100});
        check(d.received.size() == 7, "reset clears partial channel and running status");
    }
    {
        Decoder d(multi);
        d.feed({0xF0, 1, 0xF8, 2, 0xFE, 3, 0xF7, 0x90, 60, 100});
        check(d.received == std::vector<Bytes>{{0xF8}, {0xFE}, {0xF0, 1, 2, 3, 0xF7},
              {0x90, 60, 100}}, "admitted SysEx separates realtime and preserves following message");
        d.config.ignore_sysex = true;
        d.feed(sysex(limit + 1, false));
        d.feed({0xF8, 1, 0xF7, 0x90, 60, 100});
        check(d.losses == 0 && !d.received.empty() && d.received.back() == Bytes({0x90, 60, 100}),
              "ignored SysEx allocates nothing and resynchronizes");
    }
    {
        Decoder d(multi);
        d.feed(sysex(limit + 1, false));
        d.machine.reset();
        d.feed({0x90, 60, 100});
        d.feed({});
        check(d.received == std::vector<Bytes>{{0x90, 60, 100}}, "reset clears discard; empty input safe");
    }
    {
        Decoder d(multi);
        Bytes bytes;
        for (int i = 0; i < 20000; ++i) bytes.insert(bytes.end(), {0xF0, 1, 0xF7});
        d.feed(bytes);
        check(d.received.size() == 20000, "successive SysEx in one span uses bounded stack");
    }
}

#if defined(LIBREMIDI_ALSA)
// process_event uses ALSA's real byte-event codec, which needs no device.
// An external context token bypasses client open/close; no queue or port is
// created and the token is never passed to an ALSA function.
struct AlsaInput : libremidi::alsa_seq::midi_in_impl<
    libremidi::input_configuration, libremidi::alsa_seq::input_configuration> {
    using midi_in_impl::midi_in_impl;
    stdx::error open_port(const libremidi::input_port&, std::string_view) override { return {}; }
};
void alsaScratch(bool filtered) {
    libremidi::input_configuration config;
    config.ignore_sysex = false;
    config.ignore_timing = filtered;
    config.timestamps = libremidi::timestamp_mode::NoTimestamp;
    size_t losses = 0, delivered = 0;
    config.on_sysex_overflow = [&](size_t) { ++losses; };
    config.on_message = [&](libremidi::message&&) { ++delivered; };
    libremidi::alsa_seq::input_configuration api;
    unsigned char contextToken{};
    api.context = reinterpret_cast<snd_seq_t*>(&contextToken);
    AlsaInput input(std::move(config), std::move(api));
    const size_t originalCapacity = input.decoding_buffer.capacity();
    auto bytes = sysex(limit + 1, true);
    snd_seq_event_t event{};
    event.type = SND_SEQ_EVENT_SYSEX;
    event.flags = SND_SEQ_EVENT_LENGTH_VARIABLE;
    event.data.ext.ptr = bytes.data();
    event.data.ext.len = static_cast<unsigned int>(bytes.size());
    check(input.process_event(event) == 0, "ALSA event processed without a device");
    check(input.decoding_buffer.capacity() == originalCapacity &&
          input.decoding_buffer.size() <= limit,
          "ALSA borrows SysEx without growing codec scratch");
    check(losses == 1 && delivered == 0, "ALSA over-limit event counted once");
    for (bool realtimePrefix : {false, true}) {
        if (!realtimePrefix) {
            // Data-led input continues an actual unfinished logical SysEx.
            Bytes start{0xF0};
            event.data.ext.ptr = start.data();
            event.data.ext.len = 1;
            input.process_event(event);
        }
        bytes = Bytes(limit + 1, 1);
        bytes.back() = 0xF7;
        if (realtimePrefix) bytes.insert(bytes.begin(), {0xF8, 0xF0});
        event.data.ext.ptr = bytes.data();
        event.data.ext.len = static_cast<unsigned int>(bytes.size());
        const auto before = losses;
        const auto priorDelivered = delivered;
        input.process_event(event);
        check(losses == before + 1 && delivered == priorDelivered + (realtimePrefix && !filtered ? 1 : 0),
              "ALSA rejects oversized logical message with either prefix and timing filter");
        check(input.m_processing.message.empty() &&
              input.m_processing.message.bytes.capacity() <= limit &&
              input.decoding_buffer.capacity() == originalCapacity,
              "ALSA overflow leaves bounded decoder and unchanged scratch storage");
    }
    // Successive oversized chunks of one unfinished SysEx still count once.
    bytes = sysex(limit + 1, false);
    event.data.ext.ptr = bytes.data();
    event.data.ext.len = static_cast<unsigned int>(bytes.size());
    input.process_event(event);
    bytes.front() = 1; // Continuation, rather than a new F0.
    input.process_event(event);
    check(losses == 4, "ALSA discarded SysEx counted once across oversized events");
    bytes = {0xF7};
    event.data.ext.ptr = bytes.data();
    event.data.ext.len = static_cast<unsigned int>(bytes.size());
    input.process_event(event);
    for (size_t size : {600 * 1024, 900 * 1024, 1048576}) {
        bytes = sysex(size, true);
        event.data.ext.ptr = bytes.data();
        event.data.ext.len = static_cast<unsigned int>(bytes.size());
        check(input.process_event(event) == 0 && input.decoding_buffer.capacity() == originalCapacity,
              "ALSA admitted messages leave scratch capacity unchanged");
    }
    check(delivered == (filtered ? 3u : 4u) && losses == 4, "ALSA admitted messages delivered whole after rejection");
}
void alsaLogicalMessages() {
    size_t warnings = 0;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning) ++warnings;
    });
    // Compare one borrowed event with different fragmentations of the same bytes.
    for (bool aggregate : {false, true}) {
        Bytes bytes;
        std::vector<Bytes> expected;
        if (aggregate) {
            // Exceed the old event threshold using only small complete messages.
            for (size_t i = 0; i < limit / 3 + 1; ++i) {
                Bytes msg{0xF0, static_cast<unsigned char>(i % 128), 0xF7};
                bytes.insert(bytes.end(), msg.begin(), msg.end());
                expected.push_back(std::move(msg));
            }
        } else {
            auto msg = sysex(limit, true);
            bytes = msg;
            bytes.insert(bytes.begin() + limit / 2, 0xF8);
            expected = {{0xF8}, std::move(msg)};
        }
        for (size_t chunk : {bytes.size(), size_t{1024}}) {
            auto loss = tcx::midi::internal::SysexLoss::create();
            libremidi::input_configuration config;
            config.ignore_sysex = config.ignore_timing = false;
            config.timestamps = libremidi::timestamp_mode::NoTimestamp;
            std::vector<Bytes> received;
            config.on_message = [&](libremidi::message&& msg) {
                received.emplace_back(msg.bytes.begin(), msg.bytes.end());
            };
            config.on_sysex_overflow = [loss](size_t needed) { loss->count(needed); };
            libremidi::alsa_seq::input_configuration api;
            unsigned char contextToken{};
            api.context = reinterpret_cast<snd_seq_t*>(&contextToken);
            AlsaInput input(std::move(config), std::move(api));
            const auto scratchSize = input.decoding_buffer.size();
            const auto scratchCapacity = input.decoding_buffer.capacity();
            snd_seq_event_t event{};
            event.type = SND_SEQ_EVENT_SYSEX;
            event.flags = SND_SEQ_EVENT_LENGTH_VARIABLE;
            bool processed = true, bounded = true;
            for (size_t offset = 0; offset < bytes.size(); offset += chunk) {
                event.data.ext.ptr = bytes.data() + offset;
                event.data.ext.len = static_cast<unsigned int>(std::min(chunk, bytes.size() - offset));
                processed &= input.process_event(event) == 0;
                bounded &= input.decoding_buffer.size() == scratchSize &&
                           input.decoding_buffer.capacity() == scratchCapacity &&
                           input.m_processing.message.size() <= limit &&
                           input.m_processing.message.bytes.capacity() <= limit;
            }
            check(processed && bounded, "ALSA event packaging preserves bounded storage");
            check(received == expected, aggregate ? "ALSA delivers every aggregated small SysEx" :
                  "ALSA delivers exact-limit SysEx and interleaved clock");
            check(loss->discarded == 0, "ALSA valid logical messages have zero losses");
            tc::internal::drainMainThreadQueue();
            check(warnings == 0, "ALSA valid logical messages emit no Warning");
        }
    }
}

#endif

void reporting() {
    tc::getMainThreadId();
    const auto mainThread = std::this_thread::get_id();
    int warnings = 0;
    bool safeThread = true, contents = true;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning && event.message.find("SysEx assembly") != std::string::npos) {
            ++warnings;
            safeThread &= std::this_thread::get_id() == mainThread;
            contents &= event.message.find("1048576") != std::string::npos &&
                        event.message.find("1048577") != std::string::npos &&
                        event.message.find("bytes") != std::string::npos;
        }
    });
    auto loss = tcx::midi::internal::SysexLoss::create();
    for (bool multi : {false, true}) {
        Decoder d(multi);
        d.config.on_sysex_overflow = [loss](size_t needed) { loss->count(needed); };
        d.feed({1, 2, 0xF7});
        d.feed({0xF7});
    }
    tc::internal::drainMainThreadQueue();
    check(loss->discarded == 0 && warnings == 0, "stray EOX queues no loss Warning");
#if !defined(__EMSCRIPTEN__)
    check(tc::internal::mainThreadQueue().size() == 0 &&
          tc::internal::countQueuedMainThreadSlots() == 0, "idle diagnostics queue no work");
#endif
    tc::internal::drainMainThreadQueue();
    tc::internal::drainMainThreadQueue();
#if !defined(__EMSCRIPTEN__)
    check(tc::internal::mainThreadQueue().size() == 0 &&
          tc::internal::countQueuedMainThreadSlots() == 0, "idle drains leave no diagnostic work");
#endif
    std::thread worker([&] {
        Decoder d(false);
        d.config.on_sysex_overflow = [loss](size_t needed) { loss->count(needed); };
        for (int i = 0; i < 8; ++i) d.feed(sysex(limit + 1, true));
    });
    worker.join();
    check(warnings == 0 && loss->discarded == 8, "input thread counts without logging or polling");
#if !defined(__EMSCRIPTEN__)
    check(tc::internal::mainThreadQueue().size() == 0 &&
          tc::internal::countQueuedMainThreadSlots() == 1, "first loss posts exactly one slot");
#endif
    tc::internal::drainMainThreadQueue();
    check(warnings == 1 && safeThread && contents, "one automatic main-thread warning with limit and need");
    std::thread reopened([&] { loss->count(limit + 2); });
    reopened.join();
    tc::internal::drainMainThreadQueue();
    check(warnings == 1 && loss->discarded == 9, "owner diagnostics survive a replacement decoder");
    auto allocationLoss = tcx::midi::internal::SysexLoss::create();
    size_t backendAllocations = 0;
    bool threw = false;
    std::thread allocationWorker([&] {
        allocation_test::measure = true;
        allocation_test::fail = true;
        try {
            allocationLoss->count(limit + 1);
            allocationLoss->count(limit + 2);
        } catch (const std::bad_alloc&) { threw = true; }
        allocation_test::fail = false;
        allocation_test::measure = false;
        backendAllocations = allocation_test::allocations;
    });
    allocationWorker.join();
    check(!threw && backendAllocations == 0 && allocationLoss->discarded == 2,
          "first and later backend loss paths neither allocate nor throw under allocation failure");
    tc::internal::drainMainThreadQueue();
    check(warnings == 2, "allocation failure on backend cannot consume the sole warning");
    auto other = tcx::midi::internal::SysexLoss::create();
    std::thread retired([&] { other->count(limit + 1); });
    retired.join();
    other.reset();
    std::weak_ptr<tcx::midi::internal::SysexLoss> lifetime;
    {
        auto pending = tcx::midi::internal::SysexLoss::create();
        lifetime = pending;
        std::thread worker([&] { pending->count(limit + 1); });
        worker.join();
    }
    check(!lifetime.expired(), "posted slot keeps pending diagnostics alive");
    tc::internal::drainMainThreadQueue();
    check(warnings == 4, "independent owners and deferred reports survive owner destruction");
    check(lifetime.expired(), "slot releases diagnostics after the report");
#if !defined(__EMSCRIPTEN__)
    { auto unused = tcx::midi::internal::SysexLoss::create(); }
    check(tc::internal::mainThreadQueue().size() == 0 &&
          tc::internal::countQueuedMainThreadSlots() == 0, "unused owner leaves no stale work");
#endif
}
}
int deviceCheck(int argc, char** argv);
void testWinmmCallback(void (*check)(bool, const char*));
int main(int argc, char** argv) {
    if (argc > 1) return deviceCheck(argc, argv);
    tc::setConsoleLogLevel(tc::LogLevel::Silent);
    decoderCases(false);
    decoderCases(true);
    reporting();
    testWinmmCallback(check);
#if defined(LIBREMIDI_ALSA)
    alsaScratch(false);
    alsaScratch(true);
    alsaLogicalMessages();
#endif
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
