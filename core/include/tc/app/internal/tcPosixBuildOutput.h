#pragma once

#ifndef _WIN32
#include "tcBuildOutputCapture.h"
#include <new>
#include <spawn.h>
#include <sys/wait.h>
#include <sys/ioctl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstdio>
#include <vector>
extern char** environ;

namespace trussc::internal {

// Keep forwarding (including its loss warning) local to this thread. Never
// change the application's SIGPIPE disposition or consume a signal it owned
// before forwarding. Construct only after spawning/waiting for the child.
class PosixBuildOutputSignalGuard {
public:
    PosixBuildOutputSignalGuard() {
        sigemptyset(&pipeSignal_);
        sigaddset(&pipeSignal_, SIGPIPE);
        blocked_ = pthread_sigmask(SIG_BLOCK, &pipeSignal_, &previousMask_) == 0;
        if (blocked_) {
            sigset_t pending;
            if (sigpending(&pending) == 0) {
                pendingBefore_ = sigismember(&pending, SIGPIPE) == 1;
                ready_ = true;
            }
        }
    }
    ~PosixBuildOutputSignalGuard() {
        if (!blocked_) return;
        sigset_t pending;
        if (ready_ && !pendingBefore_ && sigpending(&pending) == 0 &&
            sigismember(&pending, SIGPIPE) == 1) {
#ifdef __APPLE__
            // macOS has no sigtimedwait; SIGPIPE is blocked and known pending.
            int signal = 0;
            sigwait(&pipeSignal_, &signal);
#else
            const timespec timeout = {0, 0};
            while (sigtimedwait(&pipeSignal_, nullptr, &timeout) < 0 && errno == EINTR) {}
#endif
        }
        pthread_sigmask(SIG_SETMASK, &previousMask_, nullptr);
    }
    bool ready() const { return ready_; }
    PosixBuildOutputSignalGuard(const PosixBuildOutputSignalGuard&) = delete;
    PosixBuildOutputSignalGuard& operator=(const PosixBuildOutputSignalGuard&) = delete;
private:
    sigset_t pipeSignal_, previousMask_;
    bool blocked_ = false, ready_ = false, pendingBefore_ = false;
};

inline int runPosixBuildWithOutput(const std::vector<std::string>& arguments, std::string& output) {
    output.clear();
    if (arguments.empty()) return -1;
    // Construct argv before acquiring OS resources; allocation failure is a failed launch.
    std::vector<char*> argv;
#if defined(__cpp_exceptions)
    try {
#endif
        argv.reserve(arguments.size() + 1);
        for (const auto& argument : arguments) argv.push_back(const_cast<char*>(argument.c_str()));
        argv.push_back(nullptr);
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) { return -1; }
#endif
    struct Pipes {
        int descriptors[2][2] = {{-1, -1}, {-1, -1}};
        static void reset(int& fd) { if (fd >= 0) close(fd); fd = -1; }
        ~Pipes() { for (auto& pair : descriptors) for (int& fd : pair) reset(fd); }
    } ownedPipes;
    auto& pipes = ownedPipes.descriptors;
    if (pipe(pipes[0]) || pipe(pipes[1])) return -1;
    // Keep action sources away from stdin/stdout/stderr even if the host was
    // launched with one of those descriptors closed.
    for (auto& pair : pipes) for (int& fd : pair) {
        if (fd < 3) {
            const int replacement = fcntl(fd, F_DUPFD, 3);
            if (replacement < 0) return -1;
            close(fd);
            fd = replacement;
        }
        if (fcntl(fd, F_SETFD, FD_CLOEXEC) < 0) return -1;
    }
    for (auto& pair : pipes)
        if (fcntl(pair[0], F_SETFL, O_NONBLOCK) < 0) return -1;
    struct Actions {
        posix_spawn_file_actions_t value;
        int error = posix_spawn_file_actions_init(&value);
        ~Actions() { if (!error) posix_spawn_file_actions_destroy(&value); }
    } actions;
    if (actions.error) return -1;
    int error = 0;
    for (int stream = 0; stream < 2 && !error; ++stream)
        error = posix_spawn_file_actions_adddup2(&actions.value, pipes[stream][1], stream + 1);
    for (auto& pair : pipes) for (int fd : pair)
        if (!error) error = posix_spawn_file_actions_addclose(&actions.value, fd);
    pid_t pid = 0;
    if (!error) error = posix_spawnp(&pid, argv[0], &actions.value, nullptr, argv.data(), environ);
    for (auto& pair : pipes) Pipes::reset(pair[1]);
    if (error) return -1;
    BuildOutputCapture captured[2];
    pollfd descriptors[2] = {{pipes[0][0], POLLIN, 0}, {pipes[1][0], POLLIN, 0}};
    int status = 0;
    pid_t waited = 0;
    bool finished = false;
    size_t remaining[2] = {};
    while (descriptors[0].fd >= 0 || descriptors[1].fd >= 0) {
        if (!finished) {
            do { waited = waitpid(pid, &status, WNOHANG); } while (waited < 0 && errno == EINTR);
            if (waited != 0) {
                finished = true;
                // Bound (security): after direct-child exit, drain only each pipe's readable snapshot, then close; surviving writers cannot extend the wait.
                for (int stream = 0; stream < 2; ++stream) {
                    int available = 0;
                    if (descriptors[stream].fd >= 0 &&
                        ioctl(descriptors[stream].fd, FIONREAD, &available) == 0 && available > 0)
                        remaining[stream] = static_cast<size_t>(available);
                }
            }
        }
        if (!finished) {
            // Bound (security): idle pipe polling waits at most 25 ms before checking direct-child completion again.
            const int ready = poll(descriptors, 2, 25);
            if (ready < 0) { if (errno == EINTR) continue; break; }
        }
        for (int stream = 0; stream < 2; ++stream) {
            auto& descriptor = descriptors[stream];
            if (descriptor.fd < 0) continue;
            if (finished && !remaining[stream]) {
                Pipes::reset(pipes[stream][0]); descriptor.fd = -1; continue;
            }
            if (!finished && !descriptor.revents) continue;
            // One nonblocking read per ready stream provides fairness.
            char bytes[4096];
            const size_t wanted = finished ? (std::min)(sizeof(bytes), remaining[stream]) : sizeof(bytes);
            ssize_t count;
            do { count = read(descriptor.fd, bytes, wanted); } while (count < 0 && errno == EINTR);
            if (count > 0) {
                captured[stream].append(bytes, static_cast<size_t>(count));
                if (finished) remaining[stream] -= static_cast<size_t>(count);
            } else if (finished || count == 0 || (errno != EAGAIN && errno != EWOULDBLOCK)) {
                Pipes::reset(pipes[stream][0]); descriptor.fd = -1;
            }
        }
    }
    for (auto& pair : pipes) Pipes::reset(pair[0]);
    if (!finished)
        do { waited = waitpid(pid, &status, 0); } while (waited < 0 && errno == EINTR);
    PosixBuildOutputSignalGuard signalGuard;
    buildOutputLoss().record(captured[0].omittedBytes() + captured[1].omittedBytes());
    // Forward only after draining/waiting: a slow output destination cannot
    // stop us from draining the child's pipes. Keep stream destinations apart.
#if defined(__cpp_exceptions)
    try {
#endif
        const auto stdoutText = captured[0].text();
        const auto stderrText = captured[1].text();
        output = stdoutText;
        if (!output.empty() && output.back() != '\n') output += '\n';
        output += stderrText;
        bool forwardingFailed = false;
        const std::string* streams[] = {&stdoutText, &stderrText};
        for (int stream = 0; stream < 2; ++stream) {
            const auto& text = *streams[stream];
            size_t offset = 0;
            while (signalGuard.ready() && offset < text.size()) {
                const ssize_t written = write(stream + 1, text.data() + offset, text.size() - offset);
                if (written < 0 && errno == EINTR) continue;
                // EPIPE (and any other failed destination) loses forwarding
                // only; retained diagnostics and the child status survive.
                if (written <= 0) break;
                offset += static_cast<size_t>(written);
            }
            if (offset != text.size()) forwardingFailed = true;
        }
        if (forwardingFailed) buildOutputLoss().recordForwardingFailure();
#if defined(__cpp_exceptions)
    } catch (const std::bad_alloc&) {
        output.clear();
        try { buildOutputLoss().recordStorageFailure(); } catch (...) {}
    }
#endif
    if (waited < 0) return -1;
    if (WIFEXITED(status)) return WEXITSTATUS(status);
    if (WIFSIGNALED(status)) return 128 + WTERMSIG(status);
    return -1;
}

} // namespace trussc::internal
#endif
