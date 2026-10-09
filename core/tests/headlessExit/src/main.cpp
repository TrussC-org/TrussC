// #378 / #383: headless exit routing and bounded uncancellable catch-up.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <csignal>
#include <cstdio>
#include <thread>
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
#include <cerrno>
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace tc;

namespace {
int failures = 0;
void check(bool ok, const char* name) {
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) ++failures;
}

enum class Mode { Forced, Request, CancelOnce, SetupRequest, WorkerRequest,
                  Direct, SignalInt, SignalTerm, SetupSignal, ListenerForced,
                  ListenerSignal };
struct Probe {
    Mode mode = Mode::Forced;
    int code = 0;
    int updates = 0, requests = 0, exits = 0, cleanups = 0;
    int updatesAtRequest = -1;
    bool inUpdate = false;
    bool reRequested = false;  // CancelOnce: the second request is issued once
    bool setupReturned = false;
    bool checkEntry = true;
    std::string reason;
    std::thread::id mainThread = std::this_thread::get_id();
};
Probe* probe = nullptr;

void sendSignal(int value) {
#ifdef _WIN32
    // Exercise the handler directly; real console delivery needs Windows.
    headless::consoleHandler(value == SIGTERM ? CTRL_CLOSE_EVENT : CTRL_C_EVENT);
#else
    std::raise(value);
#endif
}

struct RequestWorker : Thread {
    ~RequestWorker() override { waitForThread(); }
    void threadedFunction() override { requestExitApp(); }
};

struct ExitProbeApp : App {
    EventListener listener;
    void setup() override {
        if (probe->checkEntry) {
            check(headless::running && !headless::quitRequested
                  && headless::pendingSignal == 0 && headless::deliveredSignal == 0
                  && headless::frameCount == 0
                  && internal::appExitCode() == 0 && internal::exitReason().empty()
                  && internal::exitBlockReason().empty(), "entry resets all exit state");
        }
        // Request mode deliberately has no listener.
        if (probe->mode != Mode::Request) {
            listener = events().exitRequested.listen([](ExitRequestEventArgs& args) {
                ++probe->requests;
                probe->updatesAtRequest = probe->updates;
                check(!probe->inUpdate && probe->setupReturned,
                      "request dispatched outside setup/update");
                check(std::this_thread::get_id() == probe->mainThread,
                      "request dispatched on main thread");
                if (probe->mode == Mode::CancelOnce && probe->requests == 1) {
                    args.cancel = true;
                    args.reason = "keep running";
                }
                if (probe->mode == Mode::ListenerForced) {
                    exitApp(7);
                    args.cancel = true;
                }
                if (probe->mode == Mode::ListenerSignal) {
                    sendSignal(SIGTERM);
                    args.cancel = true;
                }
            });
        }
        if (probe->mode == Mode::SetupRequest) requestExitApp();
        if (probe->mode == Mode::SetupSignal) sendSignal(SIGTERM);
        probe->setupReturned = true;
    }
    void update() override {
        probe->inUpdate = true;
        ++probe->updates;
        if (probe->updates > 200) {
            check(false, "exit completed before safety step cap");
            requestExit();
        } else if (probe->updates == 1) {
            switch (probe->mode) {
                case Mode::Forced: exitApp(probe->code); break;
                case Mode::Request:
                case Mode::CancelOnce:
                case Mode::ListenerForced:
                case Mode::ListenerSignal: requestExitApp(); break;
                case Mode::WorkerRequest: {
                    RequestWorker worker;
                    worker.startThread();
                    worker.waitForThread(false);
                    break;
                }
                case Mode::Direct: requestExit(); break;
                case Mode::SignalInt: sendSignal(SIGINT); break;
                case Mode::SignalTerm: sendSignal(SIGTERM); break;
                case Mode::SetupRequest:
                case Mode::SetupSignal: break;
            }
        } else if (probe->mode == Mode::CancelOnce && probe->requests == 1 && !probe->reRequested) {
            // Only once: a loaded runner can run several catch-up steps in one
            // pass, and the request below stays pending until the pass ends.
            check(internal::exitReason().empty() && !headless::quitRequested,
                  "veto clears request and reason");
            probe->reRequested = true;
            requestExitApp();
        }
        probe->inUpdate = false;
    }
    void exit() override {
        ++probe->exits;
        probe->reason = internal::exitReason();
    }
    void cleanup() override {
        ++probe->cleanups;
        check(probe->exits == 1, "exit precedes cleanup");
        check(internal::exitReason() == probe->reason, "cleanup preserves exit reason");
        if (probe->reason == "sigint" || probe->reason == "sigterm") {
            check(headless::pendingSignal == (probe->reason == "sigterm" ? SIGTERM : SIGINT),
                  "signal remains pending through cleanup, including after listener veto");
        }
    }
};

