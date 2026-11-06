#pragma once

// =============================================================================
// Dist - `trusscli dist`: package a built project to run on another machine
// (#1272). Everything here is plain logic over files, without running the
// build, so core/tests/trusscliDist can test it; tools/src/main.cpp (cmdDist)
// builds the project and drives it.
//
// Package layout (Windows / Linux):
//   <output>/<Name>-<version>-<platform>-<arch>/
//     <exe> and everything else in bin/ (minus build leftovers and exclude),
//     include entries, VC++ runtime DLLs (Windows), licenses/,
//     THIRD_PARTY_NOTICES.txt
//   <output>/<Name>-<version>-<platform>-<arch>-symbols/   .pdb (Windows)
// macOS: the .app with bin/data in Contents/Resources/data and include
// entries under Contents/Resources/; licenses/ and the notices beside it.
// =============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

namespace dist {

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// dist.json
// -----------------------------------------------------------------------------

struct IncludeEntry {
    std::string from;   // relative to the project folder, or absolute
    std::string to;     // inside the package ('/'-separated)
};

struct LicenseEntry {
    std::string name;   // shown in the report and the notices
    std::string file;   // relative to the project folder, or absolute
};

// dist.json "installer" (used with --installer). Shortcuts and start at
// login are not here: the installer asks the person installing.
struct InstallerOptions {
    bool showEula = false;      // EULA.txt as an acceptance page (needs an EULA)
    bool showNotices = false;   // THIRD_PARTY_NOTICES.txt as a read-only page
};

struct Config {
    std::string name;       // "" = project folder name
    std::string version;    // "" = 0.1.0
    std::string author;     // person / studio / company; "" = not set
    std::string eulaFile;   // the app's own license terms, shipped as EULA.txt;
                            // "" = kDefaultEula in the project folder if it exists
    std::vector<IncludeEntry> include;
    std::vector<std::string> exclude;
    std::vector<LicenseEntry> licenses;
    InstallerOptions installer;
};

constexpr const char* kDefaultVersion = "0.1.0";
constexpr const char* kDefaultEula = "EULA.txt";   // in the project folder

// "© <year> <author>", or "" without an author.
std::string copyrightLine(const std::string& author);

// Parses dist.json. Unknown keys and keys of a later phase add a warning and
// are otherwise ignored. Returns false with `error` for invalid JSON or a
// known key of the wrong type.
bool parseConfig(const std::string& text, Config& out, std::vector<std::string>& warnings,
                 std::string& error);

// The dist.json that `trusscli dist --init` writes.
std::string configTemplate(const std::string& name, const std::string& version,
                           const std::string& author);

// "v1.2.0" / "1.2.0" -> "1.2.0". One to four dot-separated numbers (the
// Windows version resource has four). False for anything else.
bool normalizeVersion(const std::string& input, std::string& out);

// The name used in file and folder names: spaces become '-', characters that
// are not allowed in file names on some OS are dropped. Never empty ("app").
std::string fileSafeName(const std::string& name);

// -----------------------------------------------------------------------------
// exclude patterns
// -----------------------------------------------------------------------------

// Glob on '/'-separated paths: '*' and '?' stay within one path segment,
// '**' spans any number of segments ("data/**/*.psd" also matches
// "data/a.psd"). Case-sensitive.
bool globMatch(const std::string& pattern, const std::string& path);

// A path is excluded when a pattern matches it or one of its parent folders
// ("data/raw" excludes "data/raw/a.png").
bool isExcluded(const std::vector<std::string>& patterns, const std::string& path);

// -----------------------------------------------------------------------------
// Executables
// -----------------------------------------------------------------------------

struct BinaryInfo {
    bool valid = false;               // a PE, ELF or Mach-O file
    std::string format;               // "pe", "elf", "macho"
    std::string arch;                 // "x64", "arm64", "x86", "universal", or "" if unknown
    std::vector<std::string> imports; // PE only: imported DLL names, lowercase
};

// Reads the header (and, for PE, the import table) of an executable or DLL.
// Never throws; a file it cannot read gives valid == false.
BinaryInfo readBinaryInfo(const fs::path& file);

// Windows: a debug CRT DLL (cannot be redistributed): vcruntime140d.dll,
// msvcp140d.dll, ucrtbased.dll, ...
bool isDebugCrt(const std::string& dllLower);

// Windows: DLLs missing on N / KN editions (Media Foundation).
bool isMediaFoundation(const std::string& dllLower);

// The VC++ runtime folder of a Visual Studio installation for `arch`
// ("x64", "arm64", "x86"): VC/Redist/MSVC/<newest version>/<arch>/Microsoft.VC*.CRT.
// Empty if there is none.
fs::path findVcRuntimeDir(const fs::path& vsInstallPath, const std::string& arch);

// -----------------------------------------------------------------------------
// Licenses
// -----------------------------------------------------------------------------

struct LicenseSource {
    std::string component;          // "TrussC", "tcxGltf", or a dist.json name
    std::string declared;           // addon.json "license" (the addon's own), may be ""
    std::vector<fs::path> files;    // notice files found
    std::string note;               // shown when files is empty
    bool inCoreList = false;        // an addon whose dependencies TrussC's
                                    // docs/LICENSE.md lists (Addon column)
};

// Top-level notice files of an addon: LICENSE*, LICENSES*, COPYING*, NOTICE*
// (case-insensitive), sorted.
std::vector<fs::path> findNoticeFiles(const fs::path& dir);

// What `dist` and `info licenses` report: TrussC core, then the addons in
// the order given, then dist.json "licenses" (relative to projectDir).
// An addon named in the Addon column of TrussC's docs/LICENSE.md tables has
// its dependencies' notices there (inCoreList), so no file of its own is a gap
// only for the other addons.
std::vector<LicenseSource> collectLicenses(const fs::path& tcRoot,
                                           const std::vector<std::pair<std::string, fs::path>>& addons,
                                           const std::vector<LicenseEntry>& extra,
                                           const fs::path& projectDir);

// The report table printed after packaging and by `trusscli info licenses`.
std::string formatLicenseReport(const std::vector<LicenseSource>& sources);

// -----------------------------------------------------------------------------
// Packaging
// -----------------------------------------------------------------------------

enum class Platform { Windows, MacOS, Linux };
const char* platformName(Platform platform);  // "windows", "macos", "linux"

struct PlanInput {
    Platform platform = Platform::Windows;
    fs::path projectDir;
    fs::path binDir;            // projectDir/bin
    fs::path outputDir;         // where the package folder goes
    std::string projectName;    // executable / bundle name
    Config config;              // name and version already resolved
    std::vector<LicenseSource> licenses;
    fs::path vcRuntimeDir;      // Windows: from findVcRuntimeDir, may be empty
};

struct CopyItem {
    fs::path source;
    std::string target;         // inside the package folder, '/'-separated
};

struct Plan {
    std::string folderName;     // <Name>-<version>-<platform>-<arch>
    fs::path packageDir;
    fs::path symbolsDir;        // empty when there are no symbols
    std::string arch;
    fs::path bundle;            // macOS: the .app in bin/ (copied whole, symlinks kept)
    std::string bundleName;     // macOS: "<name>.app"
    std::vector<CopyItem> files;
    std::vector<CopyItem> symbols;          // target relative to symbolsDir
    std::vector<std::string> excluded;      // what exclude left out (logical paths)
    std::vector<std::string> runtimeDlls;   // Windows: VC++ runtime DLLs copied
    std::vector<std::string> warnings;
    std::string launcherName;               // Linux: launcher script in the package
    fs::path eulaSource;                    // shipped as EULA.txt; empty = no EULA
    std::string executableName;             // Windows / Linux: the executable in the package
};

// Works out what goes where, reading bin/ and the dist.json entries but
// writing nothing. False with `error` when the build is missing or cannot be
// shipped (a debug CRT, a missing include source or eulaFile, ...).
// The EULA (eulaFile, or EULA.txt in the project folder when present) goes to
// the package root as EULA.txt (beside the bundle on macOS).
bool makePlan(const PlanInput& input, Plan& plan, std::string& error);

// Writes the package: replaces plan.packageDir and plan.symbolsDir, copies,
// writes licenses/ and THIRD_PARTY_NOTICES.txt, sets the macOS bundle
// version and writes the Linux launcher. Does not sign or archive.
bool writePackage(const PlanInput& input, const Plan& plan, std::string& error);

// One line per file for --dry-run.
std::string formatPlan(const Plan& plan);

// -----------------------------------------------------------------------------
// Windows installer (NSIS)
// -----------------------------------------------------------------------------

// Every file in a written package, relative and '/'-separated, sorted.
std::vector<std::string> listPackageFiles(const fs::path& packageDir);

struct NsisInput {
    std::string name;            // shown in the installer, Start menu, Programs list
    std::string version;
    std::string author;          // publisher; may be ""
    std::string arch;            // "x64", "arm64", "x86"
    std::string executable;      // file name inside the package
    fs::path packageDir;         // what gets installed
    std::vector<std::string> files;  // listPackageFiles(packageDir): what the uninstaller removes
    fs::path setupExe;           // the installer to write
    fs::path eulaPage;           // UTF-8 text for the EULA page, or empty
    fs::path noticesPage;        // UTF-8 text for the notices page, or empty
    InstallerOptions options;
};

// An NSIS script (Unicode; give makensis /INPUTCHARSET UTF8). Installs for
// all users under Program Files, registers in Apps & features, offers the
// shortcuts (checked) and start at login (unchecked) as checkboxes; a silent
// install (setup.exe /S) takes those defaults. The uninstaller deletes
// exactly the installed files (and any of the shortcuts), then the folders
// left empty. zlib compression keeps the whole installer under the zlib/libpng
// license (LZMA's decompressor would be CPL code).
std::string nsisScript(const NsisInput& input);

// A copy of a UTF-8 text file with a byte order mark, which NSIS needs to
// read non-ASCII text on its license pages. False if it cannot be written.
bool writeWithBom(const fs::path& source, const fs::path& target);

} // namespace dist
