#include "ProjectOps.h"
#include "Dist.h"
#include "ProjectName.h"
#include "TrussCVersion.h"
#include "TrussC.h"
#include <cstdlib>
#include <deque>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <streambuf>
#ifdef __linux__
#include <unistd.h>
#endif

using namespace std;
using namespace tc;
namespace fs = std::filesystem;
namespace projectOps {
namespace {
// Distribution/info retain their original streaming reader; build's bounded
// selection reader belongs to its regeneration preflight.
vector<string> readDistributionAddons(const string& projectPath) {
    vector<string> out;
    const string path = projectPath + "/addons.make";
    if (!fs::exists(path)) return out;
    ifstream file(path);
    string line;
    while (getline(file, line)) {
        const size_t start = line.find_first_not_of(" \t");
        if (start == string::npos) continue;
        const size_t end = line.find_last_not_of(" \t\r\n");
        const string name = line.substr(start, end - start + 1);
        if (name.empty() || name[0] == '#') continue;
        out.push_back(name);
    }
    return out;
}
} // namespace
void collectAddonDependencies(const string& tcRoot,
                                     const string& addonName,
                                     set<string>& seen,
                                     vector<string>& out) {
    string jsonPath = tcRoot + "/addons/" + addonName + "/addon.json";
    if (!fs::exists(jsonPath)) return;
    Json meta;
    try {
        // Bound (installed addon dependencies): cap each manifest at 16 MiB.
        meta = Json::parse(trusscVersion::readBounded(jsonPath, 16 * 1024 * 1024));
    } catch (...) {
        return;
    }
    if (!meta.contains("dependencies") || !meta["dependencies"].is_array()) return;
    for (const auto& d : meta["dependencies"]) {
        string dep;
        if (d.is_string()) dep = d.get<string>();
        else if (d.is_object()) {
            // Version pins remain dependency selections, not project overrides.
            if (d.contains("commit") || d.contains("tag") || d.contains("branch")) continue;
            dep = d.value("name", "");
        }
        if (dep.empty() || dep.find('@') != string::npos || seen.count(dep) ||
            !fs::is_directory(fs::path(tcRoot) / "addons" / dep)) continue;
        seen.insert(dep);
        out.push_back(dep);
        collectAddonDependencies(tcRoot, dep, seen, out);  // transitive
    }
}
vector<pair<string, fs::path>> projectAddonDirs(const string& projectPath, const string& tcRoot) {
    vector<string> names = readDistributionAddons(projectPath);
    set<string> seen(names.begin(), names.end());
    const vector<string> direct = names;
    for (const string& name : direct) collectAddonDependencies(tcRoot, name, seen, names);
    vector<pair<string, fs::path>> out;
    for (const string& name : names) out.emplace_back(name, fs::path(tcRoot) / "addons" / name);
    return out;
}

namespace {
#ifdef __APPLE__
constexpr const char* kNativePreset = "macos";
#elif defined(_WIN32)
constexpr const char* kNativePreset = "windows";
#elif defined(__linux__)
constexpr const char* kNativePreset = "linux";
#else
constexpr const char* kNativePreset = nullptr;
#endif

struct DistStopped { int exitCode; };
class OutputBuffer : public streambuf {
public:
    explicit OutputBuffer(function<void(const string&)> callback) : callback_(std::move(callback)) {}
private:
    streamsize xsputn(const char* data, streamsize size) override {
        callback_(string(data, static_cast<size_t>(size)));
        return size;
    }
    int_type overflow(int_type value) override {
        if (!traits_type::eq_int_type(value, traits_type::eof())) callback_(string(1, traits_type::to_char_type(value)));
        return traits_type::not_eof(value);
    }
    function<void(const string&)> callback_;
};

struct DistOperation {
    const DistInput& options;
    Result& result;
    const LogCallback& emit;
    ostream& cout;
    ostream& cerr;

    void checkStop() const {
        if (options.build.cancel && options.build.cancel->stopRequested()) throw DistStopped{-1};
    }

