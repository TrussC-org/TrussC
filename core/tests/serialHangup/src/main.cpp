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
// Serial::onDisconnect fires once per open connection: on a detected loss
// (wasClean = false, reason = the warning's text) and on close() of an open
// port (wasClean = true), never from the destructor or a move assignment.
// The port is closed before listeners run, so a listener may call setup()
// to reconnect, and the call that found the loss must leave the new
// connection alone (sections 6 - 8). Only the I/O calls find a loss: close()
// or setup() after an unplug nobody noticed is a plain close (section 9).
//
// Several threads may use one Serial: an unplug found by one of them closes
// the fd exactly once, and no other thread reads, writes or closes that fd
// number afterwards (section 10). The I/O calls share the Serial's lock, so
// they never wait for each other, even for a write that takes long; opening
// and closing wait for the I/O calls in progress (section 11), and a loss
// found on one connection never closes the next one (section 12).
// isConnected() and getDevicePath() never wait for close(), even from a
// thread close() is waiting for (section 13). setup() and close() log only
// once they have released the lock, so a Logger listener may call back into
// the same Serial (section 14). The Android backend's guard against joining
// its worker from the worker itself is checked on its own (section 15).
// setup() and close() on a thread marked as a USB worker are refused, with
// one log line per thread, and before they take the lock, so they cannot
// deadlock with a close() waiting for that thread (sections 16, 17). Android
// Serial::setup() keeps the new port by what the backend's setup() returned
// (section 18). A slow write is played by slowWrite.cpp (Linux only).
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
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

