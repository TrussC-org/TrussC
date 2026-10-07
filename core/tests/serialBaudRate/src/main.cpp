// =============================================================================
// core/tests/serialBaudRate — behavioral regression test for Serial baud
// rates on macOS / Linux (#260).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariant: Serial::setup() does not report success after opening
// at a speed other than the one asked for. Before the fix, a rate missing from
// the termios table (250000, 74880, and 921600 on macOS) silently opened at
// 9600 while setup() returned true and logged the requested rate.
//
//  - Rates with a termios B-constant apply as before.
//  - Linux: any other rate is applied exactly (termios2 + BOTHER).
//  - macOS: any other rate goes through IOSSIOSPEED, which a pty rejects, so
//    here setup() must return false (real serial drivers accept it; that part
//    needs hardware).
//  - A rate <= 0 is refused.
//  - Linux read-back: a real driver that cannot generate a rate writes back
//    another one (often 9600, or the rate it had) instead of failing, for
//    B-constant rates as well as termios2 ones, and setup() must then fail.
//    A driver that applies no rate at all (USB gadget /dev/ttyGS*, ...)
//    keeps the rate it had whatever is asked; setup() must succeed there
//    with a warning. A pty applies any rate as given, so fakeDriver.cpp
//    makes TCGETS2 report both. The rule that tells a fallback from a
//    driver's nearest divisor (internal::isBaudRateClose()) is also checked
//    on its own.
//
// A pseudo-terminal stands in for the port; its master reads back the speed
// Serial set on the slave. POSIX only; skipped on Windows, where the rate
// goes to the driver unchanged (dcb.BaudRate).
//
// On every platform: the Windows write timeout that setup() derives from the
// rate (internal::serialWriteTimeout()) leaves a write at least 4 times its
// time on the wire plus 5 s, rounded up to whole ms per byte. Android gives
// each 16 KB bulk transfer of a write the same rule
// (internal::serialWriteTimeoutMs()) instead of a fixed 1 s (#409).
// =============================================================================

#include <TrussC.h>

#include <cstdio>
#include <string>

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-60s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// --- the Windows write timeout, from the rate ---------------------------------
static void checkWriteTimeouts() {
    auto t9600 = internal::serialWriteTimeout(9600);
    check("write timeout at 9600: 5 ms per byte + 5000 ms",
          t9600.multiplierMs == 5 && t9600.constantMs == 5000);
    auto t115200 = internal::serialWriteTimeout(115200);
    check("write timeout at 115200: 1 ms per byte + 5000 ms",
          t115200.multiplierMs == 1 && t115200.constantMs == 5000);
    check("write timeout at 300: 134 ms per byte", internal::serialWriteTimeout(300).multiplierMs == 134);
    check("write timeout at 40000: exactly 1 ms per byte", internal::serialWriteTimeout(40000).multiplierMs == 1);
    check("write timeout at 40001: rounded up to 1 ms per byte",
          internal::serialWriteTimeout(40001).multiplierMs == 1);
    check("write timeout at 39999: rounded up to 2 ms per byte",
          internal::serialWriteTimeout(39999).multiplierMs == 2);
    check("write timeout for a nonsense rate is still finite",
          internal::serialWriteTimeout(0).multiplierMs == 40000 &&
          internal::serialWriteTimeout(-9600).multiplierMs == 40000);

    // 4 times the wire time of a 64 KB write (10 bits per byte), at least
    bool generous = true;
    for (int rate : {300, 1200, 9600, 31250, 57600, 74880, 115200, 250000, 921600, 2000000}) {
        auto t = internal::serialWriteTimeout(rate);
        const double bytes = 65536;
        const double wireMs = bytes * 10 * 1000 / rate;
        const double timeoutMs = t.multiplierMs * bytes + t.constantMs;
        if (timeoutMs < 4 * wireMs + 5000) generous = false;
    }
    check("write timeout is at least 4x the wire time + 5 s, 300 to 2000000 baud", generous);

    // Android: the same rule for each bulk transfer of a write (#409), not a
    // fixed 1 s per 16 KB chunk
    const int chunk = internal::serialAndroidWriteChunk;
    check("android write chunk is 16 KB", chunk == 16384);
    check("android chunk timeout at 9600: 5 ms * 16384 + 5000 = 86920 ms",
          internal::serialWriteTimeoutMs(9600, chunk) == 86920u);
    check("android chunk timeout at 115200: 1 ms * 16384 + 5000 = 21384 ms",
          internal::serialWriteTimeoutMs(115200, chunk) == 21384u);
    check("android chunk timeout of 1 byte at 9600: 5005 ms",
          internal::serialWriteTimeoutMs(9600, 1) == 5005u);
    check("android chunk timeout of 0 bytes: the constant only",
          internal::serialWriteTimeoutMs(9600, 0) == 5000u &&
          internal::serialWriteTimeoutMs(9600, -1) == 5000u);
    check("android chunk timeout for a nonsense rate is finite (40000 ms per byte)",
          internal::serialWriteTimeoutMs(0, chunk) == 40000u * 16384u + 5000u);
    check("android chunk timeout saturates instead of wrapping",
          internal::serialWriteTimeoutMs(0, 0x7fffffff) == static_cast<unsigned int>(-1));

    // A full chunk always leaves 4x its wire time + 5 s
    bool chunkGenerous = true;
    for (int rate : {300, 1200, 9600, 31250, 57600, 74880, 115200, 250000, 921600, 2000000}) {
        const double wireMs = (double)chunk * 10 * 1000 / rate;
        if (internal::serialWriteTimeoutMs(rate, chunk) < 4 * wireMs + 5000) chunkGenerous = false;
    }
    check("android chunk timeout is at least 4x the wire time + 5 s", chunkGenerous);
}

