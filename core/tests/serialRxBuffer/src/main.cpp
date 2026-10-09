// #364: receive data survives app stalls; overflow keeps the newest bytes.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <chrono>
#include <thread>
#include <vector>

#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <cstdlib>
#include <cerrno>
#endif

using namespace std;
using namespace tc;

namespace {
#if !defined(_WIN32) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)
int failures = 0;
void check(const char* name, bool ok) {
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

template<class F> bool until(F condition) {
    // Failure deadline only; successful checks depend on observed data/state.
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(10);
    do {
        if (condition()) return true;
        this_thread::sleep_for(chrono::milliseconds(1));
    } while (chrono::steady_clock::now() < deadline);
    return false;
}

struct Pty {
    int master = -1;
    string path;
    bool open() {
        master = posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK);
        if (master < 0 || grantpt(master) || unlockpt(master)) return false;
        const char* name = ptsname(master);
        if (!name) return false;
        path = name;
        return true;
    }
    ~Pty() { if (master >= 0) ::close(master); }
    bool send(const vector<uint8_t>& data) {
        size_t offset = 0;
        return until([&] {
            const ssize_t n = ::write(master, data.data() + offset, data.size() - offset);
            if (n > 0) offset += static_cast<size_t>(n);
            return offset == data.size();
        });
    }
};
#endif
} // namespace

TC_CORE_TEST_MAIN() {
#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)
    printf("SKIP: serialRxBuffer needs a POSIX pseudo-terminal\n");
    return 0;
#else
    constexpr size_t cap = 1 << 20;
    const size_t total = cap + 131071;
    vector<uint8_t> input(total);
    for (size_t i = 0; i < total; ++i) input[i] = static_cast<uint8_t>((i * 17 + i / 251) % 256);
    // Android and desktop use this same policy, including chunks > capacity.
    {
        internal::SerialRxBuffer rx;
        OnceGate dropWarned{5.0};
        rx.append(input.data(), input.size());
        vector<uint8_t> newest(cap);
        check("shared policy counts oversized append exactly", rx.dropped == total - cap);
        check("shared policy keeps newest bytes", rx.read(newest.data(), cap) == static_cast<int>(cap) &&
              equal(newest.begin(), newest.end(), input.end() - cap));
        check("shared policy schedules the first overflow warning", rx.takeWarning(dropWarned));
    }
    Pty pty;
    Serial serial;
    check("new Serial counter is zero", serial.getDroppedByteCount() == 0);
    if (!pty.open() || !serial.setup(pty.path, 115200)) {
        check("pty setup", false);
        return 1;
    }
    int overflowWarnings = 0;
    bool warningHasTotal = false;
    EventListener warnings = getLogger().onLog.listen([&](LogEventArgs& event) {
        if (event.message.find("Serial: RX buffer overflow") != string::npos) {
            ++overflowWarnings;
            warningHasTotal = event.message.find(to_string(total - cap)) != string::npos;
            // Inline log callbacks must run with the Serial's locks released.
            serial.available();
            serial.getDroppedByteCount();
        }
    });
    // No app reads while the worker drains > 1 MiB from the pty.
    check("device writes the complete stream while app does not read", pty.send(input));
    const bool received = until([&] { return serial.getDroppedByteCount() == total - cap; });
    check("exact overflow count", received);
    check("available counts the capped TrussC buffer", serial.available() == static_cast<int>(cap));
    check("overflow warning includes total and permits inline callbacks", overflowWarnings == 1 && warningHasTotal);
    Serial moved(std::move(serial));
    check("move carries buffer and counter", !serial.isConnected() && moved.getDroppedByteCount() == total - cap);
    serial = std::move(moved);
    vector<uint8_t> output(cap);
    check("newest bytes survive a stall and both moves",
          serial.readBytes(output.data(), output.size()) == static_cast<int>(cap) &&
          equal(output.begin(), output.end(), input.end() - cap));
    check("reading does not count intentional consumption", serial.available() == 0 && serial.getDroppedByteCount() == total - cap);
    vector<uint8_t> small(input.begin(), input.begin() + 4096);
    check("device sends data below cap", pty.send(small));
    check("worker receives below-cap data without an app read", until([&] { return serial.available() == 4096; }));
    serial.flushInput();
    check("flushInput clears buffer and preserves counter", serial.available() == 0 && serial.getDroppedByteCount() == total - cap);
    check("data arrives after flushInput", pty.send(small) && until([&] { return serial.available() == 4096; }));
    serial.flush();
    check("flush clears buffer and preserves counter", serial.available() == 0 && serial.getDroppedByteCount() == total - cap);
    serial.close();
    check("close preserves counter", !serial.isConnected() && serial.getDroppedByteCount() == total - cap);
    Pty next;
    check("new connection resets counter", next.open() && serial.setup(next.path, 9600) && serial.getDroppedByteCount() == 0);
    check("below-cap stall loses no bytes", next.send(small) && until([&] { return serial.available() == 4096; }));
    vector<uint8_t> fresh(4096);
    check("below-cap contents survive", serial.readBytes(fresh.data(), fresh.size()) == 4096 && fresh == small && serial.getDroppedByteCount() == 0);
    return failures ? 1 : 0;
#endif
}
