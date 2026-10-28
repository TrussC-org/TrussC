// A macOS session end (logout) handled while a client callback is paused must
// end the app at once with a minimal ending: every window's App::exit (main
// first, each in its own window context, BeforeApp/App exit listeners only)
// and audio stop, but no AfterApp hooks, no GPU, window or App teardown and no
// atexit/static destructors (process exit via _exit with the app code).
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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

extern char** environ;
using namespace tc;

extern "C" void sessionEndDispatchLogout();
extern "C" void sessionEndAfter(double seconds, void (*fn)());
extern "C" void sessionEndSpin(double seconds);

namespace {

// ---- child: one scenario -------------------------------------------------
bool busyMode = false;
int childExitCode = 7;

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

struct SecondaryApp : App {
    CoreEvents* ownEvents = nullptr;
    void setup() override { ownEvents = &events(); }
    void exit() override { reportExit("secondary", ownEvents, getWindow() != nullptr); }
    void cleanup() override { say("secondary App::cleanup"); }
};

struct ChildApp : App {
    int frames = 0;
    std::shared_ptr<Window> secondary;
    EventListener logListener;
    EventListener afterAppHook;
    CoreEvents* ownEvents = nullptr;
    void setup() override {
        ownEvents = &events();
        // A protected framework hook (like an addon's shutdown): runs on the
        // normal exit, skipped by the session end under a paused callback.
        afterAppHook = trussc::internal::FrameworkEventAccess::listen(events().exit,
            [] { say("AfterApp exit hook"); }, EventPriority::AfterApp);
        trussc::internal::appExitCode() = childExitCode;
        std::atexit(atexitMarker);
        static StaticDestructorMarker marker;   // destructor registered after main's statics
        (void)marker;
        AudioEngine::getInstance().init();
        std::printf("sessionEndBusy child: audio initialized=%d\n", AudioEngine::getInstance().isInitialized());
        std::fflush(stdout);
        WindowSettings ws;
        ws.setSize(160, 100).setTitle("sessionEndBusy secondary");
        secondary = createWindow(ws);
        if (secondary) secondary->setApp(std::make_shared<SecondaryApp>());
        logListener = getLogger().onLog.listen([](LogEventArgs& e) {
            if (e.message.find("exit: clean") == std::string::npos) return;
            std::printf("sessionEndBusy child: final exit line gpu valid=%d audio initialized=%d\n",
                        sg_isvalid(), AudioEngine::getInstance().isInitialized());
            std::fflush(stdout);
        });
        if (!busyMode) sessionEndAfter(0.8, [] { sessionEndDispatchLogout(); });
    }
    void update() override {
        if (!busyMode || ++frames != 20) return;
        // Paused callback: the logout arrives while update() spins the run loop.
        sessionEndAfter(0.02, [] { sessionEndDispatchLogout(); });
        sessionEndSpin(0.15);
        say("returned into update");
    }
    void exit() override { reportExit("main", ownEvents, getWindow() != nullptr); }
    void cleanup() override { say("main App::cleanup"); }
};

int runChild(const std::string& scenario) {
    busyMode = scenario.rfind("busy", 0) == 0;
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

// Runs this binary with one scenario argument; returns its exit status (-1 on
// launch failure, -2 on a 60 s timeout) and collects stdout+stderr.
int launch(const char* scenario, std::string& output) {
    char path[4096];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) != 0) return -1;
    int fds[2];
    if (pipe(fds) != 0) return -1;
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 1);
    posix_spawn_file_actions_adddup2(&actions, fds[1], 2);
    posix_spawn_file_actions_addclose(&actions, fds[0]);
    char* argv[] = { path, const_cast<char*>(scenario), nullptr };
    pid_t pid = 0;
    const int spawned = posix_spawn(&pid, path, &actions, nullptr, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    close(fds[1]);
    if (spawned != 0) { close(fds[0]); return -1; }
    const double deadline = 60.0;
    double waited = 0.0;
    char buffer[4096];
    bool timedOut = false;
    for (;;) {
        pollfd p = { fds[0], POLLIN, 0 };
        const int ready = poll(&p, 1, 100);
        if (ready > 0) {
            const ssize_t n = read(fds[0], buffer, sizeof(buffer));
            if (n <= 0) break;
            output.append(buffer, (size_t)n);
        } else {
            waited += 0.1;
            if (waited > deadline) { timedOut = true; kill(pid, SIGKILL); break; }
        }
    }
    close(fds[0]);
    int status = 0;
    waitpid(pid, &status, 0);
    if (timedOut) return -2;
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

void checkBusy(const char* scenario, int expectedStatus) {
    std::string out;
    const int status = launch(scenario, out);
    std::printf("%s", out.c_str());
    const std::string label = std::string("busy session end, code ") + std::to_string(expectedStatus);
    check(label + ": exit status is the app code", status == expectedStatus);
    check(label + ": main App::exit exactly once", countOf(out, "child: main App::exit") == 1);
    check(label + ": secondary App::exit exactly once", countOf(out, "child: secondary App::exit") == 1);
    const size_t mainAt = out.find("child: main App::exit");
    const size_t secondaryAt = out.find("child: secondary App::exit");
    check(label + ": main App::exit before secondary App::exit",
          mainAt != std::string::npos && secondaryAt != std::string::npos && mainAt < secondaryAt);
    check(label + ": main App::exit runs in main's context (width 200, own events())",
          out.find("main App::exit width=200 own-events=1 attached=1") != std::string::npos);
    check(label + ": secondary App::exit runs in its own context (width 160, own events())",
          out.find("secondary App::exit width=160 own-events=1 attached=1") != std::string::npos);
    check(label + ": protected AfterApp exit hook skipped", out.find("AfterApp exit hook") == std::string::npos);
    check(label + ": GPU not torn down, audio stopped at the final exit line",
          out.find("final exit line gpu valid=1 audio initialized=0") != std::string::npos);
    check(label + ": no window or App teardown (Apps stay attached, no App::cleanup)",
          out.find("App::cleanup") == std::string::npos && out.find("attached=0") == std::string::npos);
    check(label + ": no atexit handler or static destructor ran",
          out.find("atexit marker") == std::string::npos && out.find("static destructor marker") == std::string::npos);
    check(label + ": control never returned into the paused callback",
          out.find("returned into update") == std::string::npos);
}

void checkNotBusy() {
    std::string out;
    const int status = launch("notbusy", out);
    std::printf("%s", out.c_str());
    const std::string label = "not-busy session end, code 7";
    check(label + ": exit status is the app code", status == 7);
    check(label + ": full cleanup tears down both Apps (exit once each, then cleanup)",
          countOf(out, "child: main App::exit") == 1 && countOf(out, "child: secondary App::exit") == 1 &&
          out.find("main App::cleanup") != std::string::npos && out.find("secondary App::cleanup") != std::string::npos);
    check(label + ": protected AfterApp exit hook runs", countOf(out, "AfterApp exit hook") == 1);
    check(label + ": full cleanup tears down the GPU (atexit sees it gone)",
          out.find("atexit marker (gpu valid=0)") != std::string::npos);
    check(label + ": process exit handlers run (std::exit)",
          out.find("static destructor marker") != std::string::npos);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) return runChild(argv[1]);
    checkBusy("busy", 7);
    checkBusy("busy0", 0);
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
