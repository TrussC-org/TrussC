// Native backend integration test; deliberately does not use tests/mocks/.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiIn.h"
#include "tcxMidiOut.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <thread>

#if defined(LIBREMIDI_ALSA)
#include <alsa/asoundlib.h>
#endif
#if defined(__APPLE__)
#include <CoreFoundation/CoreFoundation.h>
#endif

namespace {
using namespace tcx::midi;

bool check(bool ok, const char* label) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", label);
    return ok;
}

template <typename Ports>
bool contains(const Ports& ports, const std::string& name) {
    return std::any_of(ports.begin(), ports.end(), [&](const auto& port) {
        return port.display_name.find(name) != std::string::npos;
    });
}

bool listed(bool input, const std::string& name) {
    const auto ports = input ? MidiIn::listDevices() : MidiOut::listDevices();
    for (const auto& port : ports) {
        std::printf("%s %d %s\n", input ? "INPUT" : "OUTPUT",
                    port.getPortNumber(), port.getName().c_str());
    }
    return std::any_of(ports.begin(), ports.end(), [&](const auto& port) {
        return port.getName().find(name) != std::string::npos;
    });
}

bool receiveNote(MidiIn& input) {
    // Bound (test hang): a missing note fails after 5 s, even while other messages keep arriving; elapsed time is not asserted.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        MidiMessage message;
        while (input.getNextMessage(message)) {
            if (message.bytes == std::vector<unsigned char>{0x90, 60, 100}) return true;
            if (std::chrono::steady_clock::now() >= deadline) return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (std::chrono::steady_clock::now() < deadline);
    return false;
}

bool loopback(const std::string& name) {
    if (!check(listed(true, name), "external loopback input listed") ||
        !check(listed(false, name), "external loopback output listed")) return false;
    MidiIn input;
    MidiOut output;
    input.hasNewMessage(); // Enable polling before sending.
    if (!check(input.openPort(name), "external input opened by name") ||
        !check(output.openPort(name), "external output opened by name")) return false;
    return check(output.sendNoteOn(1, 60, 100), "external loopback note sent") &&
           check(receiveNote(input), "external loopback received 90 3c 64");
}

bool virtualPorts() {
    const std::string suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string sourceName = "tc982-src-" + suffix;
    const std::string sinkName = "tc982-dst-" + suffix;
    // Each object creates a separate backend client, like another application.
    MidiOut source;
    MidiIn sink;
    sink.hasNewMessage();
    if (!check(source.openVirtualPort(sourceName), "second client creates virtual source") ||
        !check(sink.openVirtualPort(sinkName), "second client creates virtual destination")) return false;

    // Negative control: these very same OS ports must be hidden by the old
    // configuration. This makes the regression meaningful on ALSA/CoreMIDI.
    libremidi::observer hardwareOnly{{}, libremidi::observer_configuration_for(platformMidiApi())};
    if (!check(!contains(hardwareOnly.get_input_ports(), sourceName) &&
               !contains(hardwareOnly.get_output_ports(), sinkName),
               "default observer hides both virtual ports (negative control)")) return false;
    if (!check(listed(true, sourceName), "virtual source listed") ||
        !check(listed(false, sinkName), "virtual destination listed")) return false;

    MidiIn input;
    MidiOut output;
    input.hasNewMessage();
    if (!check(input.openPort(sourceName), "virtual source opened by name") ||
        !check(output.openPort(sinkName), "virtual destination opened by name")) return false;
    if (!check(source.sendNoteOn(1, 60, 100), "virtual source note sent") ||
        !check(receiveNote(input), "named input received 90 3c 64")) return false;
    return check(output.sendNoteOn(1, 60, 100), "named output note sent") &&
           check(receiveNote(sink), "virtual destination received 90 3c 64");
}

// One host turn: CoreMIDI delivers port removals on the run loop of the thread
// that created the observer (the main thread; NSApp runs it in a real app),
// then the main-thread queue runs (the Runtime does this once per host turn).
void hostTurn(int ms) {
#if defined(__APPLE__)
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, ms / 1000.0, true);
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
#endif
    tc::internal::drainMainThreadQueue();
}

// Run host turns until `done()` or five seconds pass. The 5 s bound only ends a hang.
template <class Done>
bool pumpUntil(Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!done()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        hostTurn(5);
    }
    return true;
}