#if defined(_WIN32) || defined(__ANDROID__) || defined(__EMSCRIPTEN__)

int main() {
    checkWriteTimeouts();
    std::printf("SKIP: the rest of serialBaudRate needs a POSIX pseudo-terminal\n");
    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <cstdlib>
#include <vector>

// baudReadback.cpp
long readOutputBaud(int fd);

#if defined(__linux__)
// fakeDriver.cpp: while non-zero, TCGETS2 reports this output rate
void setFakeDriverRate(unsigned rate);
// fakeDriver.cpp: while from is non-zero, TCGETS2 reports `to` for `from`
void setFakeDriverSwap(unsigned from, unsigned to);
#endif

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

#if defined(__linux__)
// Open a fresh pty at `rate` while the "driver" writes `driverRate` back for
// it (and applies any other rate). driverRate 0: the rate the pty had before
// setup(), like a driver that rejected the rate and kept its old one; it is
// set to that rate. Returns setup()'s result.
static bool openWithDriverRate(int rate, unsigned& driverRate) {
    g_logs.clear();
    Pty pty;
    if (!pty.open()) {
        check("open a pty for " + to_string(rate), false);
        return false;
    }
    if (driverRate == 0) driverRate = (unsigned)readOutputBaud(pty.master);
    Serial serial;
    setFakeDriverSwap((unsigned)rate, driverRate);
    bool ok = serial.setup(pty.slavePath, rate);
    setFakeDriverSwap(0, 0);
    check(to_string(rate) + " -> " + to_string(driverRate) + ": isConnected() matches setup()",
          serial.isConnected() == ok);
    return ok;
}

// A driver that applies no rate and keeps `keptRate` whatever is asked
// (u_serial /dev/ttyGS*): setup() succeeds, and warns that the rate has no
// effect.
static void expectRateIgnored(int rate, unsigned keptRate) {
    g_logs.clear();
    string name = to_string(rate) + " on a driver that keeps " + to_string(keptRate);
    Pty pty;
    if (!pty.open()) {
        check("open a pty for " + name, false);
        return;
    }
    Serial serial;
    setFakeDriverRate(keptRate);
    bool ok = serial.setup(pty.slavePath, rate);
    setFakeDriverRate(0);
    check(name + ": setup() succeeds", ok && serial.isConnected());
    check(name + ": with a warning that the rate has no effect",
          logged("does not apply baud rates (it keeps " + to_string(keptRate) + ")"));
    check(name + ": and no error", !logged("cannot set"));
}

// A driver that fell back to another rate: setup() must fail and say so.
// driverRate 0: back to the rate the pty had (see openWithDriverRate()).
static void expectSwapRefused(int rate, unsigned driverRate) {
    string name = to_string(rate) + " -> " + (driverRate ? to_string(driverRate) : "the old rate");
    check(name + ": setup() fails", !openWithDriverRate(rate, driverRate));
    check(name + ": the error names the applied rate",
          logged("cannot set " + to_string(rate) + " baud") &&
          logged("the driver applied " + to_string(driverRate)));
    check(name + ": not taken for a driver that applies no rate",
          !logged("does not apply baud rates"));
    check(name + ": no success line at the requested rate",
          !logged("at " + to_string(rate) + " baud"));
}

// A driver's nearest divisor: setup() succeeds and logs the applied rate.
static void expectNearestAccepted(int rate, unsigned driverRate) {
    string name = to_string(rate) + " -> " + to_string(driverRate);
    check(name + ": setup() succeeds", openWithDriverRate(rate, driverRate));  // driverRate != 0 stays
    check(name + ": the log names the applied rate",
          logged("at " + to_string(driverRate) + " baud") &&
          logged("the driver applied " + to_string(driverRate)));
}
#endif

int main() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        g_logs.push_back(e.message);
    });

    checkWriteTimeouts();

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

    // --- 5. the read-back rule ------------------------------------------------
    // A fallback is far off; a nearest divisor (CP2102N: 74766 for 74880) is
    // within the kernel's 2%.
    check("read-back 9600 for 250000 is not the rate", !internal::isBaudRateClose(250000, 9600));
    check("read-back 74766 for 74880 is the rate", internal::isBaudRateClose(74880, 74766));
    check("read-back 250000 for 250000 is the rate", internal::isBaudRateClose(250000, 250000));
    check("read-back 2% off is the rate", internal::isBaudRateClose(100000, 98000) &&
                                          internal::isBaudRateClose(100000, 102000));
    check("read-back more than 2% off is not the rate", !internal::isBaudRateClose(100000, 97999) &&
                                                        !internal::isBaudRateClose(100000, 102001));
    check("read-back 0 is not the rate", !internal::isBaudRateClose(9600, 0));

