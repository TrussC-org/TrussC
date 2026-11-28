#include "ProjectOps.h"
#include "GeneratedFile.h"
#include "TrussC.h"
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
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

#if defined(__linux__)
// Linked only into this test driver with --wrap=access. Never inspect host X paths.
extern "C" int __real_access(const char*, int);
extern "C" int __wrap_access(const char* path, int mode) {
    if (std::strcmp(path, "/tmp/.X11-unix/X0") == 0 || std::strcmp(path, "/tmp/.X0-lock") == 0) {
        const char* directory = std::getenv("TEST_X11_DIRECTORY");
        if (!directory || !*directory) std::abort();
        const auto fixture = fs::path(directory) / fs::path(path).filename();
        return __real_access(fixture.c_str(), mode);
    }
    return __real_access(path, mode);
}
#endif

void writeGeneratedFile(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
void removeBuildDirectory(const fs::path& path) { fs::remove_all(path); }

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    std::string mode = argv[1];
    const bool distOperation = mode == "dist" || mode.starts_with("dist-");
    if (mode.starts_with("dist-")) mode.erase(0, 5);
    const bool runOperation = mode == "run" || mode.starts_with("run-");
    if (mode.starts_with("run-")) mode.erase(0, 4);
#if defined(__linux__)
    if (mode == "force" || mode == "force-orphan" || mode == "installer-force") prctl(PR_SET_CHILD_SUBREAPER, 1);
#endif
    projectOps::CancelHandle cancel;
    if (mode == "pre-stopped" || mode == "pre-stopped-archive") cancel.requestStop();
    std::atomic<bool> ready{false};
    bool fetchAsked = false;
    std::string output;
    tc::Json events = tc::Json::array();
    tc::Json phases = tc::Json::array(), progress = tc::Json::array();
    const auto originalDirectory = fs::current_path();
    auto execute = [&]() {
        projectOps::BuildInput input;
        input.update.projectPath = argv[2];
        input.cancel = &cancel;
        input.jobs = 1;
        input.verbose = true;
        input.phase = [&](projectOps::OperationPhase phase) { phases.push_back(static_cast<int>(phase)); };
        input.progress = [&](const BuildOutputFilter::Progress& value) {
            progress.push_back({{"compiled", value.compiledFiles}, {"total", value.totalFiles},
                {"firstError", value.firstError}, {"location", value.firstErrorLocation}});
        };
        if (argc > 3) input.buildType = argv[3];
        auto log = [&](const std::string& text, projectOps::MessageKind kind) {
            if (mode == "packaging-stop" && text.find("Packaging the existing build") != std::string::npos)
                cancel.requestStop();
            output += text;
            events.push_back({{"message", text}, {"kind", static_cast<int>(kind)}});
            if (output.find("READY") != std::string::npos) ready = true;
        };
        if (mode == "update") return projectOps::update(input.update, log);
        if (distOperation) {
            projectOps::DistInput distInput;
            distInput.build = input;
            distInput.build.update.executablePath = fs::path(std::getenv("TRUSSC_DIR")) / "tools/bin/trusscli";
            distInput.noBuild = mode != "build-stop" && mode != "fetch";
            if (mode == "fetch") distInput.build.confirmFetch = [&](const std::vector<std::string>&) {
                fetchAsked = true;
                return false;
            };
            distInput.archive = mode == "stop" || mode == "force" || mode == "pre-stopped-archive";
            distInput.installer = mode == "installer-stop" || mode == "installer-force";
            return projectOps::dist(distInput, log);
        }
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
    pid_t parent = 0, orphan = 0;
    if (mode == "stop" || mode == "force" || mode == "force-orphan" ||
        mode == "build-stop" || mode == "installer-stop" || mode == "installer-force") {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        const char* readyMarker = std::getenv("CHILD_READY");
        auto childReady = [&]() { return ready || (readyMarker && fs::exists(readyMarker)); };
        while (!childReady() && operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
               std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        cwdUnchangedDuringProcess = fs::current_path() == originalDirectory;
        cancel.requestStop();
        if (mode == "force" || mode == "force-orphan" || mode == "installer-force") {
            while (cancel.state() != projectOps::ProcessState::StillRunning &&
                   operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready &&
                   std::chrono::steady_clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(5));
            stillRunning = cancel.state() == projectOps::ProcessState::StillRunning &&
                operation.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
            {
                std::ifstream(std::getenv("CHILD_PIDS")) >> parent >> orphan;
                appAliveBeforeForce = parent > 0 && kill(parent, 0) == 0 && orphan > 0 && kill(orphan, 0) == 0;
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
        {"phases", phases}, {"progress", progress}, {"fetchAsked", fetchAsked}, {"events", events}, {"output", output}, {"stillRunning", stillRunning},
        {"appAliveBeforeForce", appAliveBeforeForce}, {"parentExitedAfterStop", parentExitedAfterStop}, {"survivorBeforeForce", survivorBeforeForce},
        {"cwdUnchangedDuringProcess", cwdUnchangedDuringProcess},
        {"cwdRestored", fs::current_path() == originalDirectory}};
}
