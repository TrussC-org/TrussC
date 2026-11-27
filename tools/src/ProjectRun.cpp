#include "ProjectOps.h"
#include "GeneratedFile.h"
#include "ProjectName.h"
#include <deque>
#include <filesystem>
#include <iostream>
#include <sstream>
#ifdef __linux__
#include <cerrno>
#include <chrono>
#include <thread>
#include <signal.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
extern char** environ;
#endif

using namespace std;
namespace fs = std::filesystem;
namespace projectOps {

#ifdef __linux__
static void stopXinit(pid_t pid) {
    kill(pid, SIGTERM);
    // Bound (xinit may ignore SIGTERM): wait 3 s, then SIGKILL and reap.
    const auto deadline = chrono::steady_clock::now() + chrono::seconds(3);
    while (chrono::steady_clock::now() < deadline) {
        const pid_t waited = waitpid(pid, nullptr, WNOHANG);
        if (waited == pid || (waited == -1 && errno != EINTR)) return;
        this_thread::sleep_for(chrono::milliseconds(50));
    }
    kill(pid, SIGKILL);
    while (waitpid(pid, nullptr, 0) == -1 && errno == EINTR) {}
}

// Keep the existing X11/labwc lifecycle together, including its CLI messages.
static ProcessResult launchDisplaySession(const string& session, const string& binPath,
    const function<ProcessResult(const vector<string>&)>& launch, const LogCallback& emit, CancelHandle* cancel) {
    if (session == "labwc") {
        emit("Launching via labwc Wayland session ...\n", MessageKind::Output);
        return launch({"labwc", "-S", binPath});
    }
    if (session == "x11") {
        // On Pi 5 (modesetting + V3D), running the binary directly as
        // xinit's client races with driver init and the app renders
        // invisibly. A window manager must be running before our GL
        // client connects, but the default xinitrc also spawns xterm
        // and friends which flash on screen. Use a minimal custom
        // xinitrc that only runs twm, then run the binary as a
        // separate X client with DISPLAY=:0:
        //   1. Write a temp xinitrc: `exec twm`
        //   2. Spawn xinit on vt7 with that script as the client
        //   3. Wait for the X socket
        //   4. Authorize the local user via xhost
        //   5. Run the binary
        //   6. Tear xinit down when the app exits
        emit("Launching via xinit (X11 session) on vt7 ...\n", MessageKind::Output);

        // 1. Write minimal xinitrc. Single-window apps work fine with
        // no WM (verified on Pi 5), so we don't make a WM a hard dep.
        // If twm or openbox happens to be installed though, prefer it
        // — it helps with multi-window scenarios such as file dialogs,
        // popups, or apps that open additional windows. Otherwise just
        // keep the X session alive with sleep infinity.
        string xinitrcPath = "/tmp/trusscli-x11-" + to_string(getpid()) + ".sh";
        {
            ostringstream f;
            f << "#!/bin/sh\n"
              << "if command -v twm >/dev/null 2>&1; then\n"
              << "    exec twm\n"
              << "elif command -v openbox >/dev/null 2>&1; then\n"
              << "    exec openbox\n"
              << "else\n"
              << "    exec sleep infinity\n"
              << "fi\n";
            writeGeneratedFile(xinitrcPath, f.str());
        }
        chmod(xinitrcPath.c_str(), 0755);

        vector<char*> xinitArgv = {
            (char*)"xinit",
            (char*)xinitrcPath.c_str(),
            (char*)"--", (char*)":0", (char*)"vt7", nullptr
        };
        cout.flush();
        cerr.flush();
        posix_spawnattr_t attributes;
        posix_spawnattr_init(&attributes);
        if (cancel) {
            posix_spawnattr_setpgroup(&attributes, 0);
            posix_spawnattr_setflags(&attributes, POSIX_SPAWN_SETPGROUP);
        }
        pid_t xinitPid = 0;
        const int error = posix_spawnp(&xinitPid, "xinit", nullptr, &attributes,
                                      xinitArgv.data(), environ);
        posix_spawnattr_destroy(&attributes);
        if (error != 0) {
            emit("Error: failed to spawn xinit (is xserver-xorg installed?)\n", MessageKind::ErrorOutput);
            std::remove(xinitrcPath.c_str());
            return {-1, false};
        }

        // 2. Poll for the X socket (<=6s).
        bool xReady = false;
        for (int i = 0; i < 30; ++i) {
            if (access("/tmp/.X11-unix/X0", F_OK) == 0) { xReady = true; break; }
            usleep(200000);
        }
        if (!xReady) {
            emit("Error: X server did not start within 6 seconds\n", MessageKind::ErrorOutput);
            stopXinit(xinitPid);
            std::remove(xinitrcPath.c_str());
            return {-1, false};
        }
        sleep(1);  // let twm settle

        setenv("DISPLAY", ":0", 1);

        // 3. Authorize this user so DISPLAY=:0 connects without needing
        // the server's XAUTHORITY file (which xinit keeps private).
        const char* user = getenv("USER");
        if (user && *user) {
            launch({"xhost", string("+SI:localuser:") + user});
        }

        // 4. Run the app (blocks until it exits).
        const auto rc = launch({binPath});

        // 5. Tear down the X session.
        stopXinit(xinitPid);
        std::remove(xinitrcPath.c_str());
        return rc;
    }
    return launch({binPath});
}
#endif

Result run(const RunInput& input, const LogCallback& callback, CancelHandle* cancel) {
    Result result;
    // Bound (app-controlled output): retain at most 2 MiB and 32768 entries, including the truncation notice.
    constexpr size_t maxLogBytes = 2 * 1024 * 1024, maxLogEntries = 32768;
    const string truncationNotice = "[Earlier run output truncated]\n";
    deque<string> retained;
    size_t retainedBytes = 0;
    bool truncated = false;
    auto retain = [&](const string& text) {
        const size_t budget = maxLogBytes - truncationNotice.size();
        truncated |= text.size() > budget;
        retained.push_back(text.size() > budget ? text.substr(text.size() - budget) : text);
        retainedBytes += retained.back().size();
        while (retainedBytes > budget || retained.size() >= maxLogEntries) {
            retainedBytes -= retained.front().size();
            retained.pop_front();
            truncated = true;
        }
    };
    auto emit = [&](const string& text, MessageKind kind) {
        if (kind == MessageKind::Warning) result.warnings.push_back(text);
        else retain(text);
        if (kind == MessageKind::ErrorOutput) result.failureReason += text;
        if (callback) callback(text, kind);
    };
    ScopedDiagnostics diagnostics(emit);
    try {
        auto buildInput = input.build;
        string projectPath = buildInput.update.projectPath.empty() ? detectProjectRoot() : buildInput.update.projectPath;
        if (projectPath.empty()) {
            emit("Error: not inside a TrussC project.\n"
                 "Use 'trusscli run -p <path>' or run from inside a project.\n", MessageKind::ErrorOutput);
        } else {
            const string projectName = sanitizeProjectName(fs::canonical(projectPath).filename().string());
            buildInput.update.projectPath = projectPath;
            buildInput.cancel = cancel;
            result = build(buildInput, callback);
            for (const auto& text : result.logLines) retain(text);
            if (result.success) {
                result.success = false;
                result.exitCode = 1;
                emit("\n", MessageKind::Output);
                string binPath;
#ifdef __APPLE__
                const string appPath = projectPath + "/bin/" + projectName + ".app";
                if (fs::exists(appPath)) binPath = appPath + "/Contents/MacOS/" + projectName;
#elif defined(_WIN32)
                const string exePath = projectPath + "/bin/" + projectName + ".exe";
                if (fs::exists(exePath)) binPath = exePath;
#elif defined(__linux__)
                const string executable = projectPath + "/bin/" + projectName;
                if (fs::exists(executable)) binPath = executable;
#endif
                if (binPath.empty()) {
                    emit("Error: built binary not found in " + projectPath + "/bin/\n"
                         "Make sure the build succeeded.\n", MessageKind::ErrorOutput);
                } else {
                    ProcessOptions options;
                    options.cancel = cancel;
                    options.inheritOutput = input.inheritOutput;
                    auto launch = [&](const vector<string>& argv) {
                        return runProcess(argv, [&](const char* data, size_t size) {
                            emit(string(data, size), MessageKind::Output);
                        }, options);
                    };
                    ProcessResult child;
#ifdef __linux__
                    if (!input.session.empty()) {
                        child = launchDisplaySession(input.session, binPath, launch, emit, cancel);
                    } else {
#else
                    if (!input.session.empty()) {
#ifdef __APPLE__
                        emit("--session is a Linux-only option (ignored on macOS).", MessageKind::Warning);
#elif defined(_WIN32)
                        emit("--session is a Linux-only option (ignored on Windows).", MessageKind::Warning);
#endif
                    }
                    {
#endif
                        emit("Launching " + projectName + " ...\n", MessageKind::Output);
                        child = launch({binPath});
                    }
                    result.exitCode = child.exitCode;
                    result.stopped = child.stopped;
                    result.success = child.exitCode == 0 && !child.stopped;
                    if (!result.success && result.failureReason.empty())
                        result.failureReason = child.stopped ? "Run stopped" : "App failed (exit code " + to_string(child.exitCode) + ").";
                }
            }
        }
    } catch (const exception& error) {
        result.success = false;
        result.exitCode = 1;
        emit("Error: " + string(error.what()) + "\n", MessageKind::ErrorOutput);
    }
    result.logLines.clear();
    if (truncated) result.logLines.push_back(truncationNotice);
    result.logLines.insert(result.logLines.end(), retained.begin(), retained.end());
    return result;
}
} // namespace projectOps
