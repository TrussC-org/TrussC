// =============================================================================
// core/tests/loggerThreadSafety — behavioral regression test for the Logger's
// thread safety and the sokol -> Logger bridge (#265).
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI).
//
// Guards the invariants:
//   - Several threads logging at once into a file set with setLogFile(),
//     while another thread switches the file and toggles the levels: every
//     line in the files is whole, and each one is there exactly once (none
//     lost, none duplicated). The console sink the same (std::cout captured).
//     The file listener used to write to one std::ofstream without a lock:
//     lines came out mixed, duplicated and missing.
//   - setLogFile() / closeFile() toggled while threads log: no torn or
//     duplicated line, and nothing lands in the file once closeFile() has
//     returned.
//   - An onLog listener that logs again, on its own thread or through a
//     thread it waits for, does not deadlock (listeners run outside the
//     Logger's lock).
//   - The sokol bridge (internal::sokolLog) maps panic / error / warning /
//     info to Fatal / Error / Warning / Verbose (Verbose is hidden by
//     default), logs "[tag] message", or "[tag] id:<item> line:<line>" when
//     sokol passes no message.
//   - POSIX, in a forked child each: a panic is written to the log file and
//     then handed to slog_func, which aborts; a panic while another thread
//     holds the Logger's lock does not wait for it (the line goes to stderr);
//     Linux: with DISPLAY unset and TRUSSC_LOG_FILE set, runApp()'s
//     XOpenDisplay() failure lands in that file (the file is opened before
//     sapp_run(), and sokol_app_tc.h reports through sapp_desc.logger).
//
// A watchdog turns a deadlock into a FAIL instead of a stuck CI job.
// =============================================================================

#include <TrussC.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#define LOGGER_TEST_FORK 1
#include <csignal>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace std;
using namespace tc;

static int g_fail = 0;
static void check(const char* name, bool ok) {
    printf("%-66s %s\n", name, ok ? "PASS" : "FAIL");
    fflush(stdout);   // flush per line so CI logs survive a later abort
    if (!ok) ++g_fail;
}

// Runs fn on its own thread. A deadlock (fn not done within ms) is a FAIL,
// and the process exits: a deadlocked thread cannot be joined.
static void runWithWatchdog(const char* name, const function<void()>& fn, int ms = 10000) {
    atomic<bool> done{false};
    thread t([&] { fn(); done = true; });
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    while (!done && chrono::steady_clock::now() < deadline) {
        this_thread::sleep_for(chrono::milliseconds(1));
    }
    if (!done) {
        check(name, false);
        printf("  (deadlock: still running after %d ms)\n", ms);
        fflush(stdout);
        _Exit(1);
    }
    t.join();
}

static fs::path tempFile(const string& name) {
    static const string stamp = to_string(chrono::steady_clock::now().time_since_epoch().count());
    fs::path p = fs::temp_directory_path() / ("tc_logger_test_" + stamp + "_" + name);
    error_code ec;
    fs::remove(p, ec);
    return p;
}

static vector<string> readLines(const fs::path& p) {
    vector<string> lines;
    ifstream in(p, ios::binary);
    string line;
    while (getline(in, line)) lines.push_back(line);
    return lines;
}

static vector<string> splitLines(const string& text) {
    vector<string> lines;
    istringstream in(text);
    string line;
    while (getline(in, line)) lines.push_back(line);
    return lines;
}

