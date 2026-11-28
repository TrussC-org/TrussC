#include "ProjectHub.h"
#include "PresetRoot.h"
#include "TrussCVersion.h"
#include "TrussC.h"
#include "tc/utils/tcFileIO.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <shellapi.h>
#endif

namespace projectHub {
namespace fs = std::filesystem;
using tc::pathToUtf8;
using tc::utf8ToPath;

bool usableFramework(const fs::path& root) {
    return !root.empty() && fs::is_regular_file(root / "core/cmake/trussc_app.cmake");
}
Snapshot inspect(const fs::path& input, const std::string& fallbackRoot) {
    Snapshot out;
    projectOps::ScopedDiagnostics diagnostics([&](const std::string& message, projectOps::MessageKind) {
        out.diagnostics.push_back(message);
    });
    try {
        auto path = fs::absolute(input).lexically_normal();
        out.path = pathToUtf8(path);
        out.name = pathToUtf8(path.filename());
        if (!fs::is_directory(path / "src")) {
            out.error = "Project folder must contain src/: " + out.path;
            return out;
        }
        out.error = ProjectGenerator::validateProject(out.path);
        if (!out.error.empty()) return out;
        out.presets = readPresetState(out.path);
        out.diagnostics.insert(out.diagnostics.end(), out.presets.warnings.begin(), out.presets.warnings.end());
        if (!out.presets.ideWarning.empty()) out.diagnostics.push_back(out.presets.ideWarning);
        if (fs::exists(path / ".trussc-version")) {
            auto pin = trusscVersion::readBounded(path / ".trussc-version", 4096);
            auto begin = pin.find_first_not_of(" \t\r\n");
            if (begin != std::string::npos) out.pin = pin.substr(begin, pin.find_last_not_of(" \t\r\n") - begin + 1);
        }
        // A broken explicit override is disclosed, never replaced by fallback.
        out.localRoot = trusscVersion::localPath(path);
        try {
            out.root = trusscVersion::resolve(path, false, &out.missing);
            if (out.root.empty() && out.pin.empty() && out.localRoot.empty()) {
                if (fs::exists(path / "CMakePresets.json")) {
                    // Bound (project presets): use the build reader's 16 MiB metadata limit.
                    auto data = tc::Json::parse(trusscVersion::readBounded(path / "CMakePresets.json", 16 * 1024 * 1024), nullptr, false);
                    out.root = projectOps::presetAddonRoot(out.path,
                        projectOps::kNativePreset ? projectOps::kNativePreset : "", {}, data);
                }
                if (out.root.empty()) out.root = fallbackRoot;
            }
            if (usableFramework(utf8ToPath(out.root))) out.commit = trusscVersion::commitAt(utf8ToPath(out.root));
        } catch (const std::exception& e) { out.diagnostics.push_back(e.what()); }
        std::istringstream lines(fs::exists(path / "addons.make") ?
            trusscVersion::readBounded(path / "addons.make", 16 * 1024 * 1024) : "");
        std::string line;
        while (std::getline(lines, line)) {
            auto begin = line.find_first_not_of(" \t\r\n");
            if (begin == std::string::npos || line[begin] == '#') continue;
            out.addons.push_back(line.substr(begin, line.find_last_not_of(" \t\r\n") - begin + 1));
        }
        if (usableFramework(utf8ToPath(out.root))) {
            try { projectOps::resolveAddons(out.path, out.root, false, {}, nullptr, &out.missing); }
            catch (const std::exception& e) { out.diagnostics.push_back(e.what()); }
        }
    } catch (const std::exception& e) { out.error = e.what(); }
    return out;
}

projectOps::Result create(const fs::path& destination, ProjectSettings settings,
                          const projectOps::LogCallback& log) {
    projectOps::Result result;
    try {
        auto path = fs::absolute(destination).lexically_normal();
        if (path.filename().empty()) throw std::runtime_error("Project folder name is required");
        if (fs::exists(path)) throw std::runtime_error("Destination already exists: " + pathToUtf8(path));
        if (!usableFramework(utf8ToPath(settings.tcRoot))) throw std::runtime_error("TrussC folder not found: " + settings.tcRoot);
        settings.projectName = pathToUtf8(path.filename());
        settings.projectDir = pathToUtf8(path.parent_path());
        settings.templatePath = pathToUtf8(utf8ToPath(settings.tcRoot) / "examples/templates/emptyExample");
        settings.detectBuildEnvironment();
        ProjectGenerator generator(settings);
        generator.setLogCallback([&](const std::string& message) {
            result.logLines.push_back(message);
            if (log) log(message, projectOps::MessageKind::Log);
        });
        result.failureReason = generator.generate();
        result.warnings = generator.getWarnings();
        for (const auto& warning : result.warnings) if (log) log(warning, projectOps::MessageKind::Warning);
        result.success = result.failureReason.empty();
        result.exitCode = result.success ? 0 : 1;
    } catch (const std::exception& e) { result.failureReason = e.what(); }
    if (!result.success) result.failureReason = pathToUtf8(destination) + "\n" + result.failureReason;
    return result;
}

static fs::path homeDirectory() {
#ifdef _WIN32
    const char* home = std::getenv("USERPROFILE");
#else
    const char* home = std::getenv("HOME");
#endif
    if (!home) throw std::runtime_error("Home directory is unavailable");
    return utf8ToPath(home);
}
fs::path documentsDirectory() {
#ifdef _WIN32
    PWSTR path = nullptr;
    auto status = SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &path);
    if (SUCCEEDED(status)) {
        fs::path result(path);
        CoTaskMemFree(path);
        return result;
    }
#elif !defined(__APPLE__)
    auto home = homeDirectory();
    const char* config = std::getenv("XDG_CONFIG_HOME");
    std::ifstream file((config && *config ? utf8ToPath(config) : home / ".config") / "user-dirs.dirs");
    std::string line;
    while (std::getline(file, line)) {
        const std::string key = "XDG_DOCUMENTS_DIR=";
        if (line.compare(0, key.size(), key) != 0) continue;
        auto first = line.find('"'), last = line.rfind('"');
        if (first == last || first == std::string::npos) continue;
        auto value = line.substr(first + 1, last - first - 1);
        if (value.compare(0, 5, "$HOME") == 0) value.replace(0, 5, pathToUtf8(home));
        auto path = utf8ToPath(value);
        if (path.is_absolute()) return path;
    }
#endif
    return homeDirectory() / "Documents";
}
fs::path preferencesPath() { return homeDirectory() / ".trussc/gui2.json"; }
bool defaultIsFramework(const fs::path& directory) {
    if (!fs::is_directory(directory / "core")) return false;
    for (const char* tool : {"trusscli", "trusscli.exe", "trusscli.app",
                             "tools/bin/trusscli", "tools/bin/trusscli.exe", "tools/bin/trusscli.app"}) {
        if (fs::exists(directory / tool)) return true;
    }
    return false;
}
std::string revealFolder(const fs::path& directory) {
    try {
        if (!fs::is_directory(directory)) return "Folder not found: " + pathToUtf8(directory);
        auto path = fs::absolute(directory);
#ifdef _WIN32
        auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
        if (result > 32) return {};
#else
#ifdef __APPLE__
        const char* command = "open";
#else
        const char* command = "xdg-open";
#endif
        std::string output;
        auto result = projectOps::runProcess({command, pathToUtf8(path)},
            [&](const char* data, size_t size) { output.append(data, size); });
        if (result.exitCode == 0) return {};
        return "Could not open folder: " + pathToUtf8(path) + "\n" + output;
#endif
        return "Could not open folder: " + pathToUtf8(path);
    } catch (const std::exception& e) { return e.what(); }
}
std::string japaneseFont() {
#ifdef _WIN32
    const char* systemRoot = std::getenv("WINDIR");
    auto fonts = utf8ToPath(systemRoot ? systemRoot : "C:/Windows") / "Fonts";
    for (const char* name : {"YuGothR.ttc", "meiryo.ttc", "msgothic.ttc"}) {
        auto path = fonts / name;
        if (fs::is_regular_file(path)) return pathToUtf8(path);
    }
#else
#ifdef __APPLE__
    const char* paths[] = {"/System/Library/Fonts/ヒラギノ角ゴシック W3.ttc", "/System/Library/Fonts/ヒラギノ丸ゴ ProN W4.ttc"};
#else
    const char* paths[] = {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc", "/usr/share/fonts/opentype/ipafont-gothic/ipag.ttf"};
#endif
    for (auto path : paths) if (fs::is_regular_file(path)) return path;
#endif
    return {};
}
}
