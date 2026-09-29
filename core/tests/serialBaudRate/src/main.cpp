// =============================================================================
// core/tests/serialBaudRate — behavioral regression test for Serial baud
// rates on macOS / Linux (#260).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: Serial::setup() never reports success at a speed
// other than the one asked for. Before the fix, a rate missing from the
// termios table (250000, 74880, and 921600 on macOS) silently opened at 9600
// while setup() returned true and logged the requested rate.
//
//  - Rates with a termios B-constant apply as before.
//  - Linux: any other rate is applied exactly (termios2 + BOTHER).
//  - macOS: any other rate goes through IOSSIOSPEED, which a pty rejects, so
//    here setup() must return false (real serial drivers accept it; that part
//    needs hardware).
//  - A rate <= 0 is refused.
//
// A pseudo-terminal stands in for the port; its master reads back the speed
// Serial set on the slave. POSIX only; prints SKIP and passes on Windows,
// where the rate goes to the driver unchanged (dcb.BaudRate).
// =============================================================================

#include <TrussC.h>

#include <cstdio>

using namespace std;
using namespace tc;

#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)

int main() {
    std::printf("SKIP: serialBaudRate needs a POSIX pseudo-terminal\n");
    return 0;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <string>
#include <vector>

// baudReadback.cpp
long readOutputBaud(int fd);

static int g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

static vector<string> g_logs;

struct Pty {
    int master = -1;
    string slavePath;

    bool open() {
        master = posix_openpt(O_RDWR | O_NOCTTY);
        if (master < 0) return false;
        if (grantpt(master) != 0 || unlockpt(master) != 0 || !ptsname(master)) {
            ::close(master);
            master = -1;
            return false;
        }
        slavePath = ptsname(master);
        return true;
    }

    ~Pty() {
        if (master >= 0) ::close(master);
    }
};

static bool logged(const string& needle) {
    for (const auto& m : g_logs) {
        if (m.find(needle) != string::npos) return true;
    }
    return false;
}

// Open a fresh pty at `rate`. Returns setup()'s result; *readback gets the
// speed the master sees (-1 if setup failed).
static bool openAt(int rate, long* readback) {
    Pty pty;
    if (!pty.open()) {
        check("open a pty for " + to_string(rate), false);
        return false;
    }
    Serial serial;
    bool ok = serial.setup(pty.slavePath, rate);
    *readback = ok ? readOutputBaud(pty.master) : -1;
    if (!ok) check(to_string(rate) + ": a failed setup() leaves it disconnected", !serial.isConnected());
    return ok;
}

// The rate must apply exactly.
static void expectApplied(int rate) {
    g_logs.clear();
    long readback = 0;
    bool ok = openAt(rate, &readback);
    check(to_string(rate) + ": setup() succeeds", ok);
    check(to_string(rate) + ": the port runs at " + to_string(rate) + " (read " + to_string(readback) + ")",
          readback == rate);
    check(to_string(rate) + ": the log names the applied rate",
          logged("at " + to_string(rate) + " baud"));
}

// The rate may fail, but must never succeed at another speed.
static void expectAppliedOrRefused(int rate) {
    g_logs.clear();
    long readback = 0;
    bool ok = openAt(rate, &readback);
    if (ok) {
        check(to_string(rate) + ": applied exactly (read " + to_string(readback) + ")", readback == rate);
    } else {
        check(to_string(rate) + ": refused with an error", logged("cannot set " + to_string(rate)));
    }
}

int main() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        g_logs.push_back(e.message);
    });

    // --- 1. rates with a termios B-constant everywhere ------------------------
    for (int rate : {9600, 115200, 230400}) expectApplied(rate);

    // --- 2. rates that lack a B-constant somewhere ----------------------------
    // 250000: Marlin / DMX, 74880: ESP8266 boot log, 31250: MIDI (none of
    // them has one anywhere). 921600: ESP32, and 1000000 / 2000000: glibc has
    // B-constants for these, macOS does not.
    const vector<int> otherRates = {250000, 74880, 31250, 921600, 1000000, 2000000};
#if defined(__linux__)
    // termios2 applies any rate, and a pty takes whatever it is given
    for (int rate : otherRates) expectApplied(rate);
#else
    for (int rate : otherRates) expectAppliedOrRefused(rate);
#endif

    // --- 3. nonsense rates ----------------------------------------------------
    for (int rate : {0, -9600}) {
        g_logs.clear();
        long readback = 0;
        check(to_string(rate) + ": setup() refuses it", !openAt(rate, &readback));
        check(to_string(rate) + ": with an error naming it", logged("invalid baud rate " + to_string(rate)));
    }

    // --- 4. the old fallback is gone ------------------------------------------
    g_logs.clear();
    long readback = 0;
    openAt(250000, &readback);
    check("no \"using 9600\" fallback", !logged("using 9600"));
    check("no silent 9600 for 250000", readback != 9600);

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}

#endif