static bool contains(const vector<string>& lines, const string& needle) {
    for (const auto& l : lines) {
        if (l.find(needle) != string::npos) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Worker lines: "[HH:MM:SS.mmm] [NOTICE] worker T line I"
// ---------------------------------------------------------------------------
constexpr int kThreads = 4;
constexpr int kLines = 2000;

static string workerMessage(int t, int i) {
    return "worker " + to_string(t) + " line " + to_string(i);
}

// A whole worker line -> (t, i). False for a torn / mixed line.
static bool parseWorkerLine(const string& line, int& t, int& i) {
    const char* stamp = "[00:00:00.000] ";
    const size_t stampLen = strlen(stamp);
    if (line.size() < stampLen) return false;
    for (size_t k = 0; k < stampLen; ++k) {
        if (stamp[k] == '0') {
            if (line[k] < '0' || line[k] > '9') return false;
        } else if (line[k] != stamp[k]) {
            return false;
        }
    }
    const string rest = line.substr(stampLen);
    const string head = "[NOTICE] worker ";
    if (rest.compare(0, head.size(), head) != 0) return false;
    if (sscanf(rest.c_str() + head.size(), "%d line %d", &t, &i) != 2) return false;
    if (t < 0 || t >= kThreads || i < 0 || i >= kLines) return false;
    return rest == "[NOTICE] " + workerMessage(t, i);
}

struct LineTally {
    int total = 0;
    int torn = 0;
    int duplicated = 0;
    int missing = 0;
    string firstTorn;
};

static LineTally tally(const vector<string>& lines) {
    LineTally r;
    vector<int> seen(kThreads * kLines, 0);
    for (const auto& l : lines) {
        ++r.total;
        int t, i;
        if (!parseWorkerLine(l, t, i)) {
            if (r.torn++ == 0) r.firstTorn = l;
            continue;
        }
        if (++seen[t * kLines + i] == 2) ++r.duplicated;
    }
    for (int n : seen) {
        if (n == 0) ++r.missing;
    }
    return r;
}

static void printTally(const LineTally& r) {
    printf("  (lines %d, torn %d, duplicated %d, missing %d)\n",
           r.total, r.torn, r.duplicated, r.missing);
    if (r.torn) printf("  first torn line: %s\n", r.firstTorn.c_str());
}

// Starts kThreads workers that log kLines lines each into lg, all released
// at once. `done` counts finished workers.
static vector<thread> startWorkers(Logger& lg, atomic<bool>& go, atomic<int>& done) {
    vector<thread> ts;
    for (int t = 0; t < kThreads; ++t) {
        ts.emplace_back([&lg, &go, &done, t] {
            while (!go) this_thread::yield();
            for (int i = 0; i < kLines; ++i) {
                lg.log(LogLevel::Notice, workerMessage(t, i));
            }
            ++done;
        });
    }
    return ts;
}

// ---------------------------------------------------------------------------
// 1. File: every line whole, exactly once, while the file is switched and
//    the levels are toggled.
// ---------------------------------------------------------------------------
static void testFileSwitching() {
    const fs::path a = tempFile("switch_a.log");
    const fs::path b = tempFile("switch_b.log");
    int switches = 0;
    runWithWatchdog("file: concurrent lines while switching files", [&] {
        Logger lg;
        lg.setConsoleLogLevel(LogLevel::Silent);
        lg.setLogFile(a);
        atomic<bool> go{false};
        atomic<int> done{0};
        auto ts = startWorkers(lg, go, done);
        go = true;
        // Every level set here still passes Notice to the file and keeps it
        // off the console; each setLogFile() closes one file and opens the
        // other.
        while (done < kThreads) {
            const bool odd = (switches % 2) != 0;
            lg.setFileLogLevel(odd ? LogLevel::Verbose : LogLevel::Notice);
            lg.setConsoleLogLevel(odd ? LogLevel::Error : LogLevel::Silent);
            lg.setLogFile(odd ? a : b);
            ++switches;
            this_thread::sleep_for(chrono::microseconds(100));
        }
        for (auto& t : ts) t.join();
        lg.closeFile();
    });
    vector<string> lines = readLines(a);
    vector<string> linesB = readLines(b);
    lines.insert(lines.end(), linesB.begin(), linesB.end());
    const LineTally r = tally(lines);
    printf("  (%d file switches)\n", switches);
    check("file: every line whole (switching files, toggling levels)", r.torn == 0);
    check("file: exactly 8000 lines, none lost or duplicated", r.total == kThreads * kLines && r.duplicated == 0 && r.missing == 0);
    if (r.torn || r.duplicated || r.missing || r.total != kThreads * kLines) printTally(r);
    check("file: the switches overlapped the workers", switches > 1);
    error_code ec;
    fs::remove(a, ec);
    fs::remove(b, ec);
}

// ---------------------------------------------------------------------------
// 2. Console: the console sink is under the same lock (std::cout captured
//    into a string buffer, which is not thread-safe by itself).
// ---------------------------------------------------------------------------
static void testConsole() {
    ostringstream captured;
    runWithWatchdog("console: concurrent lines", [&] {
        Logger lg;
        lg.setConsoleLogLevel(LogLevel::Notice);
        streambuf* old = cout.rdbuf(captured.rdbuf());
        atomic<bool> go{false};
        atomic<int> done{0};
        auto ts = startWorkers(lg, go, done);
        go = true;
        for (auto& t : ts) t.join();
        cout.rdbuf(old);
    });
    const LineTally r = tally(splitLines(captured.str()));
    check("console: every line whole", r.torn == 0);
    check("console: exactly 8000 lines, none lost or duplicated", r.total == kThreads * kLines && r.duplicated == 0 && r.missing == 0);
    if (r.torn || r.duplicated || r.missing || r.total != kThreads * kLines) printTally(r);
}

// ---------------------------------------------------------------------------
// 3. setLogFile() / closeFile() toggled while threads log.
// ---------------------------------------------------------------------------
static void testOpenCloseRace() {
    const fs::path c = tempFile("toggle.log");
    bool closedStayedClosed = true;
    bool stateAfterClose = true;
    int toggles = 0;
    runWithWatchdog("file: setLogFile/closeFile toggled while logging", [&] {
        Logger lg;
        lg.setConsoleLogLevel(LogLevel::Silent);
        atomic<bool> go{false};
        atomic<int> done{0};
        auto ts = startWorkers(lg, go, done);
        go = true;
        for (; toggles < 100 && done < kThreads; ++toggles) {
            lg.setLogFile(c);
            this_thread::sleep_for(chrono::microseconds(200));
            lg.closeFile();
            this_thread::sleep_for(chrono::microseconds(100));
        }
        // Closed for good while the workers may still run: nothing may land
        // in the file after closeFile() has returned.
        lg.setLogFile(c);
        lg.closeFile();
        stateAfterClose = !lg.isFileOpen() && lg.getLogFilePath().empty();
        error_code ec;
        const auto sizeAtClose = fs::file_size(c, ec);
        for (auto& t : ts) t.join();
        closedStayedClosed = fs::file_size(c, ec) == sizeAtClose;
    });
    const LineTally r = tally(readLines(c));
    printf("  (%d open/close toggles, %d lines kept)\n", toggles, r.total);
    check("toggle: every line whole", r.torn == 0);
    check("toggle: no line duplicated", r.duplicated == 0);
    check("toggle: nothing written after closeFile() returned", closedStayedClosed);
    check("toggle: closeFile() leaves no file open, no path", stateAfterClose);
    if (r.torn || r.duplicated) printTally(r);
    error_code ec;
    fs::remove(c, ec);
}

// ---------------------------------------------------------------------------
// 4. Re-entrant onLog listeners.
// ---------------------------------------------------------------------------
static void testReentrantListener() {
    const fs::path d = tempFile("reentrant.log");
    runWithWatchdog("re-entrant: listener logs on its own thread", [&] {
        Logger lg;
        lg.setConsoleLogLevel(LogLevel::Silent);
        lg.setLogFile(d);
        EventListener l = lg.onLog.listen([&lg](LogEventArgs& e) {
            if (e.message == "outer") lg.log(LogLevel::Notice, "inner");
        });
        lg.log(LogLevel::Notice, "outer");
        lg.closeFile();
    }, 5000);
    const vector<string> lines = readLines(d);
    check("re-entrant: listener logs on its own thread, no deadlock",
          lines.size() == 2 && contains(lines, "] outer") && contains(lines, "] inner"));

    // The listener waits for another thread that logs: that thread would
    // block forever if the listener ran while this thread held the lock
    // (the lock is recursive, so the case above cannot show it).
    const fs::path e2 = tempFile("reentrant2.log");
    runWithWatchdog("re-entrant: listener waits for a thread that logs", [&] {
        Logger lg;
        lg.setConsoleLogLevel(LogLevel::Silent);
        lg.setLogFile(e2);
        EventListener l = lg.onLog.listen([&lg](LogEventArgs& e) {
            if (e.message != "outer") return;
            thread other([&lg] { lg.log(LogLevel::Notice, "from another thread"); });
            other.join();
        });
        lg.log(LogLevel::Notice, "outer");
        lg.closeFile();
    }, 5000);
    const vector<string> lines2 = readLines(e2);
    check("re-entrant: listener waits for a thread that logs, no deadlock",
          lines2.size() == 2 && contains(lines2, "] from another thread"));

    // A failed setLogFile() logs its error to the listeners (outside the lock).
    runWithWatchdog("setLogFile failure", [&] {
        Logger lg;
        lg.setConsoleLogLevel(LogLevel::Silent);
        bool sawError = false;
        EventListener l = lg.onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Error && e.message.find("Failed to open log file") != string::npos) {
                sawError = !lg.isFileOpen();
            }
        });
        const bool opened = lg.setLogFile(fs::temp_directory_path() / "tc_no_such_dir_265" / "x.log");
        check("setLogFile: a failed open returns false and logs an Error", !opened && sawError && !lg.isFileOpen());
    }, 5000);
    error_code ec;
    fs::remove(d, ec);
    fs::remove(e2, ec);
}

