// A macOS session end (logout) handled while a client callback is paused must
// end the app at once with a minimal ending. From the moment the session-end
// path starts until _exit the framework is frozen: only every window's App::exit
// (main first, then the others newest first, each in its own window context,
// BeforeApp/App exit listeners only) and audio stop run. No AfterApp hooks, no
// tick, event, App attachment, window creation, GPU, window or App teardown,
// and no atexit/static destructors (process exit via _exit with the app code).
// Outside callbacks the normal full cleanup runs, AfterApp hooks included.
//
// Run without arguments (as build_all does) it launches itself once per
// scenario and checks each child's output and exit status. A child scenario is
// a real TrussC launch that dispatches a logout quit Apple event in-process.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if defined(__APPLE__) && TARGET_OS_OSX

#include <TrussC.h>
#include <mach-o/dyld.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

extern char** environ;
using namespace tc;

extern "C" void sessionEndDispatchLogout();
extern "C" void sessionEndAfter(double seconds, void (*fn)());
extern "C" void sessionEndSpin(double seconds);
extern "C" bool sessionEndSendKey(const char* title);
extern "C" int sessionEndWindowCount();

namespace {

// ---- child: one scenario -------------------------------------------------
std::string scenario;
int childExitCode = 7;
bool frozen = false;   // set when main's App::exit starts (the session end is running)

void say(const char* line) {
    std::printf("sessionEndBusy child: %s\n", line);
    std::fflush(stdout);
}

void atexitMarker() {
    std::printf("sessionEndBusy child: atexit marker (gpu valid=%d)\n", sg_isvalid());
    std::fflush(stdout);
}

struct StaticDestructorMarker {
    ~StaticDestructorMarker() { say("static destructor marker"); }
};

// One line per App::exit: which window context is selected (its logical
// width, whether events() is the App's own window's) and whether the App is
// still attached (normal teardown detaches it before exit).
void reportExit(const char* who, CoreEvents* ownEvents, bool attached) {
    std::printf("sessionEndBusy child: %s App::exit width=%d own-events=%d attached=%d\n", who,
                getWindowWidth(), &events() == ownEvents, attached);
    std::fflush(stdout);
}

// A protected framework hook (like an addon's shutdown): runs on the normal
// exit, skipped by the session end under a paused callback.
EventListener afterAppHook(const std::string& who) {
    return trussc::internal::FrameworkEventAccess::listen(events().exit,
        [who] { std::printf("sessionEndBusy child: %s AfterApp exit hook\n", who.c_str()); std::fflush(stdout); },
        EventPriority::AfterApp);
}

struct ChildApp;
ChildApp* owner = nullptr;
void dropWindowA();

struct SecondaryApp : App {
    std::string who;
    bool busyHere = false;   // this window's update() is the paused callback
    int frames = 0;
    CoreEvents* ownEvents = nullptr;
    EventListener afterApp;
    explicit SecondaryApp(std::string name, bool busy) : who(std::move(name)), busyHere(busy) {}
    void setup() override {
        ownEvents = &events();
        afterApp = afterAppHook(who);
    }
    void update() override {
        if (frozen) {
            std::printf("sessionEndBusy child: %s update while frozen\n", who.c_str());
            std::fflush(stdout);
        }
        if (!busyHere || ++frames != 20) return;
        sessionEndAfter(0.02, [] { sessionEndDispatchLogout(); });
        sessionEndSpin(0.15);
        say("returned into update");
    }
    void keyPressed(int) override {
        if (!frozen) return;
        std::printf("sessionEndBusy child: %s key while frozen\n", who.c_str());
        std::fflush(stdout);
    }
    void exit() override {
        reportExit(who.c_str(), ownEvents, getWindow() != nullptr);
        // "hooks": A's own exit drops A's last owner.
        if (scenario == "hooks" && who == "A") dropWindowA();
    }
    void cleanup() override {
        std::printf("sessionEndBusy child: %s App::cleanup\n", who.c_str());
        std::fflush(stdout);
    }
};

struct ChildApp : App {
    int frames = 0;
    // Window ownership as in an app: main's App holds A, then B.
    std::shared_ptr<Window> a;
    std::shared_ptr<Window> b;
    std::shared_ptr<Window> c;   // "hooks": a window requested inside an exit hook
    EventListener cExit;
    EventListener logListener;
    EventListener afterApp;
    CoreEvents* ownEvents = nullptr;
    void setup() override {
        owner = this;
        ownEvents = &events();
        afterApp = afterAppHook("main");
        trussc::internal::appExitCode() = childExitCode;
        std::atexit(atexitMarker);
        static StaticDestructorMarker marker;   // destructor registered after main's statics
        (void)marker;
        AudioEngine::getInstance().init();
        std::printf("sessionEndBusy child: audio initialized=%d\n", AudioEngine::getInstance().isInitialized());
        std::fflush(stdout);
        WindowSettings wa;
        wa.setSize(160, 100).setTitle("sessionEndBusy A");
        a = createWindow(wa);
        if (a) a->setApp(std::make_shared<SecondaryApp>("A", scenario == "busyA"));
        WindowSettings wb;
        wb.setSize(180, 110).setTitle("sessionEndBusy B");
        b = createWindow(wb);
        if (b) b->setApp(std::make_shared<SecondaryApp>("B", false));
        logListener = getLogger().onLog.listen([](LogEventArgs& e) {
            if (e.message.find("exit: clean") == std::string::npos) return;
            // getWindowWidth() reads the window context the session end restored.
            std::printf("sessionEndBusy child: final exit line gpu valid=%d audio initialized=%d context width=%d\n",
                        sg_isvalid(), AudioEngine::getInstance().isInitialized(), getWindowWidth());
            std::fflush(stdout);
        });
        if (scenario == "notbusy") sessionEndAfter(0.8, [] { sessionEndDispatchLogout(); });
    }
    void update() override {
        const bool busyHere = scenario == "busy" || scenario == "busy0" || scenario == "hooks";
        if (!busyHere || ++frames != 20) return;
        // Paused callback: the logout arrives while update() spins the run loop.
        sessionEndAfter(0.02, [] { sessionEndDispatchLogout(); });
        sessionEndSpin(0.15);
        say("returned into update");
    }
    void exit() override {
        frozen = true;
        reportExit("main", ownEvents, getWindow() != nullptr);
        if (scenario == "busyA") {
            // The paused callback is A's: dropping A here must not free the
            // context the session end later restores.
            a.reset();
            say("main exit dropped A");
        }
        if (scenario != "hooks") return;
        a->setApp(nullptr);   // a detach request, applied only at a frame boundary
        trussc::internal::applyPendingAppForTests(*a);   // a frame boundary reached while frozen
        const int before = sessionEndWindowCount();
        WindowSettings wc;
        wc.setSize(120, 80).setTitle("sessionEndBusy C");
        c = createWindow(wc);
        const bool firstNull = c == nullptr;
        if (c) cExit = c->events().exit.listen([] { say("C exit listener"); });
        const bool secondNull = createWindow(wc) == nullptr;
        std::printf("sessionEndBusy child: hook createWindow null=%d,%d native windows %d->%d\n",
                    firstNull, secondNull, before, sessionEndWindowCount());
        sessionEndDispatchLogout();   // a second session end while the first is running
        std::printf("sessionEndBusy child: hook key sent=%d\n", sessionEndSendKey("sessionEndBusy B"));
        sessionEndSpin(0.3);          // other windows' tick sources keep firing meanwhile
        say("main hook done");
        std::fflush(stdout);
    }
    void cleanup() override { say("main App::cleanup"); }
};

void dropWindowA() {
    if (owner) owner->a.reset();
    say("A exit dropped A");
}

// Supervision probes (no TrussC launch): a child that keeps printing slowly,
// one that floods output, one that closes its output and keeps running.
int runProbe(const std::string& kind) {
    const auto start = std::chrono::steady_clock::now();
    auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
    if (kind == "trickle") {
        while (elapsed() < 20.0) { std::printf("trickle\n"); std::fflush(stdout); usleep(50000); }
    } else if (kind == "flood") {
        std::string chunk(64 * 1024, 'x');
        chunk.back() = '\n';
        while (elapsed() < 10.0) { std::fwrite(chunk.data(), 1, chunk.size(), stdout); std::fflush(stdout); usleep(10000); }
    } else if (kind == "linger") {
        std::fclose(stdout);
        std::fclose(stderr);
        while (elapsed() < 20.0) usleep(100000);
    }
    return 0;
}

int runChild() {
    if (scenario == "trickle" || scenario == "flood" || scenario == "linger") return runProbe(scenario);
    childExitCode = scenario == "busy0" ? 0 : 7;
    WindowSettings s;
    s.setSize(200, 120).setTitle("sessionEndBusy");
    return runApp<ChildApp>(s);
}

// ---- parent: launch and check each scenario --------------------------------
int failures = 0;

void check(const std::string& label, bool ok) {
    std::printf("sessionEndBusy %s: %s\n", label.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}

int countOf(const std::string& text, const std::string& needle) {
    int n = 0;
    for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size())) ++n;
    return n;
}

