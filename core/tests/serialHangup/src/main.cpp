// =============================================================================
// core/tests/serialHangup — behavioral regression test for Serial device loss
// (#260).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: when the device behind a Serial goes away, the next
// available() / readBytes() / readByte() / writeBytes() notices it, closes
// the port and logs one warning, so isConnected() (and isInitialized()) turn
// false and a reconnect loop can call setup() again. A quiet but present
// device must NOT count as lost: with VMIN = VTIME = 0, read() returns 0 for
// both, and only poll()'s POLLHUP tells them apart.
//
// Before the fix, isInitialized() stayed true forever after an unplug.
//
// A pseudo-terminal stands in for the USB-serial adapter: Serial opens the
// slave, the test plays the device on the master, and closing the master
// hangs the slave up the way the kernel does when the USB device is removed.
// POSIX only; prints SKIP and passes on Windows (no pty).
// =============================================================================

#include <TrussC.h>

#include <cstdio>

using namespace std;
using namespace tc;

#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)

int main() {
    std::printf("SKIP: serialHangup needs a POSIX pseudo-terminal\n");
    return 0;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <string>
#include <thread>

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// "Serial: lost connection to ..." warnings seen so far
static int g_lostWarnings = 0;

// Poll cond every 5 ms until it holds or ms elapse.
template <typename F>
static bool waitFor(int ms, F cond) {
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (true) {
        if (cond()) return true;
        if (chrono::steady_clock::now() >= deadline) return false;
        this_thread::sleep_for(chrono::milliseconds(5));
    }
}

// One pty pair: the master is "the device", the slave path goes to Serial.
struct Pty {
    int master = -1;
    string slavePath;

    bool open() {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0) return false;
        if (grantpt(master) != 0 || unlockpt(master) != 0) {
            unplug();
            return false;
        }
        const char* name = ptsname(master);
        if (!name) {
            unplug();
            return false;
        }
        slavePath = name;
        return true;
    }

    // Closing the master hangs up the slave, like a USB unplug
    void unplug() {
        if (master >= 0) {
            ::close(master);
            master = -1;
        }
    }

    bool send(const char* s) {
        size_t n = strlen(s);
        return ::write(master, s, n) == (ssize_t)n;
    }

    // Read what Serial wrote, waiting up to ms for the first bytes
    string receive(int ms) {
        string out;
        struct pollfd pfd = {master, POLLIN, 0};
        if (poll(&pfd, 1, ms) > 0 && (pfd.revents & POLLIN)) {
            char buf[64];
            ssize_t n = ::read(master, buf, sizeof(buf));
            if (n > 0) out.assign(buf, (size_t)n);
        }
        return out;
    }

    ~Pty() { unplug(); }
};

// Open a Serial on a fresh pty; false (and a FAIL line) if that fails.
static bool connect(Pty& pty, Serial& serial, const char* what) {
    bool ok = pty.open() && serial.setup(pty.slavePath, 115200) && serial.isConnected();
    check(what, ok);
    return ok;
}

int main() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level == LogLevel::Warning && e.message.find("lost connection") != string::npos) {
            ++g_lostWarnings;
        }
    });

    char buf[64];

    // --- 1. data both ways, silence, then unplug (available() notices) -----
    {
        Pty pty;
        Serial serial;
        if (!connect(pty, serial, "1. setup() on a pty slave connects")) return 1;
        check("1. isInitialized() agrees with isConnected()", serial.isInitialized());

        check("1. device sends abc", pty.send("abc"));
        check("1. available() reports 3 bytes",
              waitFor(1000, [&] { return serial.available() == 3; }));
        int n = serial.readBytes(buf, sizeof(buf));
        check("1. readBytes() returns abc", n == 3 && memcmp(buf, "abc", 3) == 0);

        check("1. writeBytes(ping) returns 4", serial.writeBytes(string("ping")) == 4);
        check("1. device receives ping", pty.receive(1000) == "ping");

        // A quiet device is not a lost one
        bool quietOk = true;
        const auto end = chrono::steady_clock::now() + chrono::milliseconds(200);
        while (chrono::steady_clock::now() < end) {
            if (serial.available() != 0) quietOk = false;
            if (serial.readBytes(buf, sizeof(buf)) != 0) quietOk = false;
            if (serial.readByte() != -1) quietOk = false;
            if (!serial.isConnected()) quietOk = false;
            this_thread::sleep_for(chrono::milliseconds(10));
        }
        check("1. 200 ms of silence: no data, still connected", quietOk && serial.isConnected());
        check("1. no loss warning while quiet", g_lostWarnings == 0);

        pty.unplug();
        check("1. available() notices the unplug within 1 s",
              waitFor(1000, [&] { serial.available(); return !serial.isConnected(); }));
        check("1. isInitialized() is false too", !serial.isInitialized());
        check("1. available() == 0 after the loss", serial.available() == 0);
        check("1. readBytes() == -1 after the loss", serial.readBytes(buf, sizeof(buf)) == -1);
        check("1. readByte() == -2 after the loss", serial.readByte() == -2);
        check("1. writeBytes() == -1 after the loss", serial.writeBytes(string("x")) == -1);
        check("1. exactly one loss warning", g_lostWarnings == 1);

        // Reconnect on the same object, as the example's retry loop does
        Pty again;
        connect(again, serial, "1. setup() again after the loss connects");
        check("1. the new port carries data", again.send("z") &&
              waitFor(1000, [&] { return serial.readByte() == 'z'; }));
    }

    // --- 2. an app that only calls readBytes() ------------------------------
    {
        Pty pty;
        Serial serial;
        if (connect(pty, serial, "2. setup() connects")) {
            pty.unplug();
            int r = 0;
            bool lost = waitFor(1000, [&] { r = serial.readBytes(buf, sizeof(buf)); return r != 0; });
            check("2. readBytes() alone returns -1 on unplug", lost && r == -1);
            check("2. ... and the port is closed", !serial.isConnected());
        }
    }

    // --- 3. an app that only calls readByte() -------------------------------
    {
        Pty pty;
        Serial serial;
        if (connect(pty, serial, "3. setup() connects")) {
            pty.unplug();
            int r = -1;
            bool lost = waitFor(1000, [&] { r = serial.readByte(); return r != -1; });
            check("3. readByte() alone returns -2 on unplug", lost && r == -2);
            check("3. ... and the port is closed", !serial.isConnected());
        }
    }

    // --- 4. an app that only writes -----------------------------------------
    {
        Pty pty;
        Serial serial;
        if (connect(pty, serial, "4. setup() connects")) {
            pty.unplug();
            int r = 0;
            bool lost = waitFor(1000, [&] { r = serial.writeBytes(string("x")); return r < 0; });
            check("4. writeBytes() alone returns -1 on unplug", lost && r == -1);
            check("4. ... and the port is closed", !serial.isConnected());
        }
    }

    // One warning per loss: 1 + 2 + 3 + 4
    check("one loss warning per unplugged port", g_lostWarnings == 4);

    // --- 5. an explicit close() is not a loss --------------------------------
    {
        Pty pty;
        Serial serial;
        if (connect(pty, serial, "5. setup() connects")) {
            serial.close();
            check("5. close() disconnects", !serial.isConnected());
            check("5. close() logs no loss warning", g_lostWarnings == 4);
        }
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}

#endif