void testRunner(Mode mode, int code, const char* reason) {
    Probe result;
    result.mode = mode;
    result.code = code;
    probe = &result;
    const int returned = runHeadlessApp<ExitProbeApp>(HeadlessSettings().setFps(1000));
    check(returned == (mode == Mode::ListenerForced ? 7 : code), "runner returns app exit code");
    check(result.exits == 1 && result.cleanups == 1, "exit and cleanup run once");
    check(result.reason == reason, "runner records expected exit reason");
    check(!headless::isActive(), "runner leaves headless mode");
    if (mode == Mode::Forced || mode == Mode::Direct
            || mode == Mode::SignalInt || mode == Mode::SignalTerm) {
        check(result.updates == 1 && result.requests == 0,
              "uncancellable exit stops after one update without an event");
    }
    if (mode == Mode::SetupSignal)
        check(result.updates == 0 && result.requests == 0, "setup signal survives until loop poll");
    if (mode == Mode::CancelOnce)
        check(result.requests == 2, "cancel first request, accept second");
    if (mode == Mode::SetupRequest || mode == Mode::WorkerRequest)
        check(result.requests == 1, "setup/worker request delivered once");
}

void resetPassState() {
    headless::active = true;
    headless::running = true;
    headless::quitRequested = false;
    headless::pendingSignal = 0;
    headless::deliveredSignal = 0;
    headless::frameCount = 0;
    internal::appExitCode() = 0;
    internal::clearExitReason();
    internal::setExitBlockReason("");
}

void testCatchUp(Mode mode, int expectedSteps) {
    resetPassState();
    Probe result;
    result.mode = mode;
    probe = &result;
    ExitProbeApp app;
    app.setup();
    auto& ctx = internal::mainWindowContext();
    const int rateIndex = ctx.rateIndex;
    double accumulator = 0.0;
    // Binary-exact interval and elapsed time: exactly 100 owed steps,
    // independent of CPU load, wall time or sleep resolution.
    internal::runHeadlessUpdatePass(app, ctx, accumulator, 100.0 / 128.0, 1.0 / 128.0);
    check(result.updates == expectedSteps && headless::frameCount == unsigned(expectedSteps),
          "100 due steps: actual update count matches stop kind");
    check(ctx.rateSteps[rateIndex] == expectedSteps, "measured rate counts only completed steps");
    if (mode == Mode::CancelOnce) {
        check(result.requests == 1 && result.updatesAtRequest == 100,
              "cancellable request waits until all 100 steps finish");
        check(headless::running && !headless::quitRequested && internal::exitReason().empty(),
              "veto leaves loop running with cleared reason/flag");
    } else {
        check(result.requests == 0, "catch-up pass did not notify an installed listener");
        if (mode == Mode::Request)
            check(!headless::running && internal::exitReason() == "request-exit-app",
                  "request without listener stops after all 100 steps");
    }
    headless::active = false;
}

void testSetupFirstPass() {
    resetPassState();
    Probe result;
    result.mode = Mode::SetupRequest;
    probe = &result;
    ExitProbeApp app;
    app.setup();
    double accumulator = 0.0;
    internal::runHeadlessUpdatePass(app, internal::mainWindowContext(), accumulator, 0.0, 1.0);
    check(result.requests == 1 && result.updates == 0 && !headless::running,
          "setup request is handled on first pass even with no due steps");
    headless::active = false;
}

void testSignalHandler() {
    resetPassState();
    headless::installSignalHandlers();
    internal::setExitReason("before-signal");
    sendSignal(SIGTERM);
    check(headless::pendingSignal == SIGTERM && headless::running
          && internal::exitReason() == "before-signal", "handler only stores pending signal");
    headless::installSignalHandlers();
    check(headless::pendingSignal == SIGTERM, "installing handlers preserves a pending signal");
    headless::pollExitSignal();
    check(!headless::running && internal::exitReason() == "sigterm",
          "main-thread poll records signal reason and stops");
    check(headless::pendingSignal == SIGTERM && headless::deliveredSignal == SIGTERM,
          "poll acknowledges delivery without clearing pending signal");
    internal::setExitReason("after-delivery");
    headless::pollExitSignal();
    check(headless::pendingSignal == SIGTERM && internal::exitReason() == "after-delivery",
          "poll delivers each pending signal only once");
    headless::active = false;
}

