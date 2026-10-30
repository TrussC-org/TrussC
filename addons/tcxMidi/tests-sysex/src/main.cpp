// Console-only tests of the upstream libremidi stream decoder.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiIn.h"
#include <libremidi/detail/midi_stream_decoder.hpp>
#include <cstdio>

int alsaRoundtrip(bool requireDevice);

namespace {
int failures = 0;
int checks = 0;
void check(bool ok, const char* label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label); }
}
using Bytes = std::vector<unsigned char>;

void decoderCases(bool multi) {
    // Both sizes exceed the removed TrussC policy; neither is a MIDI maximum.
    for (size_t size : {size_t{1048577}, size_t{2097169}}) {
        Bytes expected(size);
        for (size_t i = 0; i < size; ++i) expected[i] = static_cast<unsigned char>(i % 128);
        expected.front() = 0xF0;
        expected.back() = 0xF7;
        for (bool fragmented : {false, true}) {
            libremidi::input_configuration config;
            config.ignore_sysex = false;
            std::vector<Bytes> received;
            std::vector<int64_t> timestamps;
            config.on_message = [&](libremidi::message&& msg) {
                received.emplace_back(msg.bytes.begin(), msg.bytes.end());
                timestamps.push_back(msg.timestamp);
            };
            libremidi::midi1::input_state_machine machine{config};
            auto feed = [&](std::span<const unsigned char> bytes, int64_t timestamp) {
                if (multi) machine.on_bytes_multi(bytes, timestamp);
                else machine.on_bytes(bytes, timestamp);
            };
            if (fragmented) {
                constexpr size_t chunkSize = 65536;
                for (size_t offset = 0; offset < size; offset += chunkSize) {
                    feed(std::span<const unsigned char>{expected}.subspan(
                        offset, std::min(chunkSize, size - offset)), offset == 0 ? 21 : 22);
                }
            } else {
                feed(expected, 21);
            }
            check(received == std::vector<Bytes>{expected}, "large SysEx arrives byte-for-byte without an addon cap");
            check(timestamps == std::vector<int64_t>{21}, "large SysEx retains its start timestamp");
            feed(Bytes{0xF0, 1, 0xF7}, 23);
            feed(Bytes{0x90, 60, 100}, 24);
            check(received.size() == 3 && received[1] == Bytes{0xF0, 1, 0xF7} &&
                  received[2] == Bytes{0x90, 60, 100}, "normal messages follow a large SysEx");
        }
    }
}
}

int main(int argc, char** argv) {
    if (argc > 1) {
        if (std::string(argv[1]) == "--alsa-roundtrip")
            return alsaRoundtrip(argc > 2 && std::string(argv[2]) == "--require-device");
        std::fprintf(stderr, "Usage: tests-sysex [--alsa-roundtrip [--require-device]]\n");
        return 1;
    }
    decoderCases(false);
    decoderCases(true);
    std::printf("SysEx: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