    int launch(const vector<string>& argv, const function<void(const char*, size_t)>& output, bool inherit) {
        ProcessOptions childOptions;
        childOptions.cancel = options.build.cancel;
        childOptions.inheritOutput = inherit && options.inheritOutput;
        childOptions.hideConsole = options.build.cancel != nullptr; // Known console tools in a layer call.
        const auto child = runProcess(argv, output, childOptions);
        if (child.stopped) throw DistStopped{child.exitCode};
        return child.exitCode;
    }
    int runInherited(const vector<string>& argv) {
        return launch(argv, [&](const char* data, size_t size) {
            emit(string(data, size), MessageKind::Output);
        }, true);
    }
    int runCaptured(const vector<string>& argv, const function<void(const char*, size_t)>& output) {
        return launch(argv, output, false);
    }
    int distInit(const string& projectPath, const string& projectName,
                        string appName, string appVersion, string author, bool authorGiven) {
        const fs::path configPath = fs::path(projectPath) / "dist.json";
        if (fs::exists(configPath)) {
            cerr << "Error: " << configPath.string() << " already exists (not overwritten).\n";
            return 1;
        }
        const bool ask = static_cast<bool>(options.prompt);
        if (appName.empty()) {
            appName = projectName;
            if (ask) {
                string answer = options.prompt("Name [" + projectName + "]: ");
                if (!answer.empty()) appName = answer;
            }
        }
        string version;
        if (!appVersion.empty()) {
            if (!dist::normalizeVersion(appVersion, version)) {
                cerr << "Error: --app-version must be three numbers like 1.2.0 (got \"" << appVersion << "\").\n";
                return 1;
            }
        } else {
            version = dist::kDefaultVersion;
            while (ask) {
                string answer = options.prompt(string("Version [") + dist::kDefaultVersion + "]: ");
                if (answer.empty()) break;
                if (dist::normalizeVersion(answer, version)) break;
                cout << "  A version is three numbers, like 1.2.0.\n";
                version = dist::kDefaultVersion;
            }
        }
        if (ask && !authorGiven) author = options.prompt("Author (person, studio or company) []: ");
        ofstream file(configPath, ios::binary);
        file << dist::configTemplate(appName, version, author);
        if (!file) {
            cerr << "Error: cannot write " << configPath.string() << "\n";
            return 1;
        }
        cout << "Wrote dist.json. Edit it for include / exclude / licenses, then run 'trusscli dist'.\n"
             << "  eulaFile  your license terms, shipped as EULA.txt; \"\" = EULA.txt in the project\n"
             << "            folder if there is one (a file named here must exist)\n"
             << "  include   [{\"from\": \"../tools/node\", \"to\": \"data/tools/node\"}]  files from outside bin/data\n"
             << "  exclude   [\"data/**/*.psd\"]                                    left out of the package\n"
             << "  licenses  [{\"name\": \"Node.js\", \"file\": \"...\"}]                  your own third-party notices\n";
        return 0;
    }

    string readCacheValue(const fs::path& cache, const string& key) {
        ifstream file(cache);
        string line;
        const string prefix = key + ":";
        while (getline(file, line)) {
            if (line.rfind(prefix, 0) != 0) continue;
            const size_t eq = line.find('=');
            if (eq != string::npos) return line.substr(eq + 1);
        }
        return "";
    }

    // NSIS's compiler: on PATH, or where the NSIS installer puts it. Empty if none.
    fs::path findMakensis() {
#ifdef _WIN32
        if (const char* path = std::getenv("PATH")) {
            stringstream dirs(path);
            string dir;
            while (getline(dirs, dir, ';')) {
                if (dir.empty()) continue;
                const fs::path candidate = fs::path(dir) / "makensis.exe";
                error_code ec;
                if (fs::is_regular_file(candidate, ec)) return candidate;
            }
        }
        for (const char* variable : {"ProgramFiles(x86)", "ProgramFiles"}) {
            if (const char* root = std::getenv(variable)) {
                const fs::path candidate = fs::path(root) / "NSIS" / "makensis.exe";
                error_code ec;
                if (fs::is_regular_file(candidate, ec)) return candidate;
            }
        }
#endif
        return {};
    }

    // Windows' own bsdtar (%SystemRoot%\System32\tar.exe, Windows 10 1803 and
    // later). Not looked up on PATH, where another tar (Git's GNU tar) may come
    // first. Empty elsewhere or when it is missing.
    fs::path findSystemTar() {
#ifdef _WIN32
        if (const char* root = std::getenv("SystemRoot")) {
            const fs::path candidate = fs::path(root) / "System32" / "tar.exe";
            error_code ec;
            if (fs::is_regular_file(candidate, ec)) return candidate;
        }
#endif
        return {};
    }

#ifdef _WIN32
    bool isAscii(const string& text) {
        for (unsigned char c : text) if (c >= 0x80) return false;
        return true;
    }
#endif

    // Builds <folder>-setup.exe with NSIS next to the package. The script and the
    // page texts go to a <folder>-installer/ work folder, removed on success and
    // kept on failure. Returns the installer path, or empty with `error`.
    fs::path buildWindowsInstaller(const fs::path& makensis, const dist::PlanInput& input,
                                          const dist::Plan& plan, string& error) {
        error_code ec;
        const fs::path work = input.outputDir / (plan.folderName + "-installer");
        const fs::path setup = input.outputDir / (plan.folderName + "-setup.exe");
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);
        fs::remove(setup, ec);

        dist::NsisInput nsis;
        nsis.name = input.config.name;
        nsis.version = input.config.version;
        nsis.author = input.config.author;
        nsis.arch = plan.arch;
        nsis.executable = plan.executableName;
        nsis.packageDir = plan.packageDir;
        nsis.files = dist::listPackageFiles(plan.packageDir);
        nsis.setupExe = setup;
        nsis.options = input.config.installer;
        if (input.config.installer.showEula) {
            nsis.eulaPage = work / "EULA.txt";
            if (!dist::writeWithBom(plan.packageDir / "EULA.txt", nsis.eulaPage)) {
                error = "cannot prepare the EULA page";
                return {};
            }
        }
        if (input.config.installer.showNotices) {
            nsis.noticesPage = work / "THIRD_PARTY_NOTICES.txt";
            if (!dist::writeWithBom(plan.packageDir / "THIRD_PARTY_NOTICES.txt", nsis.noticesPage)) {
                error = "cannot prepare the notices page";
                return {};
            }
        }
        const fs::path script = work / "setup.nsi";
        {
            ofstream file(script, ios::binary);
            file << "\xEF\xBB\xBF" << dist::nsisScript(nsis);
            if (!file) {
                error = "cannot write " + script.string();
                return {};
            }
        }
        cout << "Building the installer with NSIS ...\n";
        if (runInherited({makensis.string(), "/V2", "/INPUTCHARSET", "UTF8", script.string()}) != 0 ||
            !fs::is_regular_file(setup, ec)) {
            error = "makensis failed; the script is kept in " + work.string();
            return {};
        }
        fs::remove_all(work, ec);
        return setup;
    }

#ifdef __linux__
    // A program on PATH, or empty.
    fs::path findOnPath(const string& name) {
        if (const char* path = std::getenv("PATH")) {
            stringstream dirs(path);
            string dir;
            while (getline(dirs, dir, ':')) {
                if (dir.empty()) continue;
                const fs::path candidate = fs::path(dir) / name;
                if (access(candidate.c_str(), X_OK) == 0) return candidate;
            }
        }
        return {};
    }