void testSignalEscalation() {
#if !defined(_WIN32) && !defined(__EMSCRIPTEN__)
    // Run before any runner starts worker threads. Both same-signal and
    // mixed-signal pairs must escalate, before and after main-thread delivery.
    std::fflush(nullptr);
    for (bool delivered : {false, true}) {
        for (int first : {SIGINT, SIGTERM}) {
            for (int second : {SIGINT, SIGTERM}) {
                const pid_t child = fork();
                if (child == 0) {
                    resetPassState();
                    headless::installSignalHandlers();
                    std::raise(first);
                    if (delivered) headless::pollExitSignal();
                    std::raise(second);
                    _exit(1); // Reached only if escalation is broken.
                }
                int status = 0;
                pid_t waited = -1;
                if (child > 0) {
                    do { waited = waitpid(child, &status, 0); }
                    while (waited < 0 && errno == EINTR);
                }
                char name[128];
                std::snprintf(name, sizeof(name),
                    "signals %d then %d %s poll exit with %d", first, second,
                    delivered ? "after" : "before", 128 + second);
                check(child > 0 && waited == child && WIFEXITED(status)
                      && WEXITSTATUS(status) == 128 + second, name);
            }
        }
    }
#elif defined(_WIN32)
    for (DWORD control : {DWORD(CTRL_C_EVENT), DWORD(CTRL_BREAK_EVENT)}) {
        resetPassState();
        internal::HeadlessConsoleUtf8 consoleUtf8;
        check(headless::consoleHandler(control) == TRUE && headless::pendingSignal == SIGINT,
              "first console interrupt publishes signal and is handled");
        headless::pollExitSignal();
        check(headless::consoleHandler(control) == FALSE,
              "second console interrupt after poll falls through to default handler");
        if (consoleUtf8.outputSet)
            check(GetConsoleOutputCP() == consoleUtf8.originalOutput,
                  "second console interrupt restores original output code page");
        if (consoleUtf8.inputSet)
            check(GetConsoleCP() == consoleUtf8.originalInput,
                  "second console interrupt restores original input code page");
    }
    check(internal::headlessRestoreConsoleCP.output == 0
          && internal::headlessRestoreConsoleCP.input == 0,
          "console guard clears restore code pages");
    headless::active = false;
#endif
}
} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    testSignalEscalation();
    testRunner(Mode::Forced, 0, "exit-app");
    testRunner(Mode::Forced, 3, "exit-app");
    // Seed stale state after a nonzero exit; the next run must reset it all.
    headless::quitRequested = true;
    headless::pendingSignal = SIGTERM;
    headless::deliveredSignal = SIGTERM;
    internal::setExitBlockReason("stale veto");
    testRunner(Mode::Request, 0, "request-exit-app");
    testRunner(Mode::CancelOnce, 0, "request-exit-app");
    testRunner(Mode::SetupRequest, 0, "request-exit-app");
    testRunner(Mode::WorkerRequest, 0, "request-exit-app");
    testRunner(Mode::ListenerForced, 0, "exit-app");
    testRunner(Mode::ListenerSignal, 0, "sigterm");
    testRunner(Mode::Direct, 0, "");
    testRunner(Mode::SignalInt, 0, "sigint");
    testRunner(Mode::SignalTerm, 0, "sigterm");
    testRunner(Mode::SetupSignal, 0, "sigterm");
    testSignalHandler();
    const int previousCap = getMaxUpdateSteps();
    setMaxUpdateSteps(100);
    testCatchUp(Mode::Direct, 1);
    testCatchUp(Mode::Forced, 1);
    testCatchUp(Mode::SignalInt, 1);
    testCatchUp(Mode::SignalTerm, 1);
    testCatchUp(Mode::Request, 100);
    testCatchUp(Mode::CancelOnce, 100);
    testSetupFirstPass();
    setMaxUpdateSteps(previousCap);
    std::printf("headlessExit: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
