#include "ProjectOps.h"
#include "GeneratedFile.h"
#include "TrussC.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <signal.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#include <sys/wait.h>

void writeGeneratedFile(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
void removeBuildDirectory(const fs::path& path) { fs::remove_all(path); }

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    const std::string mode = argv[1];
#if defined(__linux__)
    if (mode == "force" || mode == "force-orphan") prctl(PR_SET_CHILD_SUBREAPER, 1);
#endif
    projectOps::CancelHandle cancel;
    std::atomic<bool> ready{false};
    std::string output;
    tc::Json events = tc::Json::array();
    const auto originalDirectory = fs::current_path();
    auto execute = [&]() {
        projectOps::BuildInput input;
        input.update.projectPath = argv[2];
        input.cancel = &cancel;
        input.jobs = 1;
        input.verbose = true;
        if (argc > 3) input.buildType = argv[3];
        return projectOps::build(input, [&](const std::string& text, projectOps::MessageKind kind) {
            output += text;
            events.push_back({{"message", text}, {"kind", static_cast<int>(kind)}});
            if (output.find("READY") != std::string::npos) ready = true;
        });
    };
    auto operation = std::async(std::launch::async, execute);
    bool stillRunning = false, cwdUnchangedDuringProcess = true;
    bool parentExitedAfterStop = false, survivorBeforeForce = false;
    pid_t parent = 0, orphan = 0;
    if (mode == "stop" || mode == "force" || mode == "force-orphan") {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        const char* readyMarker = std::getenv("CHILD_READY");
        auto childReady = [&]() { return ready || (readyMarker && fs::exists(readyMarker)); };
        while (!childReady() && operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cwdUnchangedDuringProcess = fs::current_path() == originalDirectory;
        cancel.requestStop();
        if (mode == "force" || mode == "force-orphan") {
            while (cancel.state() != projectOps::ProcessState::StillRunning &&
                   operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
                   std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            stillRunning = cancel.state() == projectOps::ProcessState::StillRunning &&
                operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
            {
                std::ifstream(std::getenv("CHILD_PIDS")) >> parent >> orphan;
#if defined(__linux__)
                if (parent > 0) waitpid(parent, nullptr, WNOHANG);
#endif
                parentExitedAfterStop = parent > 0 && kill(parent, 0) == -1 && errno == ESRCH;
                // On macOS the live orphan belongs to launchd, not this driver.
                survivorBeforeForce = orphan > 0 && kill(orphan, 0) == 0;
            }
            cancel.forceStop();
            // Linux adopts fixture descendants; macOS launchd reaps them.
#if defined(__linux__)
            while (orphan > 0 && operation.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready &&
                   std::chrono::steady_clock::now() < deadline) {
                if (parent > 0) waitpid(parent, nullptr, WNOHANG);
                waitpid(orphan, nullptr, WNOHANG);
            }
#endif
        }
    }
    const auto result = operation.get();
    std::cout << tc::Json{{"success", result.success}, {"stopped", result.stopped},
        {"exitCode", result.exitCode}, {"error", result.failureReason}, {"logs", result.logLines},
        {"events", events}, {"output", output}, {"stillRunning", stillRunning},
        {"parentExitedAfterStop", parentExitedAfterStop}, {"survivorBeforeForce", survivorBeforeForce},
        {"cwdUnchangedDuringProcess", cwdUnchangedDuringProcess},
        {"cwdRestored", fs::current_path() == originalDirectory}};
}