static int g_fail = 0;
static void check(const char* name, bool ok) {
    std::printf("%-60s %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++g_fail;
}

// "Serial: lost connection to ..." warnings seen so far, and the last one
static int g_lostWarnings = 0;
static string g_lastLostWarning;

// "Serial: setup() closes the port an onDisconnect listener opened" warnings
static int g_overruledWarnings = 0;

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
#if defined(__linux__)
// slowWrite.cpp: a write() to fd that first waits ms (fd -1: off)
void setSlowWrite(int fd, int ms);
int slowWritesInProgress();
int slowWritesToClosedFd();
void setSlowWriteHook(void (*hook)());

// Section 13's hook: runs inside a write that close() is waiting for
static Serial* g_hookSerial = nullptr;
static atomic<bool> g_closeCalled{false};
static atomic<bool> g_hookDone{false};
static bool g_hookSawConnected = false;
static string g_hookSawPath;

// Section 17's hook: the thread close() waits for is a USB worker, and a
// Logger listener running there calls close() and setup()
static string g_hookOtherPath;
static bool g_hookSetupOk = true;

static void refuseWhileCloseWaits() {
    internal::SerialWorkerThreadMark mark;
    for (int i = 0; i < 400 && !g_closeCalled; ++i) this_thread::sleep_for(chrono::milliseconds(5));
    this_thread::sleep_for(chrono::milliseconds(50));
    g_hookSerial->close();
    g_hookSetupOk = g_hookSerial->setup(g_hookOtherPath, 9600);
    g_hookSawConnected = g_hookSerial->isConnected();
    g_hookSawPath = g_hookSerial->getDevicePath();
    g_hookDone = true;
}

static void askWhileCloseWaits() {
    // Wait until close() has been called, then give it time to start
    // waiting for this write
    for (int i = 0; i < 400 && !g_closeCalled; ++i) this_thread::sleep_for(chrono::milliseconds(5));
    this_thread::sleep_for(chrono::milliseconds(50));
    g_hookSawConnected = g_hookSerial->isConnected();
    g_hookSawPath = g_hookSerial->getDevicePath();
    g_hookDone = true;
}

// The fd this process has open on path (the port Serial opened), or -1
static int fdOf(const string& path) {
    char target[256];
    for (int fd = 0; fd < 1024; ++fd) {
        string link = "/proc/self/fd/" + to_string(fd);
        ssize_t n = readlink(link.c_str(), target, sizeof(target) - 1);
        if (n <= 0) continue;
        target[n] = '\0';
        if (path == target) return fd;
    }
    return -1;
}
#endif

static bool connect(Pty& pty, Serial& serial, const char* what) {
    bool ok = pty.open() && serial.setup(pty.slavePath, 115200) && serial.isConnected();
    check(what, ok);
    return ok;
}

// Records the onDisconnect notifications of one Serial
struct Recorder {
    int count = 0;
    SerialDisconnectEventArgs first;  // args of the first notification
    SerialDisconnectEventArgs last;   // args of the latest one
    bool sawOpenPort = false;         // a listener ran while isConnected()
    EventListener sub;

    void attach(Serial& serial) {
        sub = serial.onDisconnect.listen([this, &serial](SerialDisconnectEventArgs& e) {
            if (++count == 1) first = e;
            last = e;
            if (serial.isConnected()) sawOpenPort = true;
        });
    }
};

// A loss notification: wasClean false, the port and rate it was opened with,
// and the reason the warning gave ("Serial: lost connection to P (reason)")
static bool isLossOf(const SerialDisconnectEventArgs& e, const Pty& pty) {
    const string tail = " (" + e.reason + ")";
    const string& w = g_lastLostWarning;
    return !e.wasClean && e.portName == pty.slavePath && e.baudRate == 115200 &&
           !e.reason.empty() && w.size() >= tail.size() &&
           w.compare(w.size() - tail.size(), tail.size(), tail) == 0;
}

// Open file descriptors in this process (to spot a leaked port)
static int countOpenFds() {
    int n = 0;
    for (int fd = 0; fd < 1024; ++fd) {
        if (fcntl(fd, F_GETFD) != -1) ++n;
    }
    return n;
}

int main() {
    EventListener logSub = getLogger().onLog.listen([](LogEventArgs& e) {
        if (e.level != LogLevel::Warning) return;
        if (e.message.find("lost connection") != string::npos) {
            ++g_lostWarnings;
            g_lastLostWarning = e.message;
        }
        if (e.message.find("onDisconnect listener opened") != string::npos) {
            ++g_overruledWarnings;
        }
    });

    char buf[64];

    // --- 1. data both ways, silence, then unplug (available() notices) -----
    {
        Pty pty;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
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
        check("1. no onDisconnect while quiet", rec.count == 0);

        // Through a const reference: available() must stay const and still
        // close the port
        const Serial& cs = serial;
        pty.unplug();
        check("1. const available() notices the unplug within 1 s",
              waitFor(1000, [&] { cs.available(); return !cs.isConnected(); }));
        check("1. isInitialized() is false too", !serial.isInitialized());
        check("1. onDisconnect fired once", rec.count == 1);
        check("1. ... as a loss of this port at 115200, reason as logged", isLossOf(rec.first, pty));
        check("1. ... after the port was closed", !rec.sawOpenPort);
        check("1. available() == 0 after the loss", serial.available() == 0);
        check("1. readBytes() == -1 after the loss", serial.readBytes(buf, sizeof(buf)) == -1);
        check("1. readByte() == -2 after the loss", serial.readByte() == -2);
        check("1. writeBytes() == -1 after the loss", serial.writeBytes(string("x")) == -1);
        serial.close();
        check("1. exactly one loss warning", g_lostWarnings == 1);
        check("1. no second onDisconnect (later calls, close())", rec.count == 1);

        // Reconnect on the same object, as the example's retry loop does
        Pty again;
        connect(again, serial, "1. setup() again after the loss connects");
        check("1. setup() on a closed port does not fire", rec.count == 1);
        check("1. the new port carries data", again.send("z") &&
              waitFor(1000, [&] { return serial.readByte() == 'z'; }));
    }

    // --- 2. an app that only calls readBytes() ------------------------------
    {
        Pty pty;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
        if (connect(pty, serial, "2. setup() connects")) {
            pty.unplug();
            int r = 0;
            bool lost = waitFor(1000, [&] { r = serial.readBytes(buf, sizeof(buf)); return r != 0; });
            check("2. readBytes() alone returns -1 on unplug", lost && r == -1);
            check("2. ... and the port is closed", !serial.isConnected());
            check("2. onDisconnect fired once, as a loss", rec.count == 1 && isLossOf(rec.first, pty));
            check("2. ... after the port was closed", !rec.sawOpenPort);
            serial.close();
            check("2. close() after the loss does not fire again", rec.count == 1);
        }
    }

    // --- 3. an app that only calls readByte() -------------------------------
    {
        Pty pty;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
        if (connect(pty, serial, "3. setup() connects")) {
            pty.unplug();
            int r = -1;
            bool lost = waitFor(1000, [&] { r = serial.readByte(); return r != -1; });
            check("3. readByte() alone returns -2 on unplug", lost && r == -2);
            check("3. ... and the port is closed", !serial.isConnected());
            check("3. onDisconnect fired once, as a loss", rec.count == 1 && isLossOf(rec.first, pty));
        }
    }

    // --- 4. an app that only writes -----------------------------------------
    {
        Pty pty;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
        if (connect(pty, serial, "4. setup() connects")) {
            pty.unplug();
            int r = 0;
            bool lost = waitFor(1000, [&] { r = serial.writeBytes(string("x")); return r < 0; });
            check("4. writeBytes() alone returns -1 on unplug", lost && r == -1);
            check("4. ... and the port is closed", !serial.isConnected());
            check("4. onDisconnect fired once, as a loss", rec.count == 1 && isLossOf(rec.first, pty));
        }
    }

    // One warning per loss: 1 + 2 + 3 + 4
    check("one loss warning per unplugged port", g_lostWarnings == 4);

    // --- 5. an explicit close() is not a loss --------------------------------
    {
        Pty pty;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
        serial.close();
        check("5. close() on a port never opened does not fire", rec.count == 0);
        if (connect(pty, serial, "5. setup() connects")) {
            int before = g_lostWarnings;
            serial.close();
            check("5. close() disconnects", !serial.isConnected());
            check("5. close() logs no loss warning", g_lostWarnings == before);
            check("5. close() fires onDisconnect once", rec.count == 1);
            check("5. ... wasClean, \"closed by close()\"",
                  rec.first.wasClean && rec.first.reason == "closed by close()");
            check("5. ... with this port at 115200",
                  rec.first.portName == pty.slavePath && rec.first.baudRate == 115200);
            check("5. ... after the port was closed", !rec.sawOpenPort);
            serial.close();
            check("5. a second close() does not fire", rec.count == 1);
        }

        // setup() on an open port closes it first, which is a close()
        Pty next, after;
        if (connect(next, serial, "5. setup() connects again") &&
            connect(after, serial, "5. setup() on the open port moves to another pty")) {
            check("5. that setup() fired once more, wasClean, for the previous port",
                  rec.count == 2 && rec.last.wasClean && rec.last.portName == next.slavePath &&
                  serial.getDevicePath() == after.slavePath);
        }
    }

    // --- 6. destruction and move assignment do not fire ---------------------
    {
        Pty pty;
        Recorder rec;
        auto serial = make_unique<Serial>();
        rec.attach(*serial);
        if (connect(pty, *serial, "6. setup() connects")) {
            serial.reset();
            check("6. destroying an open Serial does not fire onDisconnect", rec.count == 0);
        }

        Pty ptyA, ptyB;
        Serial a, b;
        Recorder recA, recB;
        recA.attach(a);
        recB.attach(b);
        if (connect(ptyA, a, "6. setup() connects a") && connect(ptyB, b, "6. setup() connects b")) {
            a = std::move(b);
            check("6. a move assignment over an open Serial does not fire",
                  recA.count == 0 && recB.count == 0);
            check("6. ... and a now holds b's port",
                  a.isConnected() && a.getDevicePath() == ptyB.slavePath);
            a.close();
            check("6. a's own listener reports the port it holds now",
                  recA.count == 1 && recA.first.portName == ptyB.slavePath && recB.count == 0);
        }
    }

    // --- 7. a listener reconnects from inside the notification --------------
    // The loss is found by readBytes(), by available() through a const
    // Serial&, and by writeBytes(). Each time the listener opens a fresh pty,
    // which already has data waiting (or must get none, for writeBytes()),
    // and the call that found the loss must return its usual error value
    // without touching the new connection.
    for (int via = 0; via < 3; ++via) {
        const char* name = via == 0 ? "7. readBytes()" : via == 1 ? "7. const available()" : "7. writeBytes()";
        auto label = [&](const char* what) { return string(name) + ": " + what; };

        Pty lostPty, newPty;
        Serial serial;
        const Serial& cs = serial;
        int events = 0;
        bool sawOpenPort = false;
        bool reconnected = false;
        bool sentFromListener = false;
        SerialDisconnectEventArgs firstArgs;
        EventListener sub = serial.onDisconnect.listen([&](SerialDisconnectEventArgs& e) {
            if (++events == 1) firstArgs = e;
            if (serial.isConnected()) sawOpenPort = true;
            if (e.wasClean) return;
            reconnected = serial.setup(newPty.slavePath, e.baudRate);
            if (via != 2) sentFromListener = newPty.send("new");
        });
        if (!connect(lostPty, serial, label("setup() connects").c_str())) continue;
        check(label("a second pty opens").c_str(), newPty.open());

        // Call until the notification comes; r is what that call returned
        lostPty.unplug();
        int r = 0;
        bool found = waitFor(1000, [&] {
            if (via == 0) r = serial.readBytes(buf, sizeof(buf));
            else if (via == 1) r = cs.available();
            else r = serial.writeBytes(string("x"));
            return events > 0;
        });
        check(label("finds the loss and returns its error value").c_str(),
              found && r == (via == 1 ? 0 : -1));
        check(label("one notification, a loss of the first pty").c_str(),
              events == 1 && isLossOf(firstArgs, lostPty));
        check(label("the listener saw the port closed").c_str(), !sawOpenPort);
        check(label("setup() inside the listener succeeds").c_str(), reconnected);
        check(label("still connected, to the new pty, after the call").c_str(),
              serial.isConnected() && serial.getDevicePath() == newPty.slavePath);
        if (via != 2) {
            bool intact = sentFromListener &&
                          waitFor(1000, [&] { return serial.available() == 3; }) &&
                          serial.readBytes(buf, sizeof(buf)) == 3 && memcmp(buf, "new", 3) == 0;
            check(label("the new pty's waiting data is intact").c_str(), intact);
        } else {
            check(label("the failed write did not reach the new pty").c_str(),
                  newPty.receive(100).empty());
        }
        check(label("the new connection writes").c_str(),
              serial.writeBytes(string("ok")) == 2 && newPty.receive(1000) == "ok");
        check(label("no second notification").c_str(), events == 1);
    }

    // --- 8. setup() overrules a listener that reopens on a clean close ------
    {
        Pty first, second, reopened;
        Serial serial;
        int events = 0;
        bool reopenedOk = false;
        EventListener sub = serial.onDisconnect.listen([&](SerialDisconnectEventArgs&) {
            ++events;
            // Wrong on purpose: reconnects on a clean close too
            reopenedOk = serial.setup(reopened.slavePath, 115200);
        });
        if (connect(first, serial, "8. setup() connects") &&
            second.open() && reopened.open()) {
            int fdsBefore = countOpenFds();
            int warnedBefore = g_overruledWarnings;
            bool ok = serial.setup(second.slavePath, 115200);
            check("8. setup() fires once for the previous port", events == 1);
            check("8. the listener's setup() succeeded inside", reopenedOk);
            check("8. the outer setup() wins", ok && serial.isConnected() &&
                  serial.getDevicePath() == second.slavePath);
            check("8. ... and warns that it closed the listener's port",
                  g_overruledWarnings == warnedBefore + 1);
            check("8. no port is left open behind it", countOpenFds() == fdsBefore);
            check("8. the outer port carries data", second.send("s") &&
                  waitFor(1000, [&] { return serial.readByte() == 's'; }));
        }
    }

    // --- 9. an unplug no I/O call noticed, then close() / setup() -----------
    // Only available() / readBytes() / readByte() / writeBytes() find a loss,
    // so close() and setup() just close the port: one notification, wasClean,
    // "closed by close()". (On Android the worker thread may have found the
    // loss already; the reason then says so, still with wasClean.)
    {
        const int fdsBefore = countOpenFds();
        const int warningsBefore = g_lostWarnings;
        {
            Pty pty;
            Serial serial;
            Recorder rec;
            rec.attach(serial);
            if (connect(pty, serial, "9. setup() connects")) {
                pty.unplug();
                serial.close();
                check("9. close() after an unnoticed unplug fires once", rec.count == 1);
                check("9. ... wasClean, \"closed by close()\", this port",
                      rec.first.wasClean && rec.first.reason == "closed by close()" &&
                      rec.first.portName == pty.slavePath && rec.first.baudRate == 115200);
                check("9. ... after the port was closed", !rec.sawOpenPort && !serial.isConnected());
            }

            Pty lostPty, nextPty;
            Serial other;
            Recorder otherRec;
            otherRec.attach(other);
            if (connect(lostPty, other, "9. setup() connects another Serial") && nextPty.open()) {
                lostPty.unplug();
                bool ok = other.setup(nextPty.slavePath, 115200);
                check("9. setup() elsewhere after an unnoticed unplug fires once",
                      otherRec.count == 1);
                check("9. ... wasClean, \"closed by close()\", the unplugged port",
                      otherRec.first.wasClean && otherRec.first.reason == "closed by close()" &&
                      otherRec.first.portName == lostPty.slavePath);
                check("9. ... after the port was closed", !otherRec.sawOpenPort);
                check("9. ... and connects to the new port",
                      ok && other.isConnected() && other.getDevicePath() == nextPty.slavePath);
            }
        }
        check("9. no loss warning", g_lostWarnings == warningsBefore);
        check("9. no descriptor left open", countOpenFds() == fdsBefore);
    }

    // --- 10. several threads use one Serial while the device goes away ------
    // Three threads call available() (through a const Serial&), readBytes(),
    // readByte() and writeBytes() in a loop while the device talks, then goes
    // away. Every call holds the Serial's lock, so the fd is closed exactly
    // once, and no thread touches that number again: the numbers the kernel
    // hands out right after (here to pipes, standing in for any file the app
    // opens) get no stray byte from a late write and are not closed by a
    // second close(). A number closed under us would go to the next pipe()
    // at once, so each pipe end is recognized by its inode, not its number.
    {
        const int fdsBefore = countOpenFds();
        const int warningsBefore = g_lostWarnings;
        struct PipeEnd {
            int fd;
            dev_t dev;
            ino_t ino;
        };
        vector<array<PipeEnd, 2>> pipes;
        // Still this pipe end: the number is open and names the same pipe
        auto isSame = [](const PipeEnd& end) {
            struct stat st;
            return fstat(end.fd, &st) == 0 && st.st_dev == end.dev && st.st_ino == end.ino;
        };
        {
            Pty pty;
            Serial serial;
            const Serial& cs = serial;
            atomic<int> events{0};
            atomic<bool> stop{false};
            EventListener sub = serial.onDisconnect.listen([&](SerialDisconnectEventArgs&) { ++events; });
            if (connect(pty, serial, "10. setup() connects")) {
                fcntl(pty.master, F_SETFL, fcntl(pty.master, F_GETFL) | O_NONBLOCK);
                vector<thread> threads;
                threads.emplace_back([&] {
                    char b[64];
                    while (!stop) {
                        cs.available();
                        serial.readBytes(b, sizeof(b));
                    }
                });
                threads.emplace_back([&] {
                    while (!stop) {
                        cs.available();
                        serial.readByte();
                    }
                });
                threads.emplace_back([&] {
                    while (!stop) serial.writeBytes(string("x"));
                });

                // The device talks both ways for a while, then goes away
                char sink[256];
                auto until = chrono::steady_clock::now() + chrono::milliseconds(100);
                while (chrono::steady_clock::now() < until) {
                    pty.send("abc");
                    while (::read(pty.master, sink, sizeof(sink)) > 0) {}
                    this_thread::sleep_for(chrono::milliseconds(1));
                }
                pty.unplug();

                // Take the numbers the kernel frees from now on
                until = chrono::steady_clock::now() + chrono::milliseconds(200);
                while (chrono::steady_clock::now() < until && pipes.size() < 64) {
                    int p[2];
                    if (pipe(p) == 0) {
                        fcntl(p[0], F_SETFL, O_NONBLOCK);
                        struct stat r, w;
                        fstat(p[0], &r);
                        fstat(p[1], &w);
                        pipes.push_back({PipeEnd{p[0], r.st_dev, r.st_ino},
                                         PipeEnd{p[1], w.st_dev, w.st_ino}});
                    }
                    this_thread::sleep_for(chrono::milliseconds(2));
                }
                stop = true;
                for (auto& t : threads) t.join();

                check("10. the loss is found and reported once",
                      events == 1 && g_lostWarnings == warningsBefore + 1);
                check("10. the port is closed", !serial.isConnected());
                // While every pipe stays open the kernel never hands a number
                // out twice, so a repeated number means one was closed under us
                bool stillOpen = true;
                bool noStrayByte = true;
                set<int> numbers;
                for (const auto& p : pipes) {
                    for (const auto& end : p) {
                        if (!numbers.insert(end.fd).second || !isSame(end)) stillOpen = false;
                    }
                    char c;
                    if (isSame(p[0]) && ::read(p[0].fd, &c, 1) > 0) noStrayByte = false;
                }
                check("10. no later descriptor is closed by a second close()", stillOpen);
                check("10. no later descriptor gets a late write", noStrayByte);
            }
        }
        // Close each number once, whatever happened to it
        set<int> toClose;
        for (const auto& p : pipes) {
            toClose.insert(p[0].fd);
            toClose.insert(p[1].fd);
        }
        for (int fd : toClose) ::close(fd);
        check("10. no descriptor left open", countOpenFds() == fdsBefore);
    }

#if defined(__linux__)
    // --- 11. the I/O calls do not wait for each other ------------------------
    // Two workers write back to back, staggered, and each write takes 200 ms,
    // as a WriteFile() to a slow device does on Windows: there is always a
    // write in progress. The main thread's isConnected() and available() must
    // not wait for those writes. close() must, and must still get in: it
    // returns once the writes in progress have, no write reaches the fd after
    // it is closed, and the writes that keep coming do not starve it.
    {
        Pty pty;
        Serial serial;
        if (connect(pty, serial, "11. setup() connects")) {
            fcntl(pty.master, F_SETFL, fcntl(pty.master, F_GETFL) | O_NONBLOCK);
            const int fd = fdOf(pty.slavePath);
            check("11. the test finds the port's fd", fd >= 0);
            setSlowWrite(fd, 200);
            atomic<bool> stop{false};
            atomic<int> writes{0};
            auto writeLoop = [&] {
                while (!stop) {
                    if (serial.writeBytes(string("x")) == 1) ++writes;
                }
            };
            thread first(writeLoop);
            this_thread::sleep_for(chrono::milliseconds(100));
            thread second(writeLoop);
            check("11. slow writes are in progress", waitFor(1000, [] { return slowWritesInProgress() > 0; }));

            // One second of calls, as a frame loop makes them. On a thread of
            // its own, so that a starved call fails the check instead of
            // hanging the test: after 3 s the workers stop and let it in.
            atomic<long long> worstUs{0};
            atomic<bool> probed{false};
            thread prober([&] {
                char sink[64];
                const auto until = chrono::steady_clock::now() + chrono::milliseconds(1000);
                while (chrono::steady_clock::now() < until) {
                    const auto t0 = chrono::steady_clock::now();
                    bool connected = serial.isConnected();
                    int n = serial.available();
                    const long long us = chrono::duration_cast<chrono::microseconds>(
                        chrono::steady_clock::now() - t0).count();
                    if (us > worstUs) worstUs = us;
                    (void)connected;
                    (void)n;
                    while (::read(pty.master, sink, sizeof(sink)) > 0) {}
                    this_thread::sleep_for(chrono::milliseconds(5));
                }
                probed = true;
            });
            bool probedInTime = waitFor(3000, [&] { return probed.load(); });
            if (!probedInTime) stop = true;
            prober.join();
            string name = "11. isConnected() / available() do not wait for the writes (worst " +
                          to_string(worstUs / 1000) + " ms)";
            check(name.c_str(), probedInTime && worstUs < 50000);
            check("11. the workers kept writing meanwhile", writes >= 6);

            // close() from a third thread, while the writes keep coming
            atomic<bool> closed{false};
            atomic<int> stillWriting{-1};
            thread closer([&] {
                serial.close();
                stillWriting = slowWritesInProgress();
                closed = true;
            });
            bool gotIn = waitFor(1500, [&] { return closed.load(); });
            stop = true;  // lets a starved close() in, so the test ends
            closer.join();
            first.join();
            second.join();
            setSlowWrite(-1, 0);
            check("11. close() gets in while writes keep coming (within 1.5 s)", gotIn);
            check("11. close() returns only after the writes in progress", stillWriting == 0);
            check("11. no write reached the closed fd", slowWritesToClosedFd() == 0);
            check("11. the port is closed", !serial.isConnected());
        }
    }

    // --- 12. a loss found on the old connection leaves the new one alone -----
    // The worker's write to the first pty is in progress when the device goes
    // away, and the main thread calls setup() for a second pty meanwhile. That
    // setup() closes the first port once the write has returned (a clean
    // close) and opens the second. The worker's write fails, and its loss
    // belongs to the connection that is already closed: it must not close
    // the new one, whichever of the two gets the lock first afterwards. Which
    // one does is up to the scheduler, so play it 8 times: a loss that closed
    // the new connection would show in some of them.
    {
        int rounds = 0, writeFailed = 0, reopened = 0, oneCleanClose = 0, stayedOpen = 0, carried = 0;
        for (int round = 0; round < 8; ++round) {
            Pty oldPty, newPty;
            Serial serial;
            Recorder rec;
            rec.attach(serial);
            if (!oldPty.open() || !newPty.open() || !serial.setup(oldPty.slavePath, 115200)) continue;
            ++rounds;
            setSlowWrite(fdOf(oldPty.slavePath), 200);
            int written = 0;
            thread worker([&] { written = serial.writeBytes(string("x")); });
            waitFor(1000, [] { return slowWritesInProgress() > 0; });
            oldPty.unplug();
            bool ok = serial.setup(newPty.slavePath, 115200);
            worker.join();
            setSlowWrite(-1, 0);
            if (written == -1) ++writeFailed;
            if (ok) ++reopened;
            if (rec.count == 1 && rec.first.wasClean && rec.first.portName == oldPty.slavePath) ++oneCleanClose;
            if (serial.isConnected() && serial.getDevicePath() == newPty.slavePath) ++stayedOpen;
            if (newPty.send("n") && waitFor(1000, [&] { return serial.readByte() == 'n'; })) ++carried;
        }
        check("12. 8 rounds of a write failing on the old port during setup()", rounds == 8);
        check("12. each time, that write fails", writeFailed == rounds);
        check("12. each time, setup() on the new port succeeds", reopened == rounds);
        check("12. each time, one notification: a clean close of the old port", oneCleanClose == rounds);
        check("12. each time, the new connection stays open", stayedOpen == rounds);
        check("12. each time, the new port carries data", carried == rounds);
    }
#endif

#if defined(__linux__)
    // --- 13. isConnected() / getDevicePath() never wait for close() ----------
    // close() holds the lock exclusive while it waits for a thread that is
    // inside a write (on Android it waits the same way for the USB worker,
    // whose log lines may run a Logger listener). If that thread asks the
    // Serial isConnected() or getDevicePath(), the answer must not wait for
    // close() in turn, or neither ever finishes. slowWrite.cpp runs the hook
    // inside the write, once close() is waiting for it.
    {
        Pty pty;
        Serial serial;
        if (connect(pty, serial, "13. setup() connects")) {
            fcntl(pty.master, F_SETFL, fcntl(pty.master, F_GETFL) | O_NONBLOCK);
            g_hookSerial = &serial;
            setSlowWrite(fdOf(pty.slavePath), 100);
            setSlowWriteHook(askWhileCloseWaits);
            thread writer([&] { serial.writeBytes(string("x")); });
            check("13. the write close() will wait for is in progress",
                  waitFor(1000, [] { return slowWritesInProgress() > 0; }));
            atomic<bool> closed{false};
            thread closer([&] {
                serial.close();
                closed = true;
            });
            g_closeCalled = true;
            bool finished = waitFor(3000, [&] { return closed.load() && g_hookDone.load(); });
            check("13. isConnected() / getDevicePath() inside it do not deadlock with close()", finished);
            if (!finished) {
                // Both threads are stuck for good: report and leave
                std::printf("\nFAILED (deadlock; %d failure%s)\n", g_fail, g_fail == 1 ? "" : "s");
                std::fflush(stdout);
                _exit(1);
            }
            closer.join();
            writer.join();
            setSlowWrite(-1, 0);
            setSlowWriteHook(nullptr);
            check("13. ... and see the port still open, with its path",
                  g_hookSawConnected && g_hookSawPath == pty.slavePath);
            check("13. close() then closes it", !serial.isConnected());
        }
    }
#endif

    // --- 14. a Logger listener may call the Serial that is logging ---------
    // setup() and close() decide what to log while they hold the lock, and
    // log once they have released it. A Logger listener that runs inline on
    // their thread and calls back into the same Serial ("connected to",
    // "disconnected from") must not deadlock with them. On a thread of its
    // own with a deadline, so a deadlock fails the check.
    {
        Pty pty;
        Serial serial;
        atomic<int> callbacks{0};
        EventListener logBack = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.message.rfind("Serial: connected to", 0) == 0 ||
                e.message.rfind("Serial: disconnected from", 0) == 0) {
                serial.isConnected();
                serial.available();
                serial.writeBytes(string("l"));
                ++callbacks;
            }
        });
        atomic<bool> done{false};
        bool ok = false;
        thread t([&] {
            ok = pty.open() && serial.setup(pty.slavePath, 115200);
            serial.close();
            done = true;
        });
        bool finished = waitFor(3000, [&] { return done.load(); });
        check("14. a Logger listener calling back from setup() / close() does not deadlock", finished);
        if (!finished) {
            // The thread is stuck for good: report and leave
            std::printf("\nFAILED (deadlock; %d failure%s)\n", g_fail, g_fail == 1 ? "" : "s");
            std::fflush(stdout);
            _exit(1);
        }
        t.join();
        check("14. ... and it ran for the connect and the disconnect line", ok && callbacks >= 2);
    }

    // --- 15. the self-join guard of the Android backend ---------------------
    // The Android backend asks internal::isThisThread(worker) before it joins
    // its USB worker: on the worker itself (a Logger listener running there
    // calls close() or destroys the Serial) join() would throw, so it refuses
    // or hands the connection over instead. The backend needs Android, but the
    // guard is plain C++.
    {
        thread none;
        check("15. an empty thread object is not this thread", !internal::isThisThread(none));
        atomic<bool> go{false};
        bool fromInside = false;
        thread worker;
        worker = thread([&] {
            while (!go) this_thread::yield();
            fromInside = internal::isThisThread(worker);
        });
        check("15. seen from another thread, the worker is not this thread", !internal::isThisThread(worker));
        go = true;
        worker.join();
        check("15. seen from inside, the worker is this thread", fromInside);
        check("15. once joined, it is nobody's thread", !internal::isThisThread(worker));
    }

    // --- 16. setup() / close() on a USB worker thread are refused ---------
    // On Android a Logger listener may run inline on a USB worker thread, of
    // this Serial or another. setup() and close() there would stop a worker
    // and wait for it, so they are refused: nothing closes, no onDisconnect,
    // the path stays, and one error line per thread (a listener that calls
    // close() on every line must not loop). internal::SerialWorkerThreadMark
    // plays the worker here; the refusal itself is plain C++.
    atomic<int> refusals{0};
    EventListener refusalSub = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error && e.message.find("cannot run on a USB worker thread") != string::npos) {
            ++refusals;
        }
    });
    {
        Pty pty, other;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
        if (connect(pty, serial, "16. setup() connects") && other.open()) {
            refusals = 0;
            bool setupOk = true;
            bool stillConnected = false;
            string pathAfter;
            thread worker([&] {
                internal::SerialWorkerThreadMark mark;
                serial.close();
                setupOk = serial.setup(other.slavePath, 9600);
                serial.close();
                stillConnected = serial.isConnected();
                pathAfter = serial.getDevicePath();
            });
            worker.join();
            check("16. close() on a worker thread leaves the port open", stillConnected);
            check("16. setup() there returns false and keeps the path", !setupOk && pathAfter == pty.slavePath);
            check("16. neither fires onDisconnect", rec.count == 0);
            check("16. one error line for three refused calls", refusals == 1);
            check("16. the port still carries data",
                  pty.send("w") && waitFor(1000, [&] { return serial.readByte() == 'w'; }));
            thread second([&] {
                internal::SerialWorkerThreadMark mark;
                serial.close();
            });
            second.join();
            check("16. another worker thread logs its own refusal", refusals == 2 && serial.isConnected());
            { internal::SerialWorkerThreadMark mark; }
            check("16. the mark ends with its scope", !internal::onSerialWorkerThread());
            serial.close();
            check("16. then close() closes and fires once", !serial.isConnected() && rec.count == 1);
        }
    }