bool has(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

enum class Outcome { Exited, Failed, TimedOut, OutputCapped };
struct Launch {
    Outcome outcome = Outcome::Failed;
    int status = -1;        // exit status when Exited
    double seconds = 0.0;   // wall time until the child was reaped
};

constexpr size_t outputCap = 1024 * 1024;

double secondsSince(std::chrono::steady_clock::time_point t) {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t).count();
}

// Runs this binary with one scenario argument and collects stdout+stderr.
Launch launch(const char* which, std::string& output, double deadlineSeconds) {
    Launch result;
    const auto start = std::chrono::steady_clock::now();
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return result;
    int fds[2];
    if (pipe(fds) != 0) return result;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    posix_spawn_file_actions_addclose(&actions, fds[1]);   // the child writes only through 1 and 2
    char* argv[] = { path, const_cast<char*>(which), nullptr };
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, path, &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (spawned != 0) { close(fds[0]); return result; }
    bool timedOut = false;
    bool capped = false;
    char buffer[65536];
    for (;;) {
        // Bound (deadline): one monotonic deadline for the whole child, whether or not it prints.
        const double remaining = deadlineSeconds - secondsSince(start);
        if (remaining <= 0.0) { timedOut = true; break; }
        pollfd p = { fds[0], POLLIN, 0 };
        const int ready = poll(&p, 1, remaining < 0.1 ? (int)(remaining * 1000) + 1 : 100);
        if (ready <= 0) continue;
        const ssize_t n = read(fds[0], buffer, sizeof(buffer));
        if (n <= 0) break;
        // Bound (output): at most outputCap bytes are kept; a child past it is stopped.
        if (output.size() + (size_t)n > outputCap) {
            output.append(buffer, outputCap - output.size());
            capped = true;
            break;
        }
        output.append(buffer, (size_t)n);
    }
    close(fds[0]);
    int status = 0;
    bool reaped = false;
    if (!timedOut && !capped) {
        // Bound (deadline): the reap after end of output waits only for the deadline's remainder.
        while (secondsSince(start) < deadlineSeconds) {
            const pid_t r = waitpid(pid, &status, WNOHANG);
            if (r == pid || r < 0) { reaped = r == pid; break; }
            poll(nullptr, 0, 10);
        }
        if (!reaped && secondsSince(start) >= deadlineSeconds) timedOut = true;
    }
    if (!reaped) {
        kill(pid, SIGKILL);
        // Bound (SIGKILL): the child cannot catch or ignore it; this reap follows its termination.
        waitpid(pid, &status, 0);
    }
    result.seconds = secondsSince(start);
    if (capped) result.outcome = Outcome::OutputCapped;
    else if (timedOut) result.outcome = Outcome::TimedOut;
    else if (WIFEXITED(status)) { result.outcome = Outcome::Exited; result.status = WEXITSTATUS(status); }
    return result;
}