    // The output of a command (stdout and stderr together), or nullopt when it
    // fails.
    optional<string> captureCommand(const vector<string>& argv) {
        string output;
        const int code = runCaptured(argv, [&](const char* data, size_t size) { output.append(data, size); });
        if (code != 0) return nullopt;
        return output;
    }

    // The distribution packages the app depends on: the owners of the shared
    // libraries that the ELF files in the package load directly. Libraries
    // shipped inside the package are not dependencies. False with `error` when a
    // library cannot be found or has no owning package.
    bool pacmanDepends(const fs::path& packageDir, vector<string>& depends, string& error) {
        error_code ec;
        vector<fs::path> elfFiles;
        set<string> shipped;
        for (const auto& entry : fs::recursive_directory_iterator(packageDir, ec)) {
            if (!entry.is_regular_file(ec)) continue;
            shipped.insert(entry.path().filename().string());
            if (dist::readBinaryInfo(entry.path()).format == "elf") elfFiles.push_back(entry.path());
        }
        set<string> needed;
        map<string, string> resolved;
        for (const fs::path& file : elfFiles) {
            const auto objdump = captureCommand({"objdump", "-p", file.string()});
            if (!objdump) { error = "objdump failed on " + file.string(); return false; }
            for (const string& name : dist::parseNeeded(*objdump)) {
                if (!shipped.count(name)) needed.insert(name);
            }
            // ldd exits non-zero for a library that is not an executable's; the
            // libraries it did resolve are still in the output.
            string lddOut;
            runCaptured({"ldd", file.string()}, [&](const char* data, size_t size) { lddOut.append(data, size); });
            for (const auto& [name, path] : dist::parseLdd(lddOut)) resolved.emplace(name, path);
        }
        vector<string> paths;
        for (const string& name : needed) {
            auto it = resolved.find(name);
            if (it == resolved.end()) {
                error = name + " is not found on this system";
                return false;
            }
            paths.push_back(fs::canonical(it->second, ec).string());
        }
        if (paths.empty()) return true;
        vector<string> argv = {"pacman", "-Qqo"};
        argv.insert(argv.end(), paths.begin(), paths.end());
        const auto owners = captureCommand(argv);
        if (!owners) {
            error = "pacman could not name the package of every library (one is not installed by pacman?)";
            return false;
        }
        set<string> unique;
        stringstream lines(*owners);
        string line;
        while (getline(lines, line)) {
            if (!line.empty()) unique.insert(line);
        }
        depends.assign(unique.begin(), unique.end());
        return true;
    }

    // The menu icon: the first PNG in the project's icon/ folder, else TrussC's.
    // Empty if neither exists.
    fs::path linuxAppIcon(const dist::PlanInput& input, const fs::path& tcRoot) {
        error_code ec;
        vector<fs::path> pngs;
        for (const auto& entry : fs::directory_iterator(input.projectDir / "icon", ec)) {
            if (entry.is_regular_file(ec) && entry.path().extension() == ".png") pngs.push_back(entry.path());
        }
        sort(pngs.begin(), pngs.end());
        fs::path icon = !pngs.empty() ? pngs.front() : tcRoot / "core" / "resources" / "default.png";
        return fs::is_regular_file(icon, ec) ? icon : fs::path();
    }

    // The Start menu entry of an app installed to /opt/<package>/.
    string linuxDesktopEntry(const dist::PlanInput& input, const dist::Plan& plan, const string& package) {
        dist::DesktopEntryInput desktop;
        desktop.name = input.config.name;
        desktop.exec = "/opt/" + package + "/" + plan.launcherName;
        desktop.icon = package;
        desktop.wmClass = plan.executableName;  // TrussC sets WM_CLASS to it
        return dist::desktopEntry(desktop);
    }

    string linuxPackageDescription(const dist::Config& config) {
        return config.name + (config.author.empty() ? "" : " by " + config.author);
    }