#if defined(__linux__)
    // --- 17. the refusal comes before the lock ----------------------------
    // close() on one thread holds the lock while it waits for a thread that
    // is inside a write (on Android: for the USB worker). If that thread is a
    // USB worker and calls close() or setup() (a Logger listener there), they
    // must be refused before they take the lock, or neither thread ever
    // finishes. slowWrite.cpp runs the hook inside the write, once close() is
    // waiting for it. The refused setup() must not show its port meanwhile.
    {
        Pty pty, other;
        Serial serial;
        Recorder rec;
        rec.attach(serial);
        if (connect(pty, serial, "17. setup() connects") && other.open()) {
            fcntl(pty.master, F_SETFL, fcntl(pty.master, F_GETFL) | O_NONBLOCK);
            g_hookSerial = &serial;
            g_hookOtherPath = other.slavePath;
            g_closeCalled = false;
            g_hookDone = false;
            g_hookSawConnected = false;
            g_hookSawPath.clear();
            setSlowWrite(fdOf(pty.slavePath), 100);
            setSlowWriteHook(refuseWhileCloseWaits);
            thread writer([&] { serial.writeBytes(string("x")); });
            check("17. the write close() will wait for is in progress",
                  waitFor(1000, [] { return slowWritesInProgress() > 0; }));
            atomic<bool> closed{false};
            thread closer([&] {
                serial.close();
                closed = true;
            });
            g_closeCalled = true;
            bool finished = waitFor(3000, [&] { return closed.load() && g_hookDone.load(); });
            check("17. close() / setup() on the worker do not deadlock with close() waiting for it", finished);
            if (!finished) {
                // Both threads are stuck for good: report and leave
                std::printf("\nFAILED (deadlock; %d failure%s)\n", g_fail, g_fail == 1 ? "" : "s");
                std::fflush(stdout);
                _exit(1);
            }
            closer.join();
            writer.join();
            setSlowWrite(-1, 0);
            setSlowWriteHook(nullptr);
            check("17. the refused setup() returned false", !g_hookSetupOk);
            check("17. meanwhile the port stayed open, with its own path",
                  g_hookSawConnected && g_hookSawPath == pty.slavePath);
            check("17. the outer close() fires once, for the port and rate it had",
                  rec.count == 1 && rec.first.wasClean && rec.first.portName == pty.slavePath &&
                  rec.first.baudRate == 115200);
            check("17. and closes it", !serial.isConnected());
        }
    }
#endif

    // --- 18. Android setup() keeps the new port by the backend's result -----
    // When the backend asked for the USB permission, its worker may connect
    // before Serial::setup() looks again, so setup() goes by the result: a
    // Pending setup() keeps the new port's path and rate, as a Connected one
    // does; only a Failed one goes back to the previous port's.
    check("18. a Connected setup() keeps the new port",
          internal::serialSetupStarted(internal::SerialSetupResult::Connected));
    check("18. a Pending setup() keeps it too",
          internal::serialSetupStarted(internal::SerialSetupResult::Pending));
    check("18. a Failed setup() goes back to the previous port",
          !internal::serialSetupStarted(internal::SerialSetupResult::Failed));

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}

#endif
