#include "ProjectOps.h"
#include "PresetRoot.h"
#include "BuildSetup.h"
#include "BuildPreferences.h"
#include "BuildOutputFilter.h"
#include "ProjectName.h"
#include "GeneratedFile.h"
#include "TrussCVersion.h"
#include "WindowsCommand.h"
#include "TrussC.h"
#include <algorithm>
#include <deque>
#include <fstream>
#include <sstream>
#include <streambuf>

using namespace std;
using namespace tc;
namespace fs = std::filesystem;
namespace projectOps {
// Shared parser implemented in ProjectOps.cpp.
vector<string> readProjectAddons(const string& projectPath);

string findCMake() {
#ifdef __APPLE__
    const char* paths[] = {
        "/opt/homebrew/bin/cmake",
        "/usr/local/bin/cmake",
        "/Applications/CMake.app/Contents/bin/cmake",
    };
    for (const char* p : paths) {
        if (fs::exists(p)) return p;
    }
#endif
    return "cmake";
}

// Re-pin the Visual Studio toolchain in CMakePresets.json after Visual Studio
// changed (#357): detect VS again and replace only the "windows" preset's
// CMAKE_MAKE_PROGRAM and INCLUDE / LIB / PATH. The targets, web backend, IDE
// (#350), TRUSSC_DIR, the other presets and hand edits stay. build-windows is
// removed so the next configure starts clean. Nothing is changed when no
// usable Visual Studio is found, or when its paths are missing too, so the
// next build tries again. Always prints what it did.
static int refreshWindowsPresets(const string& projectPath, const ToolchainCheck& toolchain, ostream& out, ostream& err) {
    const fs::path presetsPath = fs::path(projectPath) / "CMakePresets.json";
    string presetsText;
    {
        ifstream file(presetsPath, ios::binary);
        stringstream ss;
        ss << file.rdbuf();
        presetsText = ss.str();
    }
    const string missing = toolchain.missing.empty() ? string() : toolchain.missing[0];
    const string changed = "Visual Studio changed since this project was generated (" +
                           missing + " is missing)";

    // The newest install, as `trusscli new` / `update` pick it. Without a VS
    // the detector returns a fallback entry with no MSVC / SDK version.
    vector<VsVersionInfo> installed = VsDetector::detectInstalledVersions();
    if (installed.empty() || !canPinToolchain(installed[0])) {
        err << "Error: " << changed << ", and no Visual Studio with the C++ tools "
                "was found.\n"
             << "Install Visual Studio (Desktop development with C++) and run "
                "'trusscli build' again. CMakePresets.json and "
             << ProjectGenerator::buildDirForPreset(kNativePreset) << " were left as they are.\n";
        return 1;
    }
    const VsVersionInfo& vs = installed[0];

    string repinned = repinWindowsToolchain(presetsText, windowsToolchainPins(vs));
    if (repinned.empty()) {
        err << "Error: " << changed << ", and CMakePresets.json has no readable "
                "\"windows\" preset. Run 'trusscli update' to rewrite it.\n";
        return 1;
    }
    // The detected install's paths must exist, or every build would repeat this
    ToolchainCheck fresh = checkPresetToolchain(repinned, [](const string& path) {
        error_code e;
        return fs::exists(fs::path(path), e);
    });
    if (fresh.stale()) {
        err << "Error: " << changed << ", and the paths of the detected "
             << vs.displayName << " are missing too: " << fresh.missing[0] << "\n"
             << "That Windows SDK / MSVC folder is incomplete: remove the leftover folder or "
                "repair the SDK / Visual Studio install (or drop the \"environment\" block "
                "from the \"windows\" preset). CMakePresets.json was left as it is.\n";
        return 1;
    }

    // Remove the old build folder first: if that fails, the presets stay
    // stale and the next build tries again.
    const string buildDir = ProjectGenerator::buildDirForPreset(kNativePreset);
    error_code ec;
    try {
        removeBuildDirectory(fs::path(projectPath) / buildDir);
    } catch (const fs::filesystem_error& e) {
        ec = e.code();
    }
    if (ec) {
        err << "Error: " << changed << ", and " << buildDir << " could not be removed ("
             << ec.message() << ").\n"
             << "Close programs that use files in it and run 'trusscli build' again.\n";
        return 1;
    }
    writeGeneratedFile(presetsPath, repinned);

    out << "[build] " << changed << ": pinned " << vs.displayName
         << " in CMakePresets.json and removed " << buildDir << "\n";
    out.flush();
    return 0;
}

// Adapt existing byte-oriented build filtering to the operation callback.
class CallbackBuffer : public streambuf {
public:
    explicit CallbackBuffer(function<void(const string&)> callback) : callback_(std::move(callback)) {}
protected:
    streamsize xsputn(const char* data, streamsize size) override {
        callback_(string(data, static_cast<size_t>(size)));
        return size;
    }
    int_type overflow(int_type value) override {
        if (!traits_type::eq_int_type(value, traits_type::eof())) callback_(string(1, traits_type::to_char_type(value)));
        return traits_type::not_eof(value);
    }
private:
    function<void(const string&)> callback_;
};

static int executeBuild(const BuildInput& input, Result& result, const LogCallback& log,
                        ostream& out, ostream& err) {
    ProcessOptions processOptions;
    processOptions.cancel = input.cancel;
    processOptions.hideConsole = true; // These children are CMake console tools.
    struct RestoreCommands {
        ProjectGenerator::CommandRunner previous;
        ~RestoreCommands() { ProjectGenerator::commandRunner = std::move(previous); }
    } restore{std::move(ProjectGenerator::commandRunner)};
    ProjectGenerator::commandRunner = [&](const string& command) -> pair<int, string> {
        if (result.stopped) return {-1, ""};
        string output;
        auto options = processOptions;
#ifdef _WIN32
        options.windowsCommandLine = windowsShellCommandLine(command);
        const vector<string> arguments = {"cmd.exe"};
#else
        const vector<string> arguments = {"/bin/sh", "-c", command};
#endif
        const auto child = runProcess(arguments, [&](const char* data, size_t size) {
            output.append(data, size);
        }, options);
        result.stopped |= child.stopped;
        int code = child.exitCode;
#ifndef _WIN32
        // The generator's existing popen adapter returns a wait status.
        if (code >= 0) code <<= 8;
#endif
        return {code, std::move(output)};
    };
    const auto& explicitPath = input.update.projectPath;
    const auto& explicitTcRoot = input.update.tcRoot;
    auto targetPreset = input.targetPreset;
    const auto& buildType = input.buildType;
    const bool clean = input.clean, mcp = input.mcp, warnings = input.warnings;
    const bool dryRun = input.dryRun, verbose = input.verbose, rememberType = input.rememberType;
    const int jobs = input.jobs;
    string projectPath = explicitPath.empty() ? detectProjectRoot() : explicitPath;
    if (projectPath.empty()) {
        err << "Error: not inside a TrussC project.\n"
             << "Use 'trusscli build -p <path>' or run from inside a project.\n";
        return 1;
    }

    if (!explicitTcRoot.empty() && dryRun) {
        err << "Error: save --tc-root with use-local before --dry-run\n";
        return 1;
    }
    bool updatedForFetch = false;
    if (input.confirmFetch && (!explicitTcRoot.empty() ||
        fs::exists(fs::path(projectPath) / ".trussc-version") ||
        !trusscVersion::localPath(projectPath).empty())) {
        vector<string> missing;
        auto probeRoot = explicitTcRoot.empty()
            ? trusscVersion::resolve(projectPath, false, &missing) : explicitTcRoot;
        const bool missingFramework = !missing.empty();
        auto presetPath = fs::path(projectPath) / "CMakePresets.json";
        if (fs::exists(presetPath)) {
            auto data = Json::parse(trusscVersion::readBounded(presetPath, 16 * 1024 * 1024), nullptr, false);
            probeRoot = presetAddonRoot(projectPath, targetPreset.empty() ? string(kNativePreset ? kNativePreset : "") : targetPreset, probeRoot, data);
        }
        if (probeRoot.empty() && !input.update.executablePath.empty()) probeRoot = detectTcRoot(input.update.executablePath);
        auto selection = readProjectAddons(projectPath);
        if (missingFramework) {
            // Bundled addons and remote manifests become known only after update
            // acquires the selected framework. List explicit external selections now.
            selection.erase(remove_if(selection.begin(), selection.end(), [](const string& line) {
                const auto spec = addonVersions::parse(line);
                return spec.owner.empty() && spec.ref.empty();
            }), selection.end());
        }
        if (!selection.empty() && !probeRoot.empty()) {
            ScopedDiagnostics quiet({}); // The ordinary build emits resolver warnings once.
            projectOps::resolveAddons(projectPath, probeRoot, false, input.update.addonLookup, &selection, &missing);
        }
        if (!missing.empty() && dryRun) {
            out << "Not in cache:\n";
            for (const auto& item : missing) out << "  " << item << "\n";
            err << "Error: dry run: sources unavailable; run `trusscli update` to download them.\n";
            return 1;
        }
        if (!missing.empty() && input.confirmFetch(missing)) {
            auto regeneration = input.update;
            regeneration.projectPath = projectPath;
            regeneration.fetchVersion = true;
            if (targetPreset == "web") regeneration.flags.web = true;
            if (targetPreset == "android") regeneration.flags.android = true;
            if (targetPreset == "ios") regeneration.flags.ios = true;
            auto updated = update(regeneration, log);
            if (result.stopped) return -1;
            if (!updated.success) { err << "Error: " << updated.failureReason << "\n"; return 1; }
            updatedForFetch = true;
        }
    }

    if (!explicitTcRoot.empty() && !updatedForFetch) {
        auto regeneration = input.update;
        regeneration.projectPath = projectPath;
        regeneration.fetchVersion = false;
        auto updated = update(regeneration, log);
        if (result.stopped) return -1;
        if (!updated.success) { err << "Error: " << updated.failureReason << "\n"; return 1; }
    }
    // Validate offline after any caller-approved update.
    string selectedRoot = trusscVersion::resolve(projectPath, false);
    if (!selectedRoot.empty()) {
        selectedRoot = fs::absolute(selectedRoot).generic_string();
        out << "TrussC: " << selectedRoot;
        if (fs::exists(fs::path(selectedRoot) / ".git") ||
            fs::exists(fs::path(selectedRoot) / ".git_archival.txt")) out << " (" << trusscVersion::commitAt(selectedRoot) << ")";
        out << "\n";
    }


    if (targetPreset.empty()) {
        if (!kNativePreset) {
            err << "Error: cannot detect native platform. Use --web, --android, or --ios.\n";
            return 1;
        }
        targetPreset = kNativePreset;
    }

    // Auto-run update if build files are missing (first-time setup after a
    // minimal git clone that only ships src/). Flags are forwarded so that
    // e.g. `trusscli build --web` implies `trusscli update --web`.
    string cmakeListsPath = projectPath + "/CMakeLists.txt";
    string presetsFile = projectPath + "/CMakePresets.json";
    if ((!fs::exists(cmakeListsPath) || !fs::exists(presetsFile)) && dryRun) {
        err << "Error: --dry-run needs the project's build files; run 'trusscli update' first.\n";
        return 1;
    }
    if (!fs::exists(cmakeListsPath) || !fs::exists(presetsFile)) {
        out << "[update] Build files not found, running 'trusscli update' first...\n";
        auto regeneration = input.update;
        regeneration.projectPath = projectPath;
        regeneration.fetchVersion = false;
        if (targetPreset == "web") regeneration.flags.web = true;
        if (targetPreset == "android") regeneration.flags.android = true;
        if (targetPreset == "ios") regeneration.flags.ios = true;
        auto updated = update(regeneration, log);
        if (result.stopped) return -1;
        if (!updated.success) { err << "Error: " << updated.failureReason << "\n"; return 1; }
        out << "\n";
    }

    // Safety check (should be generated by update above, but keep this as a
    // belt-and-suspenders guard for edge cases).
    if (!fs::exists(presetsFile)) {
        err << "Error: no CMakePresets.json in " << projectPath << "\n"
             << "Run 'trusscli update' first to generate build files.\n";
        return 1;
    }

    addonVersions::Paths resolvedAddonPaths;
    // Bound (project presets): cap addon-selection metadata at 16 MiB.
    const auto presetData = Json::parse(trusscVersion::readBounded(presetsFile, 16 * 1024 * 1024), nullptr, false);
    string addonRoot = selectedRoot;
    addonRoot = presetAddonRoot(projectPath, targetPreset, addonRoot, presetData);
    // Legacy projects leave lookup to CMake, including its preset's core.
    bool resolveAddons = !selectedRoot.empty();
    Json addonVendor = Json::object();
    if (presetData.is_object() && presetData.contains("vendor")) {
        const auto& vendors = presetData["vendor"];
        if (vendors.is_object() && vendors.contains("trussc") && vendors["trussc"].is_object()) addonVendor = vendors["trussc"];
    }
    string effectiveType = buildType.empty() ? "RelWithDebInfo" : buildType;
    const bool nativeBuild = kNativePreset && targetPreset == kNativePreset;
    bool remembered = false;
    if (nativeBuild) {
        if (rememberType && buildType.empty()) {
            effectiveType = trusscliBuild::rememberedNativeBuildType(addonVendor);
            remembered = addonVendor.value("buildType", Json()) == effectiveType;
        }
        out << "Build type: " << effectiveType << (remembered ? " (remembered)" : "") << "\n";
        if (rememberType && !buildType.empty() && !dryRun && presetData.is_object()) {
            auto updated = presetData;
            if (!updated["vendor"].is_object()) updated["vendor"] = Json::object();
            if (!updated["vendor"]["trussc"].is_object()) updated["vendor"]["trussc"] = Json::object();
            updated["vendor"]["trussc"]["buildType"] = buildType;
            writeGeneratedFile(presetsFile, updated.dump(2));
        }
    } else if (targetPreset == "web") {
        effectiveType = "MinSizeRel";
    } else if (targetPreset == "android") {
        effectiveType = "Release";
    }
    const auto localAddons = addonVendor.value("addonLocalPaths", Json::object());
    const auto savedAddons = addonVendor.value("addonPaths", Json::object());
    for (const auto& line : readProjectAddons(projectPath)) {
        auto spec = addonVersions::parse(line);
        resolveAddons |= !spec.owner.empty() || !spec.ref.empty() || localAddons.contains(spec.name);
        // An unqualified external addon acquired by add/update still uses HEAD.
        resolveAddons |= savedAddons.contains(spec.name) &&
            !fs::is_directory(fs::path(addonRoot) / "addons" / spec.name);
    }
    if (resolveAddons) {
        if (addonRoot.empty()) throw runtime_error("addon resolution needs a concrete preset TRUSSC_DIR");
        resolvedAddonPaths = projectOps::resolveAddons(projectPath, addonRoot, false, input.update.addonLookup);
    }

    trusscVersion::markCachedUsed(addonRoot);
    for (const auto& addon : resolvedAddonPaths) trusscVersion::markCachedUsed(addon.second);

    // Windows: the presets pin the Visual Studio toolchain found when they were
    // written. If Visual Studio changed since, rewrite them (same targets and
    // IDE) and drop the native build folder; the configure below then runs on
    // the fresh presets. Same check as `trusscli doctor`.
    if (kNativePreset && targetPreset == kNativePreset) {
        ToolchainCheck toolchain;
        toolchain = checkPresetToolchain(trusscVersion::readBounded(presetsFile, 16 * 1024 * 1024),
            [](const string& path) { error_code ec; return fs::exists(path, ec); });
        if (shouldRefreshPresets(kNativePreset, targetPreset, toolchain)) {
            if (dryRun) {
                out << "[dry-run] would re-pin the Visual Studio paths in CMakePresets.json "
                        "and remove the native build folder\n";
            } else if (int rrc = refreshWindowsPresets(projectPath, toolchain, out, err)) {
                return rrc;
            }
        }
    }

    string cmake = findCMake();

    string parallelArg = trusscliBuild::parallelBuildArg(jobs, out);

    // cmake --build --preset <target> --parallel[=N] --config <type> [--clean-first]
    vector<string> cmd = {cmake, "--build", "--preset", targetPreset, parallelArg};
    // --config only affects multi-config generators (Xcode, Visual Studio). The
    // native presets are single-config (Unix Makefiles / Ninja), where --config
    // is silently ignored; there the build type is fixed at configure time by
    // CMAKE_BUILD_TYPE, which we pin below. Kept here so the build type also
    // applies on any future multi-config native preset.
    cmd.push_back("--config"); cmd.push_back(effectiveType);
    if (clean) cmd.push_back("--clean-first");

    // Name shown in the progress line ("Building <project> ...")
    error_code nameEc;
    fs::path projectDir = fs::weakly_canonical(projectPath, nameEc);
    string projectName = projectDir.filename().string();
    if (projectName.empty()) projectName = projectDir.parent_path().filename().string();
    projectName = sanitizeProjectName(projectName);

    // Give the child its directory without changing the caller's directory.
    processOptions.workingDirectory = fs::absolute(projectPath).string();
    const fs::path buildFolder = fs::path(projectPath) / ProjectGenerator::buildDirForPreset(targetPreset);

    // `cmake --build` cannot pass -D and does not configure a build folder
    // that has no cache (or only the cache of a failed configure), so those
    // cases need a configure pass
    // (`cmake --preset`) first. MCP flag changes also require configure;
    // every configure explicitly sets the requested MCP dev option.
    // The launcher resolves the working directory for each child.
    ConfigureInputs cfgIn = inspectBuildFolder(projectPath, targetPreset,
                                               kNativePreset ? kNativePreset : "");
    cfgIn.requestedBuildType = nativeBuild ? effectiveType : buildType;
    cfgIn.mcp = mcp;
    cfgIn.warnings = warnings;
    ConfigurePlan plan = planConfigure(cfgIn);
    // Reuse CMake's configured scan (including explicit SOURCES and the
    // platform gate) rather than maintaining a second marker parser here.
    // A changed state makes the check fail and requests reconfiguration;
    // plain cmake --build keeps that same fast-fail behavior. Dry-run must
    // not execute the check, which can touch CMakeLists.txt on a toggle.
    const fs::path hotReloadCheck = buildFolder / "_tc_check_hot_reload.cmake";
    bool hotReloadChanged = false;
    if (!dryRun && !plan.configure && cfgIn.isNative &&
        fs::exists(hotReloadCheck)) {
        auto checkOptions = processOptions;
        checkOptions.workingDirectory = fs::absolute(buildFolder).string();
        // The expected fast-fail is replaced by the configure below.
        const auto checked = runProcess({cmake, "-P", "_tc_check_hot_reload.cmake"}, {}, checkOptions);
        result.stopped |= checked.stopped;
        hotReloadChanged = checked.exitCode != 0;
        if (result.stopped) return checked.exitCode;
    }
    if (hotReloadChanged) {
        out << "[HotReload] Reconfiguring before build...\n";
        plan.defines.push_back(mcp ? "-DTRUSSC_MCP_DEV=ON" : "-DTRUSSC_MCP_DEV=OFF");
        plan.configure = true;
    }
    for (const string& line : plan.messages) out << line << "\n";
    out.flush();   // print before cmake's own output, also through a pipe
    vector<string> cfg = {cmake, "--preset", targetPreset};
    cfg.insert(cfg.end(), plan.defines.begin(), plan.defines.end());
    bool needConfigure = plan.configure;
    const string addonPathsJson = Json(resolvedAddonPaths).dump();
    const string cachedAddonPaths = readCMakeCacheValue((buildFolder / "CMakeCache.txt").string(), "TRUSSC_ADDON_PATHS");
    bool presetAddonPaths = false;
    if (presetData.is_object() && presetData.contains("configurePresets")) {
        for (const auto& preset : presetData["configurePresets"]) {
            if (preset.contains("cacheVariables") && preset["cacheVariables"].contains("TRUSSC_ADDON_PATHS")) presetAddonPaths = true;
        }
    }
    if ((resolveAddons && !resolvedAddonPaths.empty()) || !cachedAddonPaths.empty() || presetAddonPaths) {
        cfg.push_back("-DTRUSSC_ADDON_PATHS=" + addonPathsJson);
        cfg.push_back("-DTRUSSC_ADDON_ORDER=" + resolvedAddonPaths.order);
        const string cachedOrder = readCMakeCacheValue((buildFolder / "CMakeCache.txt").string(), "TRUSSC_ADDON_ORDER");
        needConfigure = needConfigure || cachedOrder != resolvedAddonPaths.order;
        needConfigure = needConfigure || cachedAddonPaths != addonPathsJson;
    }
    if (!selectedRoot.empty()) {
        string coreDir = selectedRoot + "/core";
        cfg.push_back("-DTRUSSC_DIR=" + coreDir);
        ifstream cache(buildFolder / "CMakeCache.txt");
        string line;
        bool sameRoot = false;
        while (getline(cache, line)) {
            if (line.rfind("TRUSSC_DIR:", 0) == 0) {
                auto equals = line.find('=');
                if (equals != string::npos) sameRoot = fs::path(line.substr(equals + 1)) == fs::path(coreDir);
            }
        }
        needConfigure = needConfigure || !sameRoot;
    }

    if (dryRun) {
        // Print the commands instead of running them (one line each).
        auto printCommand = [&](const vector<string>& argv) {
            out << "[dry-run]";
            for (const string& a : argv) {
                if (a.find_first_of(" \t\"") == string::npos && !a.empty()) out << ' ' << a;
                else out << " \"" << a << '"';
            }
            out << "\n";
        };
        if (needConfigure) printCommand(cfg);
        printCommand(cmd);
        return 0;
    }

    // The full output of configure and build always goes to <build folder>/build.log
    // (overwritten on each build). The screen gets the short form unless --verbose.
    // Both streams of the child arrive as one ordered stream (runCaptured).
    error_code logEc;
    fs::create_directories(buildFolder, logEc);
    const string logDisplay = cfgIn.buildDir + "/build.log";
    ofstream logFile(fs::path(projectPath) / logDisplay, ios::binary | ios::trunc);
    const bool haveLog = logFile.is_open();
    if (!haveLog) log("cannot write " + logDisplay + " (the full output is not saved).", MessageKind::Warning);
    const auto startedAt = chrono::steady_clock::now();
    auto elapsedSeconds = [&]() {
        return chrono::duration<double>(chrono::steady_clock::now() - startedAt).count();
    };
    BuildOutputFilter::Options filterOptions;
    filterOptions.verbose = verbose;
    filterOptions.project = projectName;
    filterOptions.logDisplayPath = haveLog ? logDisplay : "";
    auto runFiltered = [&](const vector<string>& argv, BuildOutputFilter::Phase phase) {
        filterOptions.phase = phase;
        if (phase == BuildOutputFilter::Phase::Build) {
            // Written by the configure above (or an earlier one)
            filterOptions.totalFiles = countCompileCommands((buildFolder / "compile_commands.json").string());
        }
        BuildOutputFilter filter(out, haveLog ? &logFile : nullptr, filterOptions);
        filter.begin();
        const auto child = runProcess(argv, [&](const char* data, size_t size) { filter.write(data, size); }, processOptions);
        result.stopped |= child.stopped;
        const int code = child.exitCode;
        filter.end(code, elapsedSeconds());
        return code;
    };

    int rc = 0;
    if (needConfigure) rc = runFiltered(cfg, BuildOutputFilter::Phase::Configure);
    if (rc == 0 && !result.stopped && !(input.cancel && input.cancel->stopRequested())) rc = runFiltered(cmd, BuildOutputFilter::Phase::Build);
#ifdef _WIN32
    // CMake owns this decision and may update it during an automatic reconfigure.
    string hotReloadState;
    ifstream(buildFolder / ".tc_hot_reload_state") >> hotReloadState;
#endif

    if (rc != 0) {
        // The short summary already says Failed; --verbose keeps the old line.
        if (verbose) err << "Build failed (exit code " << rc << ").\n";
#ifdef _WIN32
        if (hotReloadState == "ON") {
            log("the guest can use only TrussC and code linked into the guest; "
                "main.cpp functions and host-only libraries are not visible.", MessageKind::Warning);
        }
#endif
    }
    return rc;
}

Result build(const BuildInput& input, const LogCallback& callback) {
    Result result;
    // Bound (long build transcripts): retain at most 2 MiB and 32768 chunks, including the truncation notice.
    constexpr size_t maxLogBytes = 2 * 1024 * 1024, maxLogEntries = 32768;
    const string truncationNotice = "[Earlier build output truncated]\n";
    deque<string> retained;
    size_t retainedBytes = 0;
    bool truncated = false;
    auto log = [&](const string& message, MessageKind kind) {
        if (kind == MessageKind::Warning) result.warnings.push_back(message);
        else {
            const size_t budget = maxLogBytes - truncationNotice.size();
            truncated |= message.size() > budget;
            retained.push_back(message.size() > budget ? message.substr(message.size() - budget) : message);
            retainedBytes += retained.back().size();
            while (retainedBytes > budget || retained.size() >= maxLogEntries) {
                retainedBytes -= retained.front().size();
                retained.pop_front();
                truncated = true;
            }
        }
        if (kind == MessageKind::ErrorOutput) result.failureReason += message;
        if (callback) callback(message, kind);
    };
    ScopedDiagnostics diagnostics(log);
    CallbackBuffer output([&](const string& text) { log(text, MessageKind::Output); });
    CallbackBuffer errors([&](const string& text) { log(text, MessageKind::ErrorOutput); });
    ostream out(&output), err(&errors);
    try {
        result.exitCode = executeBuild(input, result, log, out, err);
        result.stopped |= input.cancel && input.cancel->stopRequested();
        result.success = result.exitCode == 0 && !result.stopped;
        if (result.success) result.failureReason.clear();
        if (!result.success && result.failureReason.empty())
            result.failureReason = result.stopped ? "Build stopped" : "Build failed (exit code " + to_string(result.exitCode) + ").";
    } catch (const exception& error) {
        err << "Error: " << error.what() << "\n";
    }
    if (truncated) result.logLines.push_back(truncationNotice);
    result.logLines.insert(result.logLines.end(), retained.begin(), retained.end());
    return result;
}
} // namespace projectOps