// ---------------------------------------------------------------------------
// 5. The sokol bridge (called directly with a fake item / tag).
// ---------------------------------------------------------------------------
static void testSokolBridge() {
    check("bridge: panic -> Fatal",     internal::sokolLogLevel(0) == LogLevel::Fatal);
    check("bridge: error -> Error",     internal::sokolLogLevel(1) == LogLevel::Error);
    check("bridge: warning -> Warning", internal::sokolLogLevel(2) == LogLevel::Warning);
    check("bridge: info -> Verbose",    internal::sokolLogLevel(3) == LogLevel::Verbose);
    check("bridge: message -> \"[tag] message\"",
          internal::sokolLogMessage("sg", 12, "bad image", 345) == "[sg] bad image");
    check("bridge: no message -> \"[tag] id:<item> line:<line>\"",
          internal::sokolLogMessage("sapp", 12, nullptr, 345) == "[sapp] id:12 line:345");

    Logger& lg = getLogger();
    check("bridge: Verbose hidden by default (console Notice)", lg.getConsoleLogLevel() == LogLevel::Notice);
    const LogLevel oldConsole = lg.getConsoleLogLevel();
    lg.setConsoleLogLevel(LogLevel::Silent);
    const fs::path f = tempFile("bridge.log");
    lg.setLogFile(f);

    struct Seen { LogLevel level; string message; };
    vector<Seen> seen;
    {
        EventListener l = lg.onLog.listen([&](LogEventArgs& e) { seen.push_back({e.level, e.message}); });
        internal::sokolLog("sg", 1, 12, nullptr, 345, nullptr, nullptr);
        internal::sokolLog("sapp", 2, 7, "warned", 10, "sokol_app.h", nullptr);
        internal::sokolLog("sgl", 3, 3, "info message", 11, nullptr, nullptr);
        internal::sokolLog("simgui", 1, 5, "imgui error", 12, nullptr, nullptr);
    }
    lg.closeFile();
    lg.setConsoleLogLevel(oldConsole);

    check("bridge: every call reaches onLog", seen.size() == 4);
    if (seen.size() == 4) {
        check("bridge: sg error, no message",
              seen[0].level == LogLevel::Error && seen[0].message == "[sg] id:12 line:345");
        check("bridge: sapp warning",
              seen[1].level == LogLevel::Warning && seen[1].message == "[sapp] warned");
        check("bridge: sgl info -> Verbose",
              seen[2].level == LogLevel::Verbose && seen[2].message == "[sgl] info message");
        check("bridge: simgui error",
              seen[3].level == LogLevel::Error && seen[3].message == "[simgui] imgui error");
    }
    const vector<string> lines = readLines(f);
    check("bridge: the file gets errors and warnings, not info",
          lines.size() == 3 &&
          contains(lines, "] [ERROR] [sg] id:12 line:345") &&
          contains(lines, "] [WARNING] [sapp] warned") &&
          contains(lines, "] [ERROR] [simgui] imgui error") &&
          !contains(lines, "info message"));
    error_code ec;
    fs::remove(f, ec);
}