    // Builds a pacman package (<pkgname>-<version>-1-<arch>.pkg.tar.zst) of the
    // dist folder with makepkg, next to it. The PKGBUILD and makepkg's work go to
    // a <folder>-pacman/ folder, removed on success and kept on failure. Returns
    // the package path, or empty with `error`.
    fs::path buildPacmanPackage(const dist::PlanInput& input, const dist::Plan& plan,
                                       const fs::path& tcRoot, string& error) {
        error_code ec;
        const fs::path work = input.outputDir / (plan.folderName + "-pacman");
        fs::remove_all(work, ec);
        fs::create_directories(work, ec);

        dist::PacmanInput pkg;
        pkg.pkgname = dist::linuxPackageName(input.config.name);
        pkg.version = input.config.version;
        pkg.description = linuxPackageDescription(input.config);
        pkg.arch = dist::pacmanArch(plan.arch);
        pkg.packageDir = plan.packageDir;
        pkg.launcher = plan.launcherName;

        cout << "Finding the packages " << input.config.name << " depends on ...\n";
        if (!pacmanDepends(plan.packageDir, pkg.depends, error)) return {};
        cout << "  depends:";
        for (const string& d : pkg.depends) cout << " " << d;
        cout << "\n";

        pkg.icon = linuxAppIcon(input, tcRoot);
        pkg.desktopFile = work / (pkg.pkgname + ".desktop");
        {
            ofstream(pkg.desktopFile, ios::binary) << linuxDesktopEntry(input, plan, pkg.pkgname);
            ofstream(work / "PKGBUILD", ios::binary) << dist::pkgbuild(pkg);
            if (!fs::is_regular_file(work / "PKGBUILD", ec)) {
                error = "cannot write " + (work / "PKGBUILD").string();
                return {};
            }
        }

        // makepkg writes the package to PKGDEST.
        const string pkgdest = input.outputDir.string();
        for (const auto& entry : fs::directory_iterator(input.outputDir, ec)) {
            const string file = entry.path().filename().string();
            if (file.rfind(pkg.pkgname + "-" + pkg.version + "-1-" + pkg.arch + ".pkg.tar", 0) == 0)
                fs::remove(entry.path(), ec);
        }
        cout << "Building the pacman package with makepkg ...\n";
        int rc;
        {
            struct ClearPkgdest { ~ClearPkgdest() { unsetenv("PKGDEST"); } } clearPkgdest;
            setenv("PKGDEST", pkgdest.c_str(), 1);
            rc = runInherited({"makepkg", "--force", "--nodeps", "--noconfirm", "-D", work.string()});
        }
        fs::path built;
        for (const auto& entry : fs::directory_iterator(input.outputDir, ec)) {
            const string file = entry.path().filename().string();
            if (file.rfind(pkg.pkgname + "-" + pkg.version + "-1-" + pkg.arch + ".pkg.tar", 0) == 0) built = entry.path();
        }
        if (rc != 0 || built.empty()) {
            error = "makepkg failed; the PKGBUILD is kept in " + work.string();
            return {};
        }
        fs::remove_all(work, ec);
        return built;
    }

    // Builds <package>_<version>_<arch>.deb of the dist folder with dpkg-deb, next
    // to it. The package tree is staged in a <folder>-deb/ folder (removed on
    // success, kept on failure); its ELF files are stripped of debug info, and
    // dpkg-shlibdeps names the packages (with minimum versions) that provide the
    // libraries they load. Returns the .deb path, or empty with `error`.
    fs::path buildDebPackage(const dist::PlanInput& input, const dist::Plan& plan,
                                    const fs::path& tcRoot, string& error) {
        error_code ec;
        const fs::path work = input.outputDir / (plan.folderName + "-deb");
        const fs::path root = work / "root";
        fs::remove_all(work, ec);

        dist::DebInput deb;
        deb.package = dist::linuxPackageName(input.config.name);
        deb.version = input.config.version;
        deb.arch = dist::debArch(plan.arch);
        deb.maintainer = input.config.author.empty() ? input.config.name : input.config.author;
        deb.description = linuxPackageDescription(input.config);

        // --- The tree: /opt/<package>, /usr/bin link, menu entry and icon ---
        const fs::path opt = root / "opt" / deb.package;
        fs::create_directories(opt.parent_path(), ec);
        fs::copy(plan.packageDir, opt, fs::copy_options::recursive | fs::copy_options::copy_symlinks, ec);
        if (ec) {
            error = "cannot stage the package in " + work.string() + ": " + ec.message();
            return {};
        }
        fs::create_directories(root / "usr" / "bin", ec);
        fs::create_symlink("/opt/" + deb.package + "/" + plan.launcherName, root / "usr" / "bin" / deb.package, ec);
        fs::create_directories(root / "usr" / "share" / "applications", ec);
        ofstream(root / "usr" / "share" / "applications" / (deb.package + ".desktop"), ios::binary)
            << linuxDesktopEntry(input, plan, deb.package);
        if (const fs::path icon = linuxAppIcon(input, tcRoot); !icon.empty()) {
            fs::create_directories(root / "usr" / "share" / "pixmaps", ec);
            fs::copy_file(icon, root / "usr" / "share" / "pixmaps" / (deb.package + ".png"),
                          fs::copy_options::overwrite_existing, ec);
        }

        // --- ELF files: strip debug info, then find what they depend on ---
        vector<string> elfFiles;
        for (const auto& entry : fs::recursive_directory_iterator(opt, ec)) {
            if (entry.is_regular_file(ec) && !entry.is_symlink(ec) &&
                dist::readBinaryInfo(entry.path()).format == "elf")
                elfFiles.push_back(entry.path().string());
        }
        for (const string& file : elfFiles) {
            if (runInherited({"strip", "--strip-debug", file}) != 0) {
                error = "strip failed on " + file;
                return {};
            }
        }
        cout << "Finding the packages " << input.config.name << " depends on ...\n";
        {
            // dpkg-shlibdeps reads debian/control in its working folder.
            fs::create_directories(work / "debian", ec);
            ofstream(work / "debian" / "control", ios::binary)
                << "Source: " << deb.package << "\n\nPackage: " << deb.package << "\nArchitecture: any\n";
            // -l: libraries shipped in the package are found there, not reported missing.
            vector<string> argv = {"sh", "-c", "cd \"$1\" && shift && exec dpkg-shlibdeps \"$@\"", "sh",
                                   work.string(), "-O", "--ignore-missing-info", "-l" + opt.string()};
            argv.insert(argv.end(), elfFiles.begin(), elfFiles.end());
            const auto output = captureCommand(argv);
            if (!output) {
                error = "dpkg-shlibdeps failed; the staged package is kept in " + work.string();
                return {};
            }
            deb.depends = dist::parseShlibdeps(*output);
            cout << "  depends: " << (deb.depends.empty() ? string("(none)") : deb.depends) << "\n";
            fs::remove_all(work / "debian", ec);
        }

        // --- DEBIAN/control ---
        uintmax_t bytes = 0;
        for (const auto& entry : fs::recursive_directory_iterator(root, ec)) {
            if (entry.is_regular_file(ec) && !entry.is_symlink(ec)) bytes += entry.file_size(ec);
        }
        deb.installedSizeKiB = (bytes + 1023) / 1024;
        fs::create_directories(root / "DEBIAN", ec);
        ofstream(root / "DEBIAN" / "control", ios::binary) << dist::debControl(deb);

        const fs::path out = input.outputDir / (deb.package + "_" + deb.version + "_" + deb.arch + ".deb");
        fs::remove(out, ec);
        cout << "Building the .deb with dpkg-deb ...\n";
        if (runInherited({"dpkg-deb", "--build", "--root-owner-group", root.string(), out.string()}) != 0 ||
            !fs::is_regular_file(out, ec)) {
            error = "dpkg-deb failed; the staged package is kept in " + work.string();
            return {};
        }
        fs::remove_all(work, ec);
        return out;
    }
#endif

