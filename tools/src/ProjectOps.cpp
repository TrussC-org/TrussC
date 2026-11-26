#include "ProjectOps.h"
#include "GeneratedFile.h"
#include "OperationLog.h"
#include "TrussCVersion.h"
#include "TrussC.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

using namespace std;
using namespace tc;
namespace fs = std::filesystem;
namespace projectOps {

static string addonNameFromLine(const string& line) {
    auto start = line.find_first_not_of(" \t\r\n");
    if (start == string::npos || line[start] == '#') return {};
    auto end = line.find_last_not_of(" \t\r\n");
    return line.substr(start, end - start + 1);
}

static string readGuiUpdateFile(const fs::path& path) {
    constexpr size_t limit = 16 * 1024 * 1024;
    if (fs::file_size(path) > limit) {
        throw runtime_error(path.filename().string() + " exceeds the " + to_string(limit) + "-byte limit");
    }
    return trusscVersion::readBounded(path, limit);
}

static string validatePlainProject(const string& projectPath, const ProjectSettings& settings) {
    const string cliRequired =
        "This project uses a pinned TrussC version or external addons. Use "
        "`trusscli update -p <path>` (GUI support is planned for v0.9.1).";
    try {
        if (fs::exists(fs::path(projectPath) / ".trussc-version")) return cliRequired;
        // use-local is currently saved in CMakePresets.json; also honor the
        // user preset location so either override stays untouched.
        for (const char* file : {"CMakePresets.json", "CMakeUserPresets.json"}) {
            auto path = fs::path(projectPath) / file;
            if (!fs::exists(path)) continue;
            // Bound (GUI preset preflight): cap each preset JSON at the CLI's 16 MiB before parsing.
            auto data = Json::parse(readGuiUpdateFile(path), nullptr, false);
            if (!data.is_object() || !data.contains("vendor")) continue;
            const auto& vendor = data["vendor"];
            if (!vendor.is_object() || !vendor.contains("trussc")) continue;
            const auto& trussc = vendor["trussc"];
            if (trussc.contains("localPath") ||
                (trussc.contains("addonLocalPaths") && !trussc["addonLocalPaths"].empty())) {
                return cliRequired;
            }
        }
        auto addonsPath = fs::path(projectPath) / "addons.make";
        istringstream input;
        if (fs::exists(addonsPath)) {
            // Bound (GUI addon preflight): cap addons.make at the CLI's 16 MiB before scanning.
            input.str(readGuiUpdateFile(addonsPath));
        }
        string line;
        while (getline(input, line)) {
            const string name = addonNameFromLine(line);
            if (name.empty()) continue;
            // Unknown lines survive the merge and still require CLI resolution.
            if (find(settings.addons.begin(), settings.addons.end(), name) == settings.addons.end()) return cliRequired;
        }
        if (input.bad()) throw runtime_error("Failed to read " + addonsPath.string());
        vector<string> pending;
        for (size_t i = 0; i < settings.addons.size(); ++i) {
            if (settings.addonSelected[i]) pending.push_back(settings.addons[i]);
        }
        set<string> visited;
        for (size_t i = 0; i < pending.size(); ++i) {
            const string name = pending[i];
            auto spec = addonVersions::parse(name);
            auto installed = fs::path(settings.tcRoot) / "addons" / spec.name;
            if (!spec.owner.empty() || !spec.ref.empty() || !fs::is_directory(installed)) return cliRequired;
            if (!visited.insert(name).second) continue;
            auto manifest = installed / "addon.json";
            if (!fs::exists(manifest)) continue;
            // Bound (installed addon dependencies): cap each manifest at the CLI's 16 MiB before parsing.
            auto meta = Json::parse(readGuiUpdateFile(manifest));
            if (!meta.contains("dependencies")) continue;
            if (!meta["dependencies"].is_array()) throw runtime_error("addon dependencies must be an array: " + name);
            for (const auto& dependency : meta["dependencies"]) {
                // Bound (untrusted dependency graph): cap queued nodes and edges at the CLI's 4096.
                if (pending.size() >= 4096) throw runtime_error("addon dependency graph exceeds the 4096 node/edge limit");
                if (dependency.is_string()) pending.push_back(dependency.get<string>());
                else {
                    if (dependency.contains("commit") || dependency.contains("tag") ||
                        dependency.contains("branch") || dependency.contains("url")) return cliRequired;
                    pending.push_back(dependency.at("name").get<string>());
                }
            }
        }
        return "";
    } catch (const exception& e) {
        return e.what();
    }
}

static string addonOwnerFromURL(const string& url) {
    const string prefix = "https://github.com/";
    if (url.compare(0, prefix.size(), prefix) != 0) return "";
    auto identity = url.substr(prefix.size());
    if (identity.size() >= 4 && identity.substr(identity.size() - 4) == ".git") identity.resize(identity.size() - 4);
    return addonVersions::parse(identity).owner;
}

addonVersions::Paths resolveAddons(const string& project, const string& root,
    bool fetch, const addonVersions::Lookup& lookup, const vector<string>* selection) {
    return addonVersions::resolve(project, root, fetch, [&](const addonVersions::Spec& spec, bool mayFetch) {
        addonVersions::Source source;
        // Installed metadata identifies external sources even without a registry.
        auto installed = fs::path(root) / "addons" / spec.name / "addon.json";
        if (spec.owner.empty() && fs::exists(installed)) {
            // Bound (installed addon metadata): cap each manifest at 16 MiB.
            auto meta = Json::parse(trusscVersion::readBounded(installed, 16 * 1024 * 1024));
            source.owner = meta.value("owner", "");
            source.url = meta.value("url", "");
            if (source.owner.empty()) source.owner = addonOwnerFromURL(source.url);
            if (!source.owner.empty()) return source;
        }
        if (lookup) source = lookup(spec, mayFetch);
        if (source.owner.empty()) source.owner = addonOwnerFromURL(source.url);
        return source;
    }, selection);
}

string detectTcRoot(const fs::path& executablePath) {
    const char* envRoot = std::getenv("TRUSSC_DIR");
    if (envRoot && fs::exists(string(envRoot) + "/core/cmake/trussc_app.cmake")) {
        return string(envRoot);
    }

    // Walk up from the executable looking for core/cmake/trussc_app.cmake.
    // Typically trusscli lives in TRUSSC_ROOT/tools/bin/trusscli.app/Contents/MacOS/
    // (Linux/Windows: TRUSSC_ROOT/tools/bin/trusscli[.exe])
    // Use canonical() to resolve symlinks — when invoked via /usr/local/bin/trusscli,
    // the raw path would be the symlink location, not the actual binary.
    fs::path searchPath = fs::canonical(executablePath).parent_path();
    #ifdef __APPLE__
    // Climb out of MacOS -> Contents -> .app -> parent dir
    for (int i = 0; i < 3 && searchPath.has_parent_path(); ++i) {
        searchPath = searchPath.parent_path();
    }
    #endif
    for (int i = 0; i < 5 && searchPath.has_parent_path(); ++i) {
        if (fs::exists(searchPath / "core" / "cmake" / "trussc_app.cmake")) {
            return searchPath.string();
        }
        searchPath = searchPath.parent_path();
    }
    return "";
}

string detectProjectRoot(const string& startPath) {
    fs::path searchPath = fs::absolute(
        startPath.empty() ? fs::current_path() : fs::path(startPath));
    for (int i = 0; i <= 5; ++i) {
        if (fs::is_directory(searchPath / "src")) {
            return searchPath.string();
        }
        if (!searchPath.has_parent_path() ||
            searchPath == searchPath.parent_path()) break;
        searchPath = searchPath.parent_path();
    }
    return "";
}

vector<string> readProjectAddons(const string& projectPath) {
    vector<string> out;
    string path = projectPath + "/addons.make";
    if (!fs::exists(path)) return out;
    ifstream f(path);
    string line;
    while (getline(f, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        string name = line.substr(start, end - start + 1);
        if (name.empty() || name[0] == '#') continue;
        out.push_back(name);
    }
    return out;
}

static void writeAddonsMake(const string& projectPath,
                            const vector<string>& selectedAddons) {
    string path = projectPath + "/addons.make";
    ostringstream out;
    out << "# TrussC addons - one addon per line\n";
    for (const string& name : selectedAddons) {
        out << name << "\n";
    }
    writeGeneratedFile(path, out.str());
}

// Save only commit IDs resolved by remote acquisition, never local overrides.
static bool expandAddonPins(vector<string>& selected, const addonVersions::Paths& paths) {
    bool changed = false;
    for (auto& line : selected) {
        auto spec = addonVersions::parse(line);
        auto resolved = paths.resolvedPins.find(spec.name);
        if (resolved == paths.resolvedPins.end()) continue;
        const auto& ref = resolved->second;
        line.replace(line.size() - spec.ref.size(), spec.ref.size(), ref);
        changed = true;
    }
    return changed;
}


static void scanAddons(const string& tcRoot, vector<string>& addons) {
    addons.clear();
    if (tcRoot.empty()) return;
    string addonsPath = tcRoot + "/addons";
    if (!fs::exists(addonsPath)) return;
    for (const auto& entry : fs::directory_iterator(addonsPath)) {
        if (entry.is_directory()) {
            string name = entry.path().filename().string();
            if (name.substr(0, 3) == "tcx") addons.push_back(name);
        }
    }
    sort(addons.begin(), addons.end());
}

static void parseAddonsMake(const string& projectPath,
                            const vector<string>& availableAddons,
                            vector<int>& addonSelected) {
    addonSelected.assign(availableAddons.size(), 0);
    string addonsMakePath = projectPath + "/addons.make";
    if (!fs::exists(addonsMakePath)) return;

    // Bound (project addons.make): cap the file at the CLI's 16 MiB before scanning.
    istringstream addonsFile(readGuiUpdateFile(addonsMakePath));
    string line;
    while (getline(addonsFile, line)) {
        size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        string name = line.substr(start, end - start + 1);
        if (name.empty() || name[0] == '#') continue;
        for (size_t i = 0; i < availableAddons.size(); ++i) {
            if (availableAddons[i] == name) { addonSelected[i] = 1; break; }
        }
    }
}

// Selection precedes update validation, just as in the command-line flow.
static Result selectVersion(UpdateInput input, string value, bool local, bool clear,
                            const LogCallback& callback) {
    Result result;
    string spawnFailure;
    auto log = [&](const string& message, MessageKind kind) {
        if (kind == MessageKind::Warning) result.warnings.push_back(message);
        else result.logLines.push_back(message);
        if (kind == MessageKind::Diagnostic && message.find("Error: failed to spawn '") == 0) {
            spawnFailure = message;
        }
        if (callback) callback(message, kind);
    };
    ScopedDiagnostics diagnostics(log);
    try {
        if (input.projectPath.empty()) input.projectPath = detectProjectRoot();
        if (input.projectPath.empty() || !fs::is_directory(fs::path(input.projectPath) / "src")) {
            result.failureReason = "not inside a TrussC project";
            return result;
        }
        auto versionFile = fs::path(input.projectPath) / ".trussc-version";
        const bool selectingVersion = !local && !clear;
        // Acquire and check the new sources before changing either project selection.
        if (selectingVersion) {
            auto spec = trusscVersion::parse(value);
            auto root = trusscVersion::acquire(spec, true);
            if (spec.shortCommit) value.replace(value.size() - spec.ref.size(), spec.ref.size(), fs::path(root).filename().string());
        }
        if (local) trusscVersion::setLocalPath(input.projectPath, clear ? "" : value);
        else {
            if (clear) fs::remove(versionFile);
            else writeGeneratedFile(versionFile.string(), value + "\n");
            trusscVersion::setLocalPath(input.projectPath, "");
        }
        // The selected TrussC was already fetched; addons still refresh in update.
        input.fetchVersion = true;
        input.versionAcquired = selectingVersion;
        auto configureOutput = input.configureOutput;
        if (configureOutput) {
            input.configureOutput = [&](const string& project, const string& buildDir, const string& output, int code) {
                result.logLines.push_back(output);
                configureOutput(project, buildDir, output, code);
            };
        }
        auto regenerated = update(input, log);
        result.success = regenerated.success;
        result.exitCode = regenerated.exitCode;
        result.failureReason = regenerated.failureReason;
    } catch (const exception& error) {
        result.failureReason = error.what();
    }
    if (!result.success && !spawnFailure.empty() && result.failureReason.find(spawnFailure) == string::npos) {
        result.failureSummary = result.failureReason;
        result.failureReason += "\nDetail: " + spawnFailure;
    }
    return result;
}

Result use(const UseInput& input, const LogCallback& callback) {
    return selectVersion(input.update, input.spec, false, input.clear, callback);
}

Result useLocal(const UseLocalInput& input, const LogCallback& callback) {
    return selectVersion(input.update, input.path, true, input.clear, callback);
}

Result update(const UpdateInput& input, const LogCallback& callback) {
    Result result;
    auto log = [&](const string& message, MessageKind kind = MessageKind::Log) {
        if (kind == MessageKind::Warning) result.warnings.push_back(message);
        else result.logLines.push_back(message);
        if (callback) callback(message, kind);
    };
    ScopedDiagnostics diagnostics(log);
    try {
        string projectPath = input.projectPath.empty() ? detectProjectRoot() : input.projectPath;
        if (input.requirePlainProject) {
            ProjectSettings plainSettings = input.settings.value_or(ProjectSettings{});
            if (!input.settings) {
                plainSettings.tcRoot = input.tcRoot;
                scanAddons(plainSettings.tcRoot, plainSettings.addons);
                parseAddonsMake(projectPath, plainSettings.addons, plainSettings.addonSelected);
            }
            result.failureReason = validatePlainProject(projectPath, plainSettings);
            if (!result.failureReason.empty()) return result;
        }
        ProjectSettings settings;
        if (input.settings) {
            settings = *input.settings;
        } else {
            // Match CLI validation order: signature checks precede version writes.
            if (!projectPath.empty()) {
                result.failureReason = ProjectGenerator::validateProject(projectPath);
                if (!result.failureReason.empty()) return result;
            }
            if (projectPath.empty()) {
                result.failureReason = "not inside a TrussC project (no src/ directory found in CWD or any parent).\n"
                    "Use '-p <path>' or run from inside a project.";
                return result;
            }
            if (!fs::is_directory(projectPath)) {
                result.failureReason = "project path '" + projectPath + "' does not exist";
                return result;
            }
            if (!input.tcRoot.empty()) trusscVersion::setLocalPath(projectPath, input.tcRoot);
            string tcRoot = trusscVersion::resolve(projectPath, input.fetchVersion && !input.versionAcquired);
            if (tcRoot.empty()) tcRoot = detectTcRoot(input.executablePath);
            if (tcRoot.empty()) {
                result.failureReason = "could not detect TrussC root. Use --tc-root <path> or set TRUSSC_DIR.";
                return result;
            }
            auto paths = resolveAddons(projectPath, tcRoot, input.fetchVersion, input.addonLookup);
            if (input.fetchVersion) {
                auto selected = readProjectAddons(projectPath);
                if (expandAddonPins(selected, paths)) writeAddonsMake(projectPath, selected);
            }
            addonVersions::saveResolved(projectPath, paths);
            vector<string> availableAddons;
            scanAddons(tcRoot, availableAddons);
            for (const auto& spec : readProjectAddons(projectPath)) {
                if (find(availableAddons.begin(), availableAddons.end(), spec) == availableAddons.end()) availableAddons.push_back(spec);
            }
            vector<int> selected;
            parseAddonsMake(projectPath, availableAddons, selected);
            auto setup = prepareRegeneration(projectPath, tcRoot, availableAddons, selected, input.flags);
            for (const auto& warning : setup.warnings) log(warning, MessageKind::Warning);
            if (!setup.summary.empty()) log(setup.summary);
            if (!setup.leftoverNotice.empty()) log(setup.leftoverNotice);
            settings = setup.settings;
        }
        ProjectGenerator generator(settings);
        generator.setLogCallback([&](const string& message) { log(message); });
        if (input.configureOutput) {
            generator.setConfigureOutputCallback([&](const string& buildDir, const string& output, int code) {
                result.logLines.push_back(output);
                input.configureOutput(projectPath, buildDir, output, code);
            });
        }
        result.failureReason = generator.update(projectPath, input.requirePlainProject);
        for (const auto& warning : generator.getWarnings()) log(warning, MessageKind::Warning);
        if (!result.failureReason.empty()) return result;
        if (!input.settings) log("Project updated: " + projectPath);
        result.success = true;
        result.exitCode = 0;
    } catch (const exception& error) {
        result.failureReason = error.what();
    }
    return result;
}
}