// Device lifecycle (#415) on a real backend: another client removes the port we
// have open, which is what an unplug looks like to ALSA / CoreMIDI.
bool lifecycle() {
    tc::getMainThreadId();
    const std::string suffix = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const std::string sourceName = "tc415-src-" + suffix;
    const std::string sinkName = "tc415-dst-" + suffix;
    const std::string otherName = "tc415-other-" + suffix;

    MidiOut peerSource;   // another client's virtual source, which our input opens
    MidiIn peerSink;      // another client's virtual destination, which our output opens
    MidiOut other;        // a third port: removing it must not matter
    peerSink.hasNewMessage();
    if (!check(peerSource.openVirtualPort(sourceName) && peerSink.openVirtualPort(sinkName) &&
               other.openVirtualPort(otherName), "lifecycle: peers created")) return false;

    MidiIn input;
    MidiOut output;
    input.hasNewMessage();
    int inputLost = 0, outputLost = 0;
    bool inputClosedWhenFired = false;
    auto inputListener = input.onDisconnect.listen([&](tc::DeviceDisconnectEventArgs&) {
        ++inputLost;
        inputClosedWhenFired = !input.isConnected();
    });
    auto outputListener = output.onDisconnect.listen([&](tc::DeviceDisconnectEventArgs&) { ++outputLost; });
    if (!check(input.openPort(sourceName) && input.isConnected(), "lifecycle: input opened") ||
        !check(output.openPort(sinkName) && output.isConnected(), "lifecycle: output opened")) return false;

    // An unrelated port going away is not our loss.
    other.close();
    const auto quiet = std::chrono::steady_clock::now() + std::chrono::milliseconds(600);
    while (std::chrono::steady_clock::now() < quiet) hostTurn(10);
    if (!check(input.isConnected() && output.isConnected() && inputLost == 0 && outputLost == 0,
               "lifecycle: removing another port keeps both open")) return false;

    // The peers go away.
    peerSource.close();
    peerSink.close();
    if (!check(pumpUntil([&] { return inputLost > 0 && outputLost > 0; }), "lifecycle: both losses delivered")) return false;
    if (!check(inputLost == 1 && outputLost == 1 && !input.isConnected() && !output.isConnected() &&
               inputClosedWhenFired, "lifecycle: each fired once, with the port closed")) return false;
    hostTurn(100);
    if (!check(inputLost == 1 && outputLost == 1, "lifecycle: no repeat")) return false;
    if (!check(!input.setup() && !output.setup(), "lifecycle: setup() fails while the port is gone")) return false;

    // They come back: setup() finds the same names again.
    if (!check(peerSource.openVirtualPort(sourceName) && peerSink.openVirtualPort(sinkName), "lifecycle: peers back")) return false;
    if (!check(input.setup() && output.setup() && input.isConnected() && output.isConnected(),
               "lifecycle: setup() reconnects both by name")) return false;
    if (!check(output.sendNoteOn(1, 60, 100) && receiveNote(peerSink), "lifecycle: reconnected output delivers") ||
        !check(peerSource.sendNoteOn(1, 60, 100) && receiveNote(input), "lifecycle: reconnected input receives")) return false;
    input.close();
    output.close();
    hostTurn(100);
    return check(inputLost == 1 && outputLost == 1, "lifecycle: close() does not fire");
}
} // namespace

int main(int argc, char** argv) {
    bool requireDevice = false;
    std::string loopbackName;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--require-device") requireDevice = true;
        else if (arg == "--loopback" && i + 1 < argc) loopbackName = argv[++i];
        else {
            std::fprintf(stderr, "Usage: tests-virtual [--require-device] [--loopback NAME]\n");
            return 1;
        }
    }
    const auto skip = [&](const char* reason) {
        std::printf("SKIP: %s\n", reason);
        return requireDevice ? 1 : 0;
    };
#if defined(__linux__) && !defined(__ANDROID__)
    if (!check(platformMidiApi() == libremidi::API::ALSA_SEQ, "Linux uses ALSA sequencer")) return 1;
#if defined(LIBREMIDI_ALSA)
    if (!std::filesystem::exists("/dev/snd/seq"))
        return skip("ALSA sequencer unavailable: no /dev/snd/seq; rerun on a host with snd-seq enabled.");
    snd_seq_t* probe = nullptr;
    const int result = snd_seq_open(&probe, "default", SND_SEQ_OPEN_DUPLEX, SND_SEQ_NONBLOCK);
    if (result < 0) {
        std::fprintf(stderr, "ALSA sequencer: %s\n", snd_strerror(result));
        return skip("cannot open ALSA sequencer; check /dev/snd/seq permissions.");
    }
    snd_seq_close(probe);
#else
    return skip("libremidi was built without ALSA sequencer support.");
#endif
#endif
    if (!loopbackName.empty()) return loopback(loopbackName) ? 0 : 1;
    if (platformMidiApi() != libremidi::API::ALSA_SEQ &&
        platformMidiApi() != libremidi::API::COREMIDI)
        return skip("automatic virtual ports require ALSA/CoreMIDI; use --loopback NAME with loopMIDI on Windows.");
    return virtualPorts() && lifecycle() ? 0 : 1;
}
