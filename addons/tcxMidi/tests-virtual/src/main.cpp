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
    return virtualPorts() ? 0 : 1;
}