std::string run(const char* which) {
    std::string out;
    const Launch l = launch(which, out, 60.0);
    std::printf("%s", out.c_str());
    if (l.outcome != Outcome::Exited) out += "\n[parent] child did not exit normally\n";
    out += "\n[parent] status=" + std::to_string(l.status) + "\n";
    return out;
}

// Order of the "<who> App::exit" lines.
std::string exitOrder(const std::string& out) {
    std::string order;
    size_t at = 0;
    const std::string tag = "child: ";
    while ((at = out.find(" App::exit width=", at)) != std::string::npos) {
        const size_t lineStart = out.rfind(tag, at);
        if (lineStart != std::string::npos) {
            if (!order.empty()) order += ",";
            order += out.substr(lineStart + tag.size(), at - lineStart - tag.size());
        }
        at += 1;
    }
    return order;
}

void checkFrozenExits(const std::string& label, const std::string& out) {
    check(label + ": every App::exit exactly once, main then newest first (main,B,A)", exitOrder(out) == "main,B,A");
    check(label + ": each App::exit runs in its own context with the App attached",
          has(out, "main App::exit width=200 own-events=1 attached=1") &&
          has(out, "B App::exit width=180 own-events=1 attached=1") &&
          has(out, "A App::exit width=160 own-events=1 attached=1"));
    check(label + ": protected AfterApp exit hooks skipped", !has(out, "AfterApp exit hook"));
    check(label + ": no window or App teardown (no App::cleanup)", !has(out, "App::cleanup"));
    check(label + ": GPU not torn down, audio stopped at the final exit line",
          has(out, "final exit line gpu valid=1 audio initialized=0"));
    check(label + ": no atexit handler or static destructor ran",
          !has(out, "atexit marker") && !has(out, "static destructor marker"));
    check(label + ": control never returned into the paused callback", !has(out, "returned into update"));
}