#if defined(__linux__)
    // --- 6. a driver that swaps in another rate -------------------------------
    // tcsetattr() succeeds for a B-constant rate too, so setup() must read
    // the rate back on that path as well as on the termios2 one.
    expectSwapRefused(115200, 9600);         // B-constant
#ifdef B4000000
    expectSwapRefused(4000000, 9600);        // FT232R above its 3 MBd maximum
#endif
    expectSwapRefused(250000, 9600);         // termios2
    expectSwapRefused(2000000, 1000000);     // clamped to the chip maximum
    // Back to the rate the tty had: the driver still applies other rates,
    // so this is a rejected rate, not a driver that applies none
    expectSwapRefused(115200, 0);            // B-constant
    expectSwapRefused(250000, 0);            // termios2
    // CP2104: 115384 is the nearest rate its divisor gives for 115200
    expectNearestAccepted(115200, 115384);
    expectNearestAccepted(74880, 74766);     // termios2, CP2102N

    // --- 7. a driver that applies no rate at all -------------------------------
    // u_serial (/dev/ttyGS*, a Pi Zero's USB gadget), usb_serial_generic,
    // xHCI DbC: the kernel restores the old rate after every change, so the
    // read-back never matches. The rate means nothing there; setup() must
    // not fail forever.
    expectRateIgnored(115200, 9600);         // B-constant
    expectRateIgnored(250000, 9600);         // termios2
    expectRateIgnored(9600, 38400);          // the probe rate itself
#endif

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}

#endif
