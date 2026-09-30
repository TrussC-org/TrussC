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
// Every call holds the Serial's lock, so several threads may use one Serial:
// an unplug found by one of them closes the fd exactly once, and no other
// thread reads, writes or closes that fd number afterwards (section 10).
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
#include <array>
#include <atomic>
#include <chrono>
#include <memory>
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
    // second close().
    {
        const int fdsBefore = countOpenFds();
        const int warningsBefore = g_lostWarnings;
        vector<array<int, 2>> pipes;
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
                        pipes.push_back({p[0], p[1]});
                    }
                    this_thread::sleep_for(chrono::milliseconds(2));
                }
                stop = true;
                for (auto& t : threads) t.join();

                check("10. the loss is found and reported once",
                      events == 1 && g_lostWarnings == warningsBefore + 1);
                check("10. the port is closed", !serial.isConnected());
                bool stillOpen = true;
                bool noStrayByte = true;
                for (const auto& p : pipes) {
                    if (fcntl(p[0], F_GETFD) == -1 || fcntl(p[1], F_GETFD) == -1) stillOpen = false;
                    char c;
                    if (::read(p[0], &c, 1) > 0) noStrayByte = false;
                }
                check("10. no later descriptor is closed by a second close()", stillOpen);
                check("10. no later descriptor gets a late write", noStrayByte);
            }
        }
        for (const auto& p : pipes) {
            ::close(p[0]);
            ::close(p[1]);
        }
        check("10. no descriptor left open", countOpenFds() == fdsBefore);
    }

    std::printf("\n%s (%d failure%s)\n", g_fail ? "FAILED" : "ALL PASSED", g_fail, g_fail == 1 ? "" : "s");
    return g_fail ? 1 : 0;
}

#endif