void checkBusy(const char* which, int expectedStatus) {
    const std::string out = run(which);
    const std::string label = std::string("busy session end, code ") + std::to_string(expectedStatus);
    check(label + ": exit status is the app code", has(out, "[parent] status=" + std::to_string(expectedStatus) + "\n"));
    checkFrozenExits(label, out);
}

void checkHooks() {
    const std::string out = run("hooks");
    const std::string label = "busy session end, exit hooks that act";
    check(label + ": exit status is the app code", has(out, "[parent] status=7\n"));
    checkFrozenExits(label, out);
    check(label + ": every hook ran to its end (a second logout inside a hook ends nothing)",
          has(out, "main hook done") && has(out, "A exit dropped A"));
    check(label + ": no tick while frozen", !has(out, "update while frozen"));
    check(label + ": no event delivered while frozen", has(out, "hook key sent=1") && !has(out, "key while frozen"));
    check(label + ": createWindow refused, no native window, logged once",
          has(out, "hook createWindow null=1,1") && countOf(out, "createWindow(): refused, the session is ending") == 1);
    check(label + ": only windows open before the first notification are notified", !has(out, "C exit listener"));
    {
        const size_t at = out.find("native windows ");
        bool same = false;
        if (at != std::string::npos) {
            int before = -1, after = -2;
            same = std::sscanf(out.c_str() + at, "native windows %d->%d", &before, &after) == 2 && before == after;
        }
        check(label + ": native window count unchanged by createWindow", same);
    }
}

void checkBusyA() {
    const std::string out = run("busyA");
    const std::string label = "busy session end in A's callback, main's exit drops A";
    check(label + ": exit status is the app code", has(out, "[parent] status=7\n"));
    check(label + ": main's exit dropped A", has(out, "main exit dropped A"));
    check(label + ": the interrupted context (A's) is still valid at the final exit line",
          has(out, "audio initialized=0 context width=160"));
    checkFrozenExits(label, out);
}

void checkNotBusy() {
    const std::string out = run("notbusy");
    const std::string label = "not-busy session end, code 7";
    check(label + ": exit status is the app code", has(out, "[parent] status=7\n"));
    // Normal shutdown follows App ownership: main's App holds A then B.
    std::printf("sessionEndBusy not-busy exit order: %s\n", exitOrder(out).c_str());
    check(label + ": normal order follows ownership (main,B,A)", exitOrder(out) == "main,B,A");
    check(label + ": full cleanup detaches and cleans up every App",
          has(out, "main App::exit width=200 own-events=1 attached=0") && has(out, "main App::cleanup") &&
          has(out, "A App::cleanup") && has(out, "B App::cleanup") && !has(out, "attached=1"));
    check(label + ": protected AfterApp exit hooks run once each",
          countOf(out, "main AfterApp exit hook") == 1 && countOf(out, "A AfterApp exit hook") == 1 &&
          countOf(out, "B AfterApp exit hook") == 1);
    check(label + ": full cleanup tears down the GPU (atexit sees it gone)", has(out, "atexit marker (gpu valid=0)"));
    check(label + ": process exit handlers run (std::exit)", has(out, "static destructor marker"));
}

void checkSupervision() {
    std::string out;
    Launch l = launch("trickle", out, 3.0);
    check("supervision: a child that keeps printing is stopped at the deadline",
          l.outcome == Outcome::TimedOut && l.seconds < 5.0);
    out.clear();
    l = launch("flood", out, 30.0);
    check("supervision: a child past the output cap is stopped",
          l.outcome == Outcome::OutputCapped && out.size() == outputCap && l.seconds < 5.0);
    out.clear();
    l = launch("linger", out, 3.0);
    check("supervision: a child that closes its output and keeps running is reaped by the deadline",
          l.outcome == Outcome::TimedOut && l.seconds < 5.0);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) { scenario = argv[1]; return runChild(); }
    checkSupervision();
    checkBusy("busy", 7);
    checkBusy("busy0", 0);
    checkHooks();
    checkBusyA();
    checkNotBusy();
    std::printf("sessionEndBusy: %s\n", failures ? "FAILED" : "all checks passed");
    return failures ? 1 : 0;
}

#else

#include <cstdio>

int main() {
    std::puts("SKIP: sessionEndBusy covers the macOS session-end path");
    return 0;
}

#endif