    int execute() {
        const auto& explicitPath = options.build.update.projectPath;
        const auto& outputDir = options.outputDir;
        const auto& appName = options.appName;
        const auto& appVersion = options.appVersion;
        const auto& appAuthor = options.appAuthor;
        const bool appAuthorGiven = options.appAuthorGiven;
        const bool release = options.release, archive = options.archive;
        const bool noBuild = options.noBuild, dryRun = options.dryRun, installer = options.installer;
        const string projectPath = explicitPath.empty() ? detectProjectRoot() : explicitPath;
        if (projectPath.empty() || !fs::is_directory(projectPath)) {
            cerr << "Error: not inside a TrussC project.\n"
                 << "Use 'trusscli dist -p <path>' or run from inside a project.\n";
            return 1;
        }
        error_code ec;
        const fs::path projectDir = fs::weakly_canonical(fs::absolute(projectPath), ec);
        if (ec || projectDir.empty()) {
            cerr << "Error: cannot resolve the project folder " << projectPath << ": "
                 << (ec ? ec.message() : string("empty path")) << "\n";
            return 1;
        }
        // The folder name is what people see (the app name by default); the
        // executable / bundle is named after the CMake target (see targetName below).
        string projectName = projectDir.filename().string();
        if (projectName.empty()) projectName = projectDir.parent_path().filename().string();

        if (options.init) return distInit(projectDir.string(), projectName, appName, appVersion, appAuthor, appAuthorGiven);

        if (!kNativePreset) {
            cerr << "Error: cannot detect the native platform.\n";
            return 1;
        }
        vector<string> missing;
        const bool mayFetch = options.build.confirmFetch && !noBuild && !dryRun;
        string tcRoot = options.build.update.tcRoot;
        if (tcRoot.empty()) tcRoot = trusscVersion::resolve(projectPath, false, mayFetch ? &missing : nullptr);
        if (tcRoot.empty() && !options.build.update.executablePath.empty()) tcRoot = detectTcRoot(options.build.update.executablePath);
        if (tcRoot.empty() && missing.empty()) {
            cerr << "Error: could not detect the TrussC root.\n";
            return 1;
        }

        // --- Settings: dist.json, then the command line ---
        dist::Config config;
        const fs::path configPath = projectDir / "dist.json";
        const bool haveConfig = fs::exists(configPath);
        if (haveConfig) {
            ifstream file(configPath, ios::binary);
            stringstream text;
            text << file.rdbuf();
            vector<string> warnings;
            string error;
            if (!dist::parseConfig(text.str(), config, warnings, error)) {
                cerr << "Error: " << error << "\n";
                return 1;
            }
            for (const string& w : warnings) emit(w, MessageKind::Warning);
        }
        if (!appName.empty()) config.name = appName;
        if (!appVersion.empty()) config.version = appVersion;
        if (appAuthorGiven) config.author = appAuthor;
        if (config.name.empty()) config.name = projectName;
        if (config.version.empty()) config.version = dist::kDefaultVersion;
        {
            string version;
            if (!dist::normalizeVersion(config.version, version)) {
                cerr << "Error: the version must be three numbers like 1.2.0 (got \"" << config.version << "\").\n"
                     << "Set it in dist.json \"version\" or with --app-version.\n";
                return 1;
            }
            config.version = version;
        }

        // --- Build, or check the existing one ---
        const fs::path buildDir = projectDir / (string("build-") + kNativePreset);
        if (!noBuild && !dryRun) {
            auto buildInput = options.build;
            buildInput.update.projectPath = projectDir.string();
            buildInput.targetPreset.clear();
            buildInput.buildType = release ? "Release" : "RelWithDebInfo";
            buildInput.rememberType = false;
            auto built = build(buildInput, emit);
            result.stopped = built.stopped;
            if (!built.success) {
                result.failureReason = built.failureReason;
                return built.exitCode;
            }
            // Fetch approval and acquisition use the build operation's existing hook.
            if (mayFetch) {
                auto selectedRoot = trusscVersion::resolve(projectPath, false);
                if (!selectedRoot.empty()) tcRoot = selectedRoot;
            }
            cout << "\n";
        } else {
            const string type = readCacheValue(buildDir / "CMakeCache.txt", "CMAKE_BUILD_TYPE");
            if (type == "Debug") {
                cerr << "Error: the existing build is a Debug build; it cannot be shipped.\n"
                     << "Run 'trusscli dist' without --no-build, or build with --relwithdebinfo / --release.\n";
                return 1;
            }
            if (release && !type.empty() && type != "Release") {
                cerr << "Error: --release asks for a Release build, but the existing build is " << type << ".\n";
                return 1;
            }
            if (noBuild) {
                cout << "Packaging the existing build (--no-build): "
                     << (type.empty() ? string("build type unknown") : type) << "\n";
            }
        }
        {
            string hotReload;
            ifstream(buildDir / ".tc_hot_reload_state") >> hotReload;
            if (hotReload == "ON") {
                cerr << "Error: this build uses hot reload; its code lives in a library in "
                     << buildDir.filename().string() << "/, which does not travel with the app.\n"
                     << "Turn hot reload off and build again to package it.\n";
                return 1;
            }
        }

        // The executable / bundle name: the project name the build used, from its
        // CMake cache. Generated projects name it sanitizeProjectName(<folder>)
        // ("sample project" -> sample_project); reading it back instead of
        // computing it again also holds for projects made before that, and on
        // Windows, where the folder name reaches here in the ANSI code page.
        string targetName = readCacheValue(buildDir / "CMakeCache.txt", "CMAKE_PROJECT_NAME");
        if (targetName.empty()) targetName = sanitizeProjectName(projectName);  // not configured yet

        // --- Plan ---
        dist::PlanInput input;
#ifdef _WIN32
        input.platform = dist::Platform::Windows;
#elif defined(__APPLE__)
        input.platform = dist::Platform::MacOS;
#else
        input.platform = dist::Platform::Linux;
        dist::LinuxPackageFormat linuxFormat = dist::LinuxPackageFormat::None;
        {
            // The package runs on the distribution it was built on; say which one.
            std::stringstream osRelease;
            osRelease << ifstream("/etc/os-release").rdbuf();
            const dist::LinuxDistro distro = dist::parseOsRelease(osRelease.str());
            input.platformTag = dist::distroTag(distro);
            linuxFormat = dist::packageFormatFor(distro);
        }
        if (installer) {
            if (linuxFormat == dist::LinuxPackageFormat::None) {
                cerr << "Error: --installer makes a pacman package (Arch and derivatives) or a .deb (Debian,\n"
                        "Ubuntu and derivatives); this distribution (" << input.platformTag
                     << ") is neither. Use --archive.\n";
                return 1;
            }
            const bool pacman = linuxFormat == dist::LinuxPackageFormat::Pacman;
            const vector<const char*> tools = pacman ? vector<const char*>{"makepkg", "objdump", "ldd"}
                                                     : vector<const char*>{"dpkg-deb", "dpkg-shlibdeps", "strip"};
            for (const char* tool : tools) {
                if (findOnPath(tool).empty()) {
                    cerr << "Error: --installer needs " << tool << " (install it with '"
                         << (pacman ? "sudo pacman -S --needed base-devel" : "sudo apt install dpkg-dev binutils")
                         << "').\n";
                    return 1;
                }
            }
        }
#endif
        input.projectDir = projectDir;
        input.binDir = projectDir / "bin";
        {
            // An error here would leave an empty path, and the package folder would
            // then be replaced relative to the current directory: stop instead.
            const fs::path requested = fs::absolute(outputDir.empty() ? (projectDir / "dist") : fs::path(outputDir), ec);
            if (!ec) input.outputDir = fs::weakly_canonical(requested, ec);
            if (ec || input.outputDir.empty() || !input.outputDir.is_absolute()) {
                cerr << "Error: cannot resolve the output folder "
                     << (outputDir.empty() ? (projectDir / "dist").string() : outputDir) << ": "
                     << (ec ? ec.message() : string("not an absolute path")) << "\n";
                return 1;
            }
        }
        input.projectName = targetName;
        input.config = config;
        input.licenses = dist::collectLicenses(tcRoot, projectAddonDirs(projectDir.string(), tcRoot),
                                               config.licenses, projectDir);
#ifdef _WIN32
        {
            const auto installs = VsDetector::detectInstalledVersions();
            const dist::BinaryInfo exe = dist::readBinaryInfo(input.binDir / (targetName + ".exe"));
            const string arch = exe.arch.empty() ? "x64" : exe.arch;
            for (const auto& vs : installs) {  // newest first
                input.vcRuntimeDir = dist::findVcRuntimeDir(vs.installPath, arch);
                if (!input.vcRuntimeDir.empty()) break;
            }
        }
#endif

        dist::Plan plan;
        string error;
        if (!dist::makePlan(input, plan, error)) {
            cerr << "Error: " << error << "\n";
            return 1;
        }
        for (const string& w : plan.warnings) emit(w, MessageKind::Warning);

        fs::path makensis;
        if (installer && input.platform == dist::Platform::Windows) {
            if (config.installer.showEula && plan.eulaSource.empty()) {
                cerr << "Error: dist.json \"installer.showEula\" needs an EULA: create EULA.txt in the "
                        "project folder with your license terms (or set \"eulaFile\").\n";
                return 1;
            }
            makensis = findMakensis();
        }

        if (dryRun) {
            if (installer && input.platform == dist::Platform::Windows) {
                cout << "Would also build " << plan.folderName << "-setup.exe"
                     << (makensis.empty() ? " (NSIS not found: install it with 'winget install NSIS.NSIS')" : "")
                     << "\n";
            }
#ifdef __linux__
            if (installer && linuxFormat == dist::LinuxPackageFormat::Pacman) {
                cout << "Would also build a pacman package " << dist::linuxPackageName(config.name) << "-"
                     << config.version << "-1-" << dist::pacmanArch(plan.arch) << ".pkg.tar.zst\n";
            } else if (installer) {
                cout << "Would also build " << dist::linuxPackageName(config.name) << "_" << config.version << "_"
                     << dist::debArch(plan.arch) << ".deb\n";
            }
#endif
            cout << "Would write to " << input.outputDir.string() << ":\n" << dist::formatPlan(plan) << "\n"
                 << dist::formatLicenseReport(input.licenses);
            return 0;
        }

        checkStop();
        fs::create_directories(input.outputDir, ec);
        if (ec) {
            cerr << "Error: cannot create the output folder " << input.outputDir.string() << ": " << ec.message() << "\n";
            return 1;
        }
        if (!dist::writePackage(input, plan, error)) {
            cerr << "Error: " << error << "\n";
            return 1;
        }

#ifdef __APPLE__
        // Adding data to the bundle invalidates the linker's ad-hoc signature.
        const string bundlePath = (plan.packageDir / plan.bundleName).string();
        if (runInherited({"codesign", "--force", "--deep", "--sign", "-", bundlePath}) != 0)
            emit("ad-hoc signing failed; macOS may refuse to start the app.", MessageKind::Warning);
#endif

        string archiveName, archiveError;
        if (archive) {
            const string cmake = findCMake();
            const bool tarball = input.platform == dist::Platform::Linux;
            archiveName = plan.folderName + (tarball ? ".tar.gz" : ".zip");
            checkStop();
            fs::remove(input.outputDir / archiveName, ec);
            vector<string> tarArgs;
#ifdef _WIN32
            // cmake -E tar stores zip entry names in the system code page without
            // the UTF-8 flag, so non-ASCII names come out garbled when the zip is
            // extracted on a machine with another code page, or on macOS / Linux.
            // Windows' bsdtar writes them as UTF-8 with the flag set.
            if (const fs::path systemTar = findSystemTar(); !systemTar.empty()) {
                tarArgs = {systemTar.string(), "--format=zip", "--options", "hdrcharset=UTF-8",
                           "-c", "-f", (input.outputDir / archiveName).string(),
                           "-C", input.outputDir.string(), plan.folderName};
            } else {
                bool ascii = isAscii(plan.folderName);
                for (const string& file : dist::listPackageFiles(plan.packageDir)) {
                    if (!ascii) break;
                    ascii = isAscii(file);
                }
                if (!ascii) {
                    emit("%SystemRoot%\\System32\\tar.exe not found; the zip's "
                         "non-ASCII file names may be garbled when it is extracted on another "
                         "system.", MessageKind::Warning);
                }
            }
#endif
            if (tarArgs.empty()) {
                tarArgs = {cmake, "-E", "chdir", input.outputDir.string(), cmake, "-E", "tar",
                           tarball ? "czf" : "cf", archiveName};
                if (!tarball) tarArgs.push_back("--format=zip");
                tarArgs.push_back(plan.folderName);
            }
            // On failure keep going: the folder is written, and the installer does
            // not need the archive. The error is reported at the end.
            try {
                if (runInherited(tarArgs) != 0) {
                    archiveError = "could not write " + archiveName;
                    fs::remove(input.outputDir / archiveName, ec);
                    archiveName.clear();
                }
            } catch (const DistStopped&) {
                fs::remove(input.outputDir / archiveName, ec);
                throw;
            }
        }

        fs::path setupPath;
        string installerError;
#ifdef __linux__
        if (installer) {
            setupPath = linuxFormat == dist::LinuxPackageFormat::Pacman
                            ? buildPacmanPackage(input, plan, fs::path(tcRoot), installerError)
                            : buildDebPackage(input, plan, fs::path(tcRoot), installerError);
        }
#else
        if (installer) {
            if (makensis.empty()) {
                installerError = "NSIS (makensis.exe) was not found, so no installer was built. Install it "
                                 "with 'winget install NSIS.NSIS' (free, zlib/libpng license) and run again.";
            } else {
                setupPath = buildWindowsInstaller(makensis, input, plan, installerError);
            }
        }
#endif

        checkStop();
        cout << dist::formatLicenseReport(input.licenses) << "\n"
             << "Packaged " << config.name << " " << config.version
             << (config.author.empty() ? string() : " by " + config.author) << ":\n"
             << "  " << plan.packageDir.string() << "\n";
        if (!plan.eulaSource.empty()) cout << "  EULA.txt from " << plan.eulaSource.filename().string() << "\n";
        if (!plan.runtimeDlls.empty()) {
            cout << "  VC++ runtime:";
            for (const string& dll : plan.runtimeDlls) cout << " " << dll;
            cout << "\n";
        }
        if (!plan.symbolsDir.empty()) cout << "  symbols (keep, do not ship): " << plan.symbolsDir.string() << "\n";
        if (!archiveName.empty()) cout << "  " << (input.outputDir / archiveName).string() << "\n";
        if (!setupPath.empty()) cout << "  " << setupPath.string() << "\n";
        if (!plan.excluded.empty()) cout << "  " << plan.excluded.size() << " file(s) left out by exclude\n";
        if (!haveConfig) {
            cout << "No dist.json: used name \"" << config.name << "\", version " << config.version
                 << ". Run 'trusscli dist --init' to set them.\n";
        }
#ifdef __linux__
        if (!setupPath.empty() && linuxFormat == dist::LinuxPackageFormat::Pacman) {
            cout << "Install with:  sudo pacman -U " << setupPath.filename().string() << "\n"
                 << "(installs the dependencies too; remove with: sudo pacman -R "
                 << dist::linuxPackageName(config.name) << ")\n";
        } else if (!setupPath.empty()) {
            // "./": apt installs a local file only when it is given as a path.
            cout << "Install with:  sudo apt install ./" << setupPath.filename().string() << "\n"
                 << "(installs the dependencies too; remove with: sudo apt remove "
                 << dist::linuxPackageName(config.name) << ")\n";
        } else if (!installer) {
            cout << "Note: the folder runs on " << input.platformTag
                 << " with the same libraries installed. For other people, prefer\n"
                 << "'trusscli dist --installer': the package manager then installs what it needs.\n";
        }
#endif
#ifdef __APPLE__
        cout << "Note: an app signed ad hoc is blocked by Gatekeeper when downloaded from the internet\n"
             << "(open it with right-click > Open). Copying it by USB or network share is fine.\n";
#endif
        if (!archiveError.empty()) cerr << "Error: " << archiveError << "\n";
        if (!installerError.empty()) cerr << "Error: " << installerError << "\n";
        if (!archiveError.empty() || !installerError.empty()) {
            cerr << "What is listed above was written.\n";
            return 1;
        }
        checkStop();
        return 0;
    }

};
} // namespace

