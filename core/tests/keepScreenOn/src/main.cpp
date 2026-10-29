#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstring>
#include <thread>
#include <type_traits>
#include <vector>

#if defined(__linux__) && !defined(__ANDROID__)
#if __has_include(<X11/extensions/scrnsaver.h>)
#include <X11/extensions/scrnsaver.h>
#define TC_IDLE_CHECK_XSS_ABI
#endif
// Independent copies allow injection without a production test API.
#define applyKeepScreenOn probeLinuxApply
#include "../../../platform/linux/tcKeepScreenOn_linux.cpp"
#undef applyKeepScreenOn
#define applyKeepScreenOn probeDispatch
#define setKeepScreenOn probeSet
#define getKeepScreenOn probeGet
#define updateKeepScreenOn probeUpdate
#define shutdownKeepScreenOn probeShutdown
#include "../../../include/impl/tcKeepScreenOn.cpp"
#undef shutdownKeepScreenOn
#undef updateKeepScreenOn
#undef getKeepScreenOn
#undef setKeepScreenOn
#undef applyKeepScreenOn
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>
#endif

using namespace std;
namespace {
int failures = 0;
void check(const char* name, bool ok) {
    printf("%s %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}
#if defined(__linux__) && !defined(__ANDROID__)
using namespace trussc;
#ifdef TC_IDLE_CHECK_XSS_ABI
static_assert(is_same_v<decltype(IdleX11::queryExtension), decltype(&XScreenSaverQueryExtension)>);
static_assert(is_same_v<decltype(IdleX11::queryVersion), decltype(&XScreenSaverQueryVersion)>);
static_assert(is_same_v<decltype(IdleX11::suspendSaver), decltype(&XScreenSaverSuspend)>);
#endif
Display* fakeDisplay() { return reinterpret_cast<Display*>(1); }
vector<bool> suspends;
vector<thread::id> suspendThreads;
const char* missing = "";
int majorVersion = 1, minorVersion = 1;
bool extension = true, versionReply = true;
int loadCount = 0, queryCount = 0;
Bool fakeExtension(Display* d, int*, int*) {
    ++queryCount;
    return d == fakeDisplay() && extension;
}
Status fakeVersion(Display* d, int* major, int* minor) {
    *major = majorVersion; *minor = minorVersion;
    return d == fakeDisplay() && versionReply;
}
void fakeSuspend(Display* d, Bool enabled) {
    if (d != fakeDisplay()) abort();
    suspends.push_back(enabled != False);
    suspendThreads.push_back(this_thread::get_id());
}
void* fakeOpen(const char* name, int flags) {
    ++loadCount;
    if (strcmp(name, "libXss.so.1") || flags != (RTLD_NOW | RTLD_LOCAL)) abort();
    return !strcmp(missing, "library") ? nullptr : reinterpret_cast<void*>(1);
}
void* fakeSymbol(void*, const char* name) {
    if (!strcmp(missing, name)) return nullptr;
    if (!strcmp(name, "XScreenSaverQueryExtension")) return reinterpret_cast<void*>(&fakeExtension);
    if (!strcmp(name, "XScreenSaverQueryVersion")) return reinterpret_cast<void*>(&fakeVersion);
    if (!strcmp(name, "XScreenSaverSuspend")) return reinterpret_cast<void*>(&fakeSuspend);
    abort();
}
static_assert(is_same_v<decltype(&fakeExtension), decltype(IdleX11::queryExtension)>);
static_assert(is_same_v<decltype(&fakeVersion), decltype(IdleX11::queryVersion)>);
static_assert(is_same_v<decltype(&fakeSuspend), decltype(IdleX11::suspendSaver)>);
IdleX11 injectedX11() {
    IdleX11 x;
    x.openLibrary = fakeOpen; x.findSymbol = fakeSymbol;
    return x;
}
void resetFixture() {
    missing = ""; majorVersion = minorVersion = 1;
    extension = versionReply = true;
    loadCount = queryCount = 0;
    suspends.clear(); suspendThreads.clear();
}
vector<bool> dispatches;
vector<thread::id> dispatchThreads;
IdleX11* dispatchX11 = nullptr;
Display* dispatchDisplay = fakeDisplay();
bool throwDispatch = false;
bool requestDuringRelease = false;
int exitWriter = -1;
} // namespace
namespace trussc::internal {
void probeDispatch(bool enabled, bool headless) {
    dispatches.push_back(enabled);
    dispatchThreads.push_back(this_thread::get_id());
    if (exitWriter >= 0) {
        char value = enabled ? 'T' : 'F';
        if (write(exitWriter, &value, 1) != 1) abort();
    }
    if (dispatchX11) dispatchX11->apply(enabled, headless ? nullptr : dispatchDisplay);
    if (!enabled && requestDuringRelease) {
        thread worker([] { probeSet(true); });
        worker.join();
    }
    if (throwDispatch) throw 1;
}
} // namespace trussc::internal
namespace {
void zeroGate() {
    check("storage starts null", keepScreenStorage.load() == nullptr);
    for (int i = 0; i < 10000; ++i) {
        internal::probeUpdate(false);
        internal::probeUpdate(true);
    }
    internal::probeShutdown();
    check("unused window/headless hooks and cleanup keep storage null", keepScreenStorage.load() == nullptr);
    check("unused hooks perform no backend work", dispatches.empty() && loadCount == 0);
}
void sharedBehavior() {
    check("getter starts false", !probeGet());
    check("first API call publishes storage", keepScreenStorage.load() != nullptr);
    for (bool enabled : {false, true, true, false, false}) {
        probeSet(enabled);
        internal::probeUpdate(false);
    }
    check("shared false/true/true/false/false applies one pair", dispatches == vector<bool>({true, false}));
    dispatches.clear(); dispatchThreads.clear();
    thread worker([] { probeSet(true); check("worker getter sees requested state", probeGet()); });
    worker.join();
    check("worker exiting does not call backend", dispatches.empty());
    internal::probeUpdate(false);
    check("main-loop turn applies worker request on main thread", dispatches == vector<bool>({true}) &&
          dispatchThreads == vector<thread::id>({this_thread::get_id()}));
    internal::probeShutdown();
    internal::probeShutdown();
    check("cleanup releases once and clears getter", dispatches == vector<bool>({true, false}) && !probeGet());
    dispatches.clear();
    probeSet(true); probeSet(false); internal::probeUpdate(false);
    check("latest request wins before a frame", dispatches.empty());
    throwDispatch = true;
    probeSet(true); internal::probeUpdate(false); internal::probeShutdown();
    check("exceptions cannot cross frame or cleanup callbacks", !probeGet() && dispatches == vector<bool>({true, false}));
    throwDispatch = false;
    dispatches.clear();
    probeSet(true); internal::probeUpdate(false);
    requestDuringRelease = true;
    internal::probeShutdown();
    check("cleanup releases despite a worker request during OS release", probeGet() &&
          !keepScreenStorage.load()->applied && dispatches == vector<bool>({true, false}));
    requestDuringRelease = false;
    internal::probeUpdate(false);
    check("request received during cleanup waits for another main-loop turn", dispatches == vector<bool>({true, false, true}));
    internal::probeShutdown();
}
void linuxBehavior() {
    resetFixture();
    auto x = injectedX11();
    for (bool enabled : {false, true, true, false, false}) x.apply(enabled, fakeDisplay());
    check("Linux false/true/true/false/false makes one Suspend True/False pair", suspends == vector<bool>({true, false}));
    check("Linux checks extension and retains one library handle", queryCount == 1 && loadCount == 1);
    resetFixture();
    x = injectedX11(); dispatchX11 = &x;
    thread worker([] { probeSet(true); }); worker.join();
    check("Linux worker request has no OS work before main-loop turn", suspends.empty() && loadCount == 0);
    internal::probeUpdate(false);
    check("Linux worker request suspends on main thread", suspends == vector<bool>({true}) &&
          suspendThreads == vector<thread::id>({this_thread::get_id()}));
    internal::probeShutdown();
    check("Linux cleanup sends False before host Display closes", suspends == vector<bool>({true, false}) && !x.display);
    dispatchX11 = nullptr;
    const char* unavailableModes[] = {
        "library", "extension symbol", "version symbol", "suspend symbol", "extension",
        "version reply", "version 1.0", "version 0.x", "Display", "headless"
    };
    for (int mode = 0; mode < 10; ++mode) {
        resetFixture();
        if (mode == 0) missing = "library";
        if (mode == 1) missing = "XScreenSaverQueryExtension";
        if (mode == 2) missing = "XScreenSaverQueryVersion";
        if (mode == 3) missing = "XScreenSaverSuspend";
        if (mode == 4) extension = false;
        if (mode == 5) versionReply = false;
        if (mode == 6) minorVersion = 0;
        if (mode == 7) majorVersion = 0;
        auto unavailable = injectedX11();
        dispatchX11 = &unavailable;
        dispatchDisplay = mode == 8 ? nullptr : fakeDisplay();
        int notices = 0;
        bool pointsToSettings = false;
        auto listener = getLogger().onLog.listen([&](LogEventArgs& e) {
            ++notices;
            pointsToSettings = e.message.find("OS or desktop settings") != string::npos;
        });
        for (int repeat = 0; repeat < 2; ++repeat) {
            probeSet(true);
            internal::probeUpdate(mode == 9);
            for (int frame = 0; frame < 100; ++frame) internal::probeUpdate(mode == 9);
            probeSet(false); internal::probeUpdate(mode == 9);
        }
        char label[128];
        snprintf(label, sizeof(label), "missing %s: one settings notice, no suspension", unavailableModes[mode]);
        check(label, notices == 1 && pointsToSettings && suspends.empty());
        if (mode >= 8) check("missing Display avoids library and server queries", loadCount == 0 && queryCount == 0);
        dispatchX11 = nullptr;
        dispatchDisplay = fakeDisplay();
    }
    resetFixture(); majorVersion = 2; minorVersion = 0;
    x = injectedX11();
    check("newer extension major accepted", x.acquire(fakeDisplay()));
    x.release();
}
bool finishChild(pid_t child) {
    // Bound (deadlock detection): allow three seconds for a child with no timed OS work.
    auto deadline = chrono::steady_clock::now() + chrono::seconds(3);
    int status = 0;
    while (chrono::steady_clock::now() < deadline) {
        if (waitpid(child, &status, WNOHANG) == child) return WIFEXITED(status) && WEXITSTATUS(status) == 0;
        this_thread::sleep_for(chrono::milliseconds(10));
    }
    kill(child, SIGKILL); waitpid(child, &status, 0);
    return false;
}
void logReentry() {
    fflush(nullptr);
    pid_t child = fork();
    if (child == 0) {
        IdleX11 x = injectedX11(); dispatchX11 = &x;
        auto listener = getLogger().onLog.listen([](LogEventArgs&) {
            thread worker([] { probeSet(false); (void)probeGet(); });
            worker.join();
        });
        probeSet(true); internal::probeUpdate(true);
        internal::probeUpdate(true);
        _exit(probeGet() ? 1 : 0);
    }
    check("notice listener can join worker setter/getter without request lock", child > 0 && finishChild(child));
}
void releaseAtExit() {
    int pipefd[2];
    if (pipe(pipefd)) { check("exit pipe", false); return; }
    fflush(nullptr);
    pid_t child = fork();
    if (child == 0) {
        close(pipefd[0]); exitWriter = pipefd[1];
        resetFixture();
        IdleX11 x = injectedX11(); dispatchX11 = &x;
        // Model the host's cleanup callback at normal process exit.
        atexit([] {
            internal::probeShutdown();
            if (suspends != vector<bool>({true, false}) ||
                suspendThreads != vector<thread::id>(2, this_thread::get_id())) _Exit(2);
        });
        probeSet(true); internal::probeUpdate(false);
        exit(0);
    }
    close(pipefd[1]);
    bool exited = child > 0 && finishChild(child);
    char values[3] = {};
    int count = 0;
    while (count < 3) {
        auto n = read(pipefd[0], values + count, 3 - count);
        if (n <= 0) break;
        count += static_cast<int>(n);
    }
    close(pipefd[0]);
    check("host cleanup at exit releases Linux suspension on the main thread", exited && count == 2 && !strcmp(values, "TF"));
}
// XSS QueryInfo exposes saver activity, not a suspended flag.
struct SaverInfo {
    unsigned long window;
    int state, kind;
    unsigned long tilOrSince, idle, eventMask;
};
int realX11() {
    Display* host = XOpenDisplay(nullptr);
    Display* observer = XOpenDisplay(nullptr);
    if (!host || !observer) {
        if (host) XCloseDisplay(host);
        if (observer) XCloseDisplay(observer);
        puts("SKIP Xvfb check: no test display"); return 77;
    }
    IdleX11 x;
    if (!x.acquire(host)) {
        XCloseDisplay(host); XCloseDisplay(observer);
        puts("SKIP Xvfb check: screen-saver suspension unavailable"); return 77;
    }
    int event = 0, error = 0, major = 0, minor = 0;
    check("Xvfb observer negotiates screen saver", x.queryExtension(observer, &event, &error) &&
          x.queryVersion(observer, &major, &minor) && (major > 1 || (major == 1 && minor >= 1)));
    auto query = reinterpret_cast<int (*)(Display*, unsigned long, SaverInfo*)>(dlsym(x.library, "XScreenSaverQueryInfo"));
    if (!query) { x.release(); XCloseDisplay(host); XCloseDisplay(observer); return 1; }
    XSetScreenSaver(host, 1, 0, PreferBlanking, AllowExposures);
    XResetScreenSaver(host); XSync(host, False);
    auto state = [&] {
        SaverInfo info{};
        return query(observer, DefaultRootWindow(observer), &info) ? info.state : -1;
    };
    auto becomesActive = [&] {
        // Bound (integration timeout): allow five seconds for a one-second Xvfb saver.
        auto deadline = chrono::steady_clock::now() + chrono::seconds(5);
        while (chrono::steady_clock::now() < deadline) {
            if (state() == 1) return true;
            this_thread::sleep_for(chrono::milliseconds(50));
        }
        return false;
    };
    this_thread::sleep_for(chrono::milliseconds(2200));
    check("Xvfb saver stays off beyond timeout while suspended", state() == 0);
    x.release(); XSync(host, False);
    check("Xvfb saver starts after False", becomesActive());
    x.acquire(host); XResetScreenSaver(host); XSync(host, False);
    this_thread::sleep_for(chrono::milliseconds(2200));
    check("Xvfb saver stays off after reacquire", state() == 0);
    XCloseDisplay(host); x.display = nullptr; x.suspended = false;
    check("Xvfb saver starts after suspending connection closes", becomesActive());
    XCloseDisplay(observer);
    return failures ? 1 : 0;
}
#endif
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__) && !defined(__ANDROID__)
    if (argc == 2 && !strcmp(argv[1], "--x11-probe")) return realX11();
#ifdef TC_IDLE_CHECK_XSS_ABI
    check("private prototypes match optional public headers", true);
#else
    puts("SKIP public-header ABI assertions: optional screen-saver header unavailable");
#endif
    zeroGate();
    sharedBehavior();
    linuxBehavior();
    logReentry();
    releaseAtExit();
    check("production API links and starts false", !trussc::getKeepScreenOn());
    trussc::setKeepScreenOn(false);
    trussc::internal::updateKeepScreenOn(true);
    trussc::internal::shutdownKeepScreenOn();
#else
    (void)argc; (void)argv;
    puts("SKIP Linux keepScreenOn backend injection on this OS");
#endif
    return failures ? 1 : 0;
}
