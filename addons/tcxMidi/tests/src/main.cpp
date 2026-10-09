// Console-only harness: link TrussC without starting its app loop.
namespace trussc {}
namespace tc = trussc;
#include "tcxMidiOut.h"

#include <cstdio>
#include <functional>

namespace {
using tcx::midi::MidiMessage;
using tcx::midi::MidiOut;
int checks = 0;
int failures = 0;
void check(bool ok, const std::string& label) {
    ++checks;
    if (!ok) { ++failures; std::fprintf(stderr, "FAIL: %s\n", label.c_str()); }
}
struct SendCase {
    const char* name;
    std::function<bool(MidiOut&)> send;
    std::vector<unsigned char> expected;
};
}

int main() {
    const std::vector<SendCase> cases = {
        {"note on", [](MidiOut& o) { return o.sendNoteOn(1, 60, 100); }, {0x90, 60, 100}},
        {"note off", [](MidiOut& o) { return o.sendNoteOff(2, 61); }, {0x81, 61, 0}},
        {"control change", [](MidiOut& o) { return o.sendControlChange(16, 7, 64); }, {0xBF, 7, 64}},
        {"program change", [](MidiOut& o) { return o.sendProgramChange(3, 42); }, {0xC2, 42}},
        {"aftertouch", [](MidiOut& o) { return o.sendAftertouch(4, 64); }, {0xD3, 64}},
        {"poly aftertouch", [](MidiOut& o) { return o.sendPolyAftertouch(5, 60, 64); }, {0xA4, 60, 64}},
        {"pitch bend", [](MidiOut& o) { return o.sendPitchBend(6, 8192); }, {0xE5, 0, 64}},
        {"raw pitch bend", [](MidiOut& o) { return o.sendPitchBend(7, static_cast<unsigned char>(255), static_cast<unsigned char>(128)); }, {0xE6, 127, 0}},
        {"sysex", [](MidiOut& o) { return o.sendSysex({0xF0, 1, 0xF7}); }, {0xF0, 1, 0xF7}},
        {"MIDI byte", [](MidiOut& o) { return o.sendMidiByte(0xF8); }, {0xF8}},
        {"bytes", [](MidiOut& o) { return o.sendBytes({0xF2, 1, 2}); }, {0xF2, 1, 2}},
        {"message", [](MidiOut& o) { return o.send(MidiMessage({0xFA})); }, {0xFA}},
        {"low clamps", [](MidiOut& o) { return o.sendNoteOn(0, -1, -100); }, {0x90, 0, 0}},
        {"high clamps", [](MidiOut& o) { return o.sendNoteOn(17, 128, 999); }, {0x9F, 127, 127}},
        {"low bend clamp", [](MidiOut& o) { return o.sendPitchBend(1, -1); }, {0xE0, 0, 0}},
        {"high bend clamp", [](MidiOut& o) { return o.sendPitchBend(1, 16384); }, {0xE0, 127, 127}},
    };
    std::vector<std::string> warnings;
    auto listener = tc::getLogger().onLog.listen([&](tc::LogEventArgs& event) {
        if (event.level == tc::LogLevel::Warning &&
            event.message.find("[tcxMidiOut]") != std::string::npos)
            warnings.push_back(event.message);
    });
    tc::setConsoleLogLevel(tc::LogLevel::Silent);
    MidiOut out;
    const size_t closedLogged = warnings.size();
    for (const auto& item : cases) {
        const size_t sent = libremidi::test::sends;
        check(!item.send(out), std::string(item.name) + ": closed returns false");
        check(libremidi::test::sends == sent, "closed does not reach backend");
        check(warnings.size() == closedLogged + 1, "closed helpers share one warning");
    }
    check(out.openVirtualPort("send test"), "open mock virtual port");
    bool firstBackendFailure = true;
    for (const auto& item : cases) {
        const size_t logged = warnings.size();
        const size_t sent = libremidi::test::sends;
        libremidi::test::failSend = false;
        check(item.send(out), std::string(item.name) + ": success returns true");
        check(libremidi::test::sends == sent + 1, "exactly one backend send");
        check(libremidi::test::lastBytes == item.expected, "wire bytes unchanged");
        check(warnings.size() == logged, "success does not warn");
        libremidi::test::failSend = true;
        check(!item.send(out), std::string(item.name) + ": backend failure returns false");
        check(libremidi::test::sends == sent + 2, "failure attempted exactly once");
        check(warnings.size() == logged + (firstBackendFailure ? 1 : 0) &&
              warnings.back().find("injected send error") != std::string::npos,
              "backend helpers share one warning, even across successful sends");
        firstBackendFailure = false;
        check(out.isOpen(), "send failure preserves open state");
    }
    libremidi::test::failSend = false;
    const size_t sent = libremidi::test::sends;
    const size_t logged = warnings.size();
    check(!out.sendBytes({}), "empty bytes return false");
    check(!out.sendSysex({}), "empty sysex returns false");
    check(!out.send(MidiMessage{}), "empty message returns false");
    check(libremidi::test::sends == sent && warnings.size() == logged + 1,
          "empty helpers share one warning without reaching backend");
    check(out.sendNoteOn(1, 60, 100), "success after failure");
    out.closePort();
    check(!out.sendMidiByte(0xF8), "send after close returns false");
    check(out.openPort("test output"), "reopen mock named port");
    check(out.sendMidiByte(0xFA), "send after reopen succeeds");
    // Existing callers can continue ignoring all return values.
    out.sendNoteOn(1, 60, 100);
    out.sendNoteOff(1, 60);
    out.sendControlChange(1, 7, 64);
    out.sendProgramChange(1, 42);
    out.sendAftertouch(1, 64);
    out.sendPolyAftertouch(1, 60, 64);
    out.sendPitchBend(1, 8192);
    out.sendPitchBend(1, static_cast<unsigned char>(0), static_cast<unsigned char>(64));
    out.sendSysex({0xF0, 1, 0xF7});
    out.sendMidiByte(0xF8);
    out.sendBytes({0xFA});
    out.send(MidiMessage({0xFC}));

    // Count real logger warning events: clock traffic must not flood the log.
    auto checkFlood = [&](const std::function<bool()>& send,
                          const std::string& reason, bool reachesBackend) {
        const size_t beforeWarnings = warnings.size();
        const size_t beforeSends = libremidi::test::sends;
        bool allFailed = true;
        for (int i = 0; i < 100; ++i) {
            if (send()) allFailed = false;
        }
        check(allFailed, reason + ": all 100 failures return false");
        check(warnings.size() == beforeWarnings + 1,
              reason + ": exactly one warning for 100 failures");
        check(warnings.size() > beforeWarnings &&
              warnings.back().find(reason) != std::string::npos,
              reason + ": first warning includes reason text");
        check(libremidi::test::sends == beforeSends + (reachesBackend ? 100 : 0),
              reason + ": backend call count");
    };
    MidiOut flood;
    auto closedFlood = [&](MidiOut& o) {
        checkFlood([&] { return o.sendMidiByte(0xF8); }, "no output port is open", false);
    };
    auto openFlood = [&](MidiOut& o) {
        checkFlood([&] { return o.sendBytes({}); }, "empty message", false);
        libremidi::test::failSend = true;
        checkFlood([&] { return o.sendMidiByte(0xF8); }, "injected send error", true);
        libremidi::test::failSend = false;
    };
    closedFlood(flood);
    MidiOut otherClosed;
    closedFlood(otherClosed);
    const size_t independentClosedWarnings = warnings.size();
    check(!flood.sendMidiByte(0xF8), "first closed object still returns false");
    check(warnings.size() == independentClosedWarnings,
          "second closed object does not reset first object's gate");
    flood.closePort();
    closedFlood(flood); // Closing an already closed port also resets the gate.
    check(!flood.openPort(-1), "invalid index open fails");
    closedFlood(flood);
    check(!flood.openPort("missing output"), "unmatched name open fails");
    closedFlood(flood);

    const std::vector<std::function<bool()>> reopen = {
        [&] { return flood.openPort(0); },
        [&] { return flood.openPort("test output"); },
        [&] { return flood.openVirtualPort("flood test"); },
    };
    for (const auto& open : reopen) {
        check(open(), "open for repeated failure test");
        openFlood(flood);
        check(open(), "reopen without explicit close");
        openFlood(flood); // Both reasons produce one more warning after reopening.
        flood.closePort();
        closedFlood(flood);
    }
    // Gates belong to each output, even while another has exhausted its gates.
    check(flood.openVirtualPort("first output"), "open first independent output");
    openFlood(flood);
    MidiOut independent;
    closedFlood(independent);
    check(independent.openVirtualPort("second output"), "open second independent output");
    openFlood(independent);
    const size_t independentOpenWarnings = warnings.size();
    check(!flood.sendBytes({}), "first object's empty send still returns false");
    libremidi::test::failSend = true;
    check(!flood.sendMidiByte(0xF8), "first object's backend failure still returns false");
    libremidi::test::failSend = false;
    check(warnings.size() == independentOpenWarnings,
          "opening second object does not reset first object's gates");
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
