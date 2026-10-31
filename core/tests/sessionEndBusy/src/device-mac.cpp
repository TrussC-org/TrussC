// Explicit device pass: in-process logout Apple event, idle and busy-no-exit.
#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif
#if defined(__APPLE__) && TARGET_OS_OSX
#include <TrussC.h>
#include <mach-o/dyld.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>
extern char** environ;
extern "C" void sessionEndDispatchLogout();
extern "C" void sessionEndAfter(double, void (*)());
using namespace tc;
namespace {
bool busyChild = false;
void marker() { std::puts("STATIC-EXIT"); std::fflush(stdout); }
struct DeviceApp : App {
    bool fired = false;
    void setup() override {
        runtime().setLogoutTimeout(3);
        if (!busyChild) sessionEndAfter(0.1, sessionEndDispatchLogout);
    }
    void update() override {
        if (busyChild && !fired) {
            fired = true;
            sessionEndDispatchLogout();
            std::puts("RESUMED-APP"); std::fflush(stdout);
        }
    }
    void exit() override { std::puts("APP-EXIT"); std::fflush(stdout); }
    void cleanup() override { std::puts("APP-CLEANUP"); std::fflush(stdout); }
};
}
int runSessionEndDeviceProbe(int argc, char** argv) {
    if (argc > 1) {
        busyChild = std::string(argv[1]) == "busy";
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::atexit(marker);
        WindowSettings settings; settings.title = "sessionEndBusy";
        return runApp<DeviceApp>(settings);
    }
    char executable[4096]; uint32_t size = sizeof(executable);
    if (_NSGetExecutablePath(executable, &size)) return 2;
    int failures = 0;
    for (const char* scenario : {"idle", "busy"}) {
        int pipefd[2]; if (pipe(pipefd)) return 2;
        posix_spawn_file_actions_t actions; posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, pipefd[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, pipefd[0]);
        posix_spawn_file_actions_addclose(&actions, pipefd[1]);
        char* args[]{executable, const_cast<char*>("--mac-device"), const_cast<char*>(scenario), nullptr};
        pid_t child; const int error = posix_spawn(&child, executable, &actions, nullptr, args, environ);
        posix_spawn_file_actions_destroy(&actions); close(pipefd[1]);
        if (error) { close(pipefd[0]); return 2; }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        std::string output; char buffer[1024];
        bool eof = false, reaped = false, timedOut = false;
        int status = 0;
        while (!eof || !reaped) {
            const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now()).count();
            if (remaining <= 0) { timedOut = true; break; }
            pollfd descriptor{eof ? -1 : pipefd[0], POLLIN, 0};
            const int ready = poll(&descriptor, 1, int(std::min<int64_t>(remaining, 50)));
            if (ready < 0 && errno == EINTR) continue;
            if (ready < 0) break;
            if (descriptor.revents & (POLLIN | POLLHUP)) {
                const ssize_t n = read(pipefd[0], buffer, sizeof(buffer));
                if (n > 0) output.append(buffer, n);
                else if (n == 0) eof = true;
                else if (errno != EINTR) break;
            }
            if (!reaped) reaped = waitpid(child, &status, WNOHANG) == child;
        }
        close(pipefd[0]);
        if (!reaped) {
            kill(child, SIGKILL);
            pid_t waited;
            do { waited = waitpid(child, &status, 0); } while (waited < 0 && errno == EINTR);
            reaped = waited == child;
        }
        const bool idle = std::string(scenario) == "idle";
        const bool ok = !timedOut && eof && reaped && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
            (output.find("APP-EXIT") != std::string::npos) == idle &&
            (output.find("APP-CLEANUP") != std::string::npos) == idle &&
            output.find("RESUMED-APP") == std::string::npos && output.find("STATIC-EXIT") == std::string::npos;
        std::printf("sessionEndBusy mac %s: %s\n%s", scenario, ok ? "PASS" : "FAIL", output.c_str());
        failures += !ok;
    }
    return failures ? 1 : 0;
}
#endif