Result dist(const DistInput& input, const LogCallback& callback) {
    Result result;
    // Bound (long dist transcripts): retain at most 2 MiB and 32768 entries, including the truncation notice.
    constexpr size_t maxLogBytes = 2 * 1024 * 1024, maxLogEntries = 32768;
    const string notice = "[Earlier dist output truncated]\n";
    deque<string> retained;
    size_t bytes = 0;
    bool truncated = false;
    LogCallback emit = [&](const string& text, MessageKind kind) {
        if (kind == MessageKind::Warning) result.warnings.push_back(text);
        if (kind == MessageKind::ErrorOutput) result.failureReason += text;
        const size_t budget = maxLogBytes - notice.size();
        truncated |= text.size() > budget;
        retained.push_back(text.size() > budget ? text.substr(text.size() - budget) : text);
        bytes += retained.back().size();
        while (bytes > budget || retained.size() >= maxLogEntries) {
            bytes -= retained.front().size();
            retained.pop_front();
            truncated = true;
        }
        if (callback) callback(text, kind);
    };
    OutputBuffer output([&](const string& text) { emit(text, MessageKind::Output); });
    OutputBuffer errors([&](const string& text) { emit(text, MessageKind::ErrorOutput); });
    ostream out(&output), err(&errors);
    ScopedDiagnostics diagnostics(emit);
    try {
        DistOperation operation{input, result, emit, out, err};
        result.exitCode = operation.execute();
        result.success = result.exitCode == 0 && !result.stopped;
    } catch (const DistStopped& stopped) {
        result.stopped = true;
        result.exitCode = stopped.exitCode;
        result.failureReason = "Dist stopped";
    } catch (const exception& error) {
        emit(string("Error: ") + error.what() + "\n", MessageKind::ErrorOutput);
    }
    if (truncated) result.logLines.push_back(notice);
    result.logLines.insert(result.logLines.end(), retained.begin(), retained.end());
    return result;
}
} // namespace projectOps
