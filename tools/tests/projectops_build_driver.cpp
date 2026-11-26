#include "ProjectOps.h"
#include "GeneratedFile.h"
#include "TrussC.h"
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <iostream>
#include <thread>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/wait.h>

void writeGeneratedFile(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
void removeBuildDirectory(const fs::path& path) { fs::remove_all(path); }

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string mode = argv[1];
    const bool runOperation = mode == "run" || mode.starts_with("run-");
    if (mode.starts_with("run-")) mode.erase(0, 4);
    if (mode == "force" || mode == "force-orphan") prctl(PR_SET_CHILD_SUBREAPER, 1);
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
        auto log = [&](const std::string& text, projectOps::MessageKind kind) {
            output += text;
            events.push_back({{"message", text}, {"kind", static_cast<int>(kind)}});
            if (output.find("READY") != std::string::npos) ready = true;
        };
        if (runOperation) {
            projectOps::RunInput runInput;
            runInput.build = input;
            if (argc > 4) runInput.session = argv[4];
            return projectOps::run(runInput, log, &cancel);
        }
        return projectOps::build(input, log);
    };
    auto operation = std::async(std::launch::async, execute);
    bool stillRunning = false, cwdUnchangedDuringProcess = true;
    bool parentExitedAfterStop = false, survivorBeforeForce = false, appAliveBeforeForce = false;
    pid_t orphan = 0;
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
                pid_t parent = 0;
                std::ifstream(std::getenv("CHILD_PIDS")) >> parent >> orphan;
                appAliveBeforeForce = parent > 0 && kill(parent, 0) == 0 && orphan > 0 && kill(orphan, 0) == 0;
                parentExitedAfterStop = parent > 0 && kill(parent, 0) == -1;
                survivorBeforeForce = orphan > 0 && waitpid(orphan, nullptr, WNOHANG) == 0 && kill(orphan, 0) == 0;
            }
            cancel.forceStop();
            // This test process reaps its adopted orphan, just as init normally does.
            while (orphan > 0 && operation.wait_for(std::chrono::milliseconds(5)) != std::future_status::ready &&
                   std::chrono::steady_clock::now() < deadline) {
                waitpid(orphan, nullptr, WNOHANG);
            }
        }
    }
    const auto result = operation.get();
    std::cout << tc::Json{{"success", result.success}, {"stopped", result.stopped},
        {"exitCode", result.exitCode}, {"error", result.failureReason}, {"logs", result.logLines},
        {"events", events}, {"output", output}, {"stillRunning", stillRunning},
        {"appAliveBeforeForce", appAliveBeforeForce}, {"parentExitedAfterStop", parentExitedAfterStop}, {"survivorBeforeForce", survivorBeforeForce},
        {"cwdUnchangedDuringProcess", cwdUnchangedDuringProcess},
        {"cwdRestored", fs::current_path() == originalDirectory}};
}