#ifdef LOGGER_TEST_FORK
// ---------------------------------------------------------------------------
// 6. POSIX: panics, each in a forked child (they abort).
// ---------------------------------------------------------------------------
static string readText(const fs::path& p) {
    ifstream in(p, ios::binary);
    stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

// Forks, runs child() (which must end the child), and waits up to ms.
// Returns the wait status, or -1 when the child had to be killed.
static int runChild(const function<void()>& child, int ms = 10000) {
    fflush(stdout);
    fflush(stderr);
    const pid_t pid = fork();
    if (pid == 0) {
        child();
        _exit(0);
    }
    const auto deadline = chrono::steady_clock::now() + chrono::milliseconds(ms);
    int status = 0;
    while (true) {
        const pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) return status;
        if (chrono::steady_clock::now() >= deadline) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            return -1;
        }
        this_thread::sleep_for(chrono::milliseconds(5));
    }
}

static bool abortedWithSigabrt(int status) {
    return status != -1 && WIFSIGNALED(status) && WTERMSIG(status) == SIGABRT;
}

static void redirectStderr(const fs::path& p) {
    const int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd >= 0) {
        dup2(fd, 2);
        close(fd);
    }
}

static void testPanicForwards() {
    const fs::path log = tempFile("panic.log");
    const fs::path err = tempFile("panic.err");
    const int status = runChild([&] {
        redirectStderr(err);
        getLogger().setLogFile(log);
        internal::sokolLog("sg", 0, 77, nullptr, 123, nullptr, nullptr);
    });
    const string text = readText(log);
    check("panic: the child aborts (forwarded to slog_func)", abortedWithSigabrt(status));
    check("panic: the Fatal line reaches the log file first",
          text.find("] [FATAL] [sg] id:77 line:123") != string::npos);
    check("panic: slog_func still prints it",
          readText(err).find("ABORTING because of [panic]") != string::npos);
    error_code ec;
    fs::remove(log, ec);
    fs::remove(err, ec);
}

