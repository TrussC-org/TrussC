// A macOS session end (logout) handled while a client callback is paused must
// end the app at once with a minimal ending: App::exit and audio stop, but no
// GPU, window or App teardown and no atexit/static destructors (process exit
// via _exit with the app code). Outside callbacks the normal full cleanup runs.
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

struct SecondaryApp : App {
    void exit() override { say("secondary App::exit"); }
};

struct ChildApp : App {
    int frames = 0;
    std::shared_ptr<Window> secondary;
    EventListener logListener;
    void setup() override {
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
    void exit() override { say("App::exit"); }
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
    check(label + ": App::exit exactly once", countOf(out, "child: App::exit") == 1);
    check(label + ": GPU not torn down, audio stopped at the final exit line",
          out.find("final exit line gpu valid=1 audio initialized=0") != std::string::npos);
    check(label + ": no window or App teardown",
          out.find("secondary App::exit") == std::string::npos);
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
    check(label + ": full cleanup tears down the secondary window's App", out.find("secondary App::exit") != std::string::npos);
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
