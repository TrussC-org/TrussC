// Optional native-port check. No port is opened by the default unit-test run.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiIn.h"
#include "tcxMidiOut.h"
#include <condition_variable>
#include <cstdio>
#include <thread>

int deviceCheck(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "Usage: tests-sysex --receive-virtual NAME | --receive NAME | --send NAME\n");
        return 2;
    }
    tc::getMainThreadId();
    const std::string mode = argv[1], port = argv[2];
    if (mode == "--send") {
        tcx::midi::MidiOut output;
        if (!output.openPort(port)) return 1;
        std::vector<unsigned char> bytes(1048577, 1);
        bytes.front() = 0xF0;
        bytes.back() = 0xF7;
        if (!output.sendSysex(bytes)) return 1;
        bytes.insert(bytes.begin() + bytes.size() / 2, 0xF8);
        if (!output.sendSysex(bytes) || !output.sendSysex({0xF0, 1, 0xF7}) ||
            !output.sendNoteOn(1, 60, 100)) return 1;
        std::puts("Sent two over-limit SysEx messages (one with F8), a valid SysEx and a note.");
        return 0;
    }
    if (mode != "--receive" && mode != "--receive-virtual") return 2;
    std::mutex mutex;
    std::condition_variable changed;
    // Any delivery other than the expected clock, small SysEx and note (for
    // example part of a rejected dump) counts as unexpected and fails the check.
    int oversized = 0, clocks = 0, validSysex = 0, notes = 0, unexpected = 0, warnings = 0;
    auto logs = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning &&
            event.message.find("SysEx assembly") != std::string::npos) ++warnings;
    });
    tcx::midi::MidiIn input;
    input.ignoreTypes(false, false, false);
    auto listener = input.onMessage.listen([&](tcx::midi::MidiMessage& message) {
        std::lock_guard<std::mutex> lock(mutex);
        if (message.bytes.size() > 1048576) ++oversized;
        else if (message.bytes == std::vector<unsigned char>{0xF8}) ++clocks;
        else if (message.bytes == std::vector<unsigned char>{0xF0, 1, 0xF7}) ++validSysex;
        else if (message.bytes == std::vector<unsigned char>{0x90, 60, 100}) ++notes;
        else ++unexpected;
        changed.notify_one();
    });
    if (!(mode == "--receive-virtual" ? input.openVirtualPort(port) : input.openPort(port))) return 1;
    std::puts("Ready; start the sender in another terminal (60 second timeout).");
    std::fflush(stdout);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(60);
    bool complete = false;
    while (std::chrono::steady_clock::now() < deadline) {
        tc::internal::drainMainThreadQueue();
        std::unique_lock<std::mutex> lock(mutex);
        if (notes > 0) { complete = true; break; }
        changed.wait_for(lock, std::chrono::milliseconds(10));
    }
    input.closePort();
    tc::internal::drainMainThreadQueue();
    std::printf("oversize=%d clocks=%d valid_sysex=%d notes=%d unexpected=%d warnings=%d\n",
                oversized, clocks, validSysex, notes, unexpected, warnings);
    return complete && oversized == 0 && unexpected == 0 && clocks == 1 && validSysex == 1 && notes == 1 && warnings == 1 ? 0 : 1;
}