// Another thread holds the Logger's lock (setLogFile() on a FIFO no one
// reads blocks in open() under it): the panic must not wait.
static void testPanicDoesNotWaitForLock() {
    const fs::path fifo = tempFile("panic.fifo");
    const fs::path err = tempFile("panic_locked.err");
    if (mkfifo(fifo.c_str(), 0600) != 0) {
        check("panic with the lock held: mkfifo (skipped)", true);
        return;
    }
    const int status = runChild([&] {
        redirectStderr(err);
        atomic<bool> holding{false};
        thread holder([&] {
            holding = true;
            getLogger().setLogFile(fifo);   // blocks in open(), holding the lock
        });
        holder.detach();
        while (!holding) this_thread::yield();
        this_thread::sleep_for(chrono::milliseconds(200));
        internal::sokolLog("sg", 0, 78, nullptr, 124, nullptr, nullptr);
    }, 5000);
    const string text = readText(err);
    check("panic with the lock held: no wait, the child aborts", abortedWithSigabrt(status));
    check("panic with the lock held: the line goes to stderr",
          text.find("] [FATAL] [sg] id:78 line:124") != string::npos);
    error_code ec;
    fs::remove(fifo, ec);
    fs::remove(err, ec);
}

#if defined(__linux__) && !defined(__ANDROID__)
class NoDisplayApp : public App {};

// The manual check from #265, automated: no X display, TRUSSC_LOG_FILE set.
static void testXOpenDisplayFailureReachesLogFile() {
    const fs::path log = tempFile("nodisplay.log");
    const fs::path err = tempFile("nodisplay.err");
    const int status = runChild([&] {
        redirectStderr(err);
        unsetenv("DISPLAY");
        unsetenv("WAYLAND_DISPLAY");
        setenv("TRUSSC_LOG_FILE", log.c_str(), 1);
        WindowSettings settings;
        runApp<NoDisplayApp>(settings);
    });
    const string text = readText(log);
    check("no display: runApp() aborts", abortedWithSigabrt(status));
    check("no display: XOpenDisplay failure in TRUSSC_LOG_FILE",
          text.find("] [FATAL] [sapp] XOpenDisplay() failed (no X server / DISPLAY not set)") != string::npos);
    if (text.find("XOpenDisplay") == string::npos) {
        printf("  log file: [%s]\n  stderr: [%s]\n", text.c_str(), readText(err).c_str());
    }
    error_code ec;
    fs::remove(log, ec);
    fs::remove(err, ec);
}
#endif
#endif // LOGGER_TEST_FORK

int main() {
    printf("=== loggerThreadSafety (#265) ===\n");
    testFileSwitching();
    testConsole();
    testOpenCloseRace();
    testReentrantListener();
    testSokolBridge();
#ifdef LOGGER_TEST_FORK
    testPanicForwards();
    testPanicDoesNotWaitForLock();
#if defined(__linux__) && !defined(__ANDROID__)
    testXOpenDisplayFailureReachesLogFile();
#endif
#else
    printf("  (panic cases skipped: they need fork())\n");
#endif
    printf("\n%s (%d failure(s))\n", g_fail ? "FAILED" : "ALL PASSED", g_fail);
    return g_fail ? 1 : 0;
}
