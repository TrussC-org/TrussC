// =============================================================================
// core/tests/trusscliDist — `trusscli dist` packaging logic (#1272).
//
// Headless, console, exit code = pass/fail. local.cmake compiles
// tools/src/Dist.cpp into this test.
//
// Guards:
//   - dist.json: every key is read, "installer" options included; unknown
//     keys (also inside "installer") are warned about, not fatal; a wrong
//     type or invalid JSON is an error; the --init template parses back.
//     The copyright line is "© <year> <author>".
//   - NSIS script: zlib compression (no LZMA), a four-part version, names
//     escaped for NSIS ("$" and '"'), Program Files by arch, the EULA and
//     notices pages only when asked for, shortcuts (checked) and start at
//     login (unchecked) as checkboxes, and an uninstaller
//     that deletes the listed files and any of the shortcuts, then the
//     folders deepest first, never recursively.
//   - Versions: "v1.2.0" -> "1.2.0", one to four numbers of at most 65535,
//     anything else rejected. File-safe names: spaces -> '-', characters not
//     allowed in file names dropped, never empty.
//   - exclude globs: '*' and '?' stay in one segment, '**' spans any number
//     (including none); a pattern for a folder excludes what is inside it.
//   - readBinaryInfo() on this test's own executable: the format and arch of
//     the platform; on Windows its imports include kernel32.dll. A text file
//     is not a binary.
//   - Licenses: TrussC's docs/LICENSE.md, an addon's notice files and its
//     addon.json license, an addon with none (reported) unless TrussC's
//     LICENSE.md tables name it in their Addon column, a dist.json entry
//     whose file is missing (reported).
//   - makePlan() + writePackage() on a fake project whose executable is a
//     copy of this test binary: bin/ minus build leftovers and exclude,
//     include entries, .pdb to the -symbols folder and the VC++ runtime DLLs
//     the executable imports (Windows; a debug CRT import is refused), the
//     bin/data move into the bundle and the bundle version (macOS), the
//     launcher (Linux), licenses/ and THIRD_PARTY_NOTICES.txt with the
//     author, and the author as the bundle copyright (macOS). The EULA:
//     EULA.txt in the project folder ships without any setting, eulaFile
//     ships under the name EULA.txt, neither ships none. A missing eulaFile,
//     a missing include source and an "include" target outside the package
//     fail.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include "Dist.h"

#include <cstdio>
#include <fstream>
#include <sstream>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

namespace {

namespace fs = std::filesystem;

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}

void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

std::string read(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    std::stringstream text;
    text << file.rdbuf();
    return text.str();
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void testConfig() {
    dist::Config config;
    std::vector<std::string> warnings;
    std::string error;
    const bool ok = dist::parseConfig(R"({
        "name": "My App", "version": "1.2.0", "author": "My Studio", "eulaFile": "EULA-ko.txt",
        "include": [{"from": "../tools", "to": "data/tools"}],
        "exclude": ["data/**/*.psd"],
        "licenses": [{"name": "Node.js", "file": "LICENSE-node"}],
        "installer": {"showNotices": true},
        "exlcude": []
    })", config, warnings, error);
    check("dist.json: all phase-1 keys read",
          ok && config.name == "My App" && config.version == "1.2.0" && config.author == "My Studio" &&
              config.eulaFile == "EULA-ko.txt" && config.include.size() == 1 &&
              config.include[0].to == "data/tools" && config.exclude.size() == 1 &&
              config.licenses.size() == 1 && config.licenses[0].file == "LICENSE-node");
    bool typoWarned = false;
    for (const auto& w : warnings) typoWarned = typoWarned || contains(w, "\"exlcude\"");
    check("dist.json: unknown keys warn", typoWarned && warnings.size() == 1);

    warnings.clear();
    check("dist.json: installer options; shortcut choices are not among them",
          dist::parseConfig(R"({"installer": {"showEula": true, "showNotices": true, "shortcut": false}})",
                            config, warnings, error) &&
              config.installer.showEula && config.installer.showNotices && warnings.size() == 1 &&
              contains(warnings[0], "installer.shortcut"));
    check("dist.json: installer options must be booleans",
          !dist::parseConfig(R"({"installer": {"showEula": "yes"}})", config, warnings, error) &&
              !dist::parseConfig(R"({"installer": []})", config, warnings, error));

    check("dist.json: wrong type is an error",
          !dist::parseConfig(R"({"exclude": "data/raw"})", config, warnings, error) &&
              contains(error, "exclude"));
    check("dist.json: invalid JSON is an error",
          !dist::parseConfig("{ name: 1 ", config, warnings, error));
    check("dist.json: eulaFile must be a string",
          !dist::parseConfig(R"({"eulaFile": true})", config, warnings, error));

    warnings.clear();
    check("--init template parses back",
          dist::parseConfig(dist::configTemplate("My \"App\"", "0.1.0", "Studio"), config, warnings, error) &&
              config.name == "My \"App\"" && config.version == "0.1.0" && config.author == "Studio" &&
              config.eulaFile == "EULA.txt" && warnings.empty());
    check("copyright line", dist::copyrightLine("").empty() &&
                                contains(dist::copyrightLine("My Studio"), " My Studio") &&
                                dist::copyrightLine("My Studio").rfind("\xC2\xA9 20", 0) == 0);
}

void testNsis() {
    dist::NsisInput in;
    in.name = "My \"App\" $1";
    in.version = "1.2";
    in.author = "My Studio";
    in.arch = "x64";
    in.executable = "myApp.exe";
    in.packageDir = "C:/out/My-App-1.2-windows-x64";
    in.files = {"EULA.txt", "data/a b.txt", "data/sub/c.txt", "myApp.exe"};
    in.setupExe = "C:/out/My-App-1.2-windows-x64-setup.exe";
    in.eulaPage = "C:/out/work/EULA.txt";
    const std::string s = dist::nsisScript(in);
    check("nsis: zlib compression, four-part version, escaped name",
          contains(s, "SetCompressor /SOLID zlib") && contains(s, "VIProductVersion \"1.2.0.0\"") &&
              contains(s, "Name \"My $\\\"App$\\\" $$1\"") && !contains(s, "lzma"));
    check("nsis: Program Files, files, uninstaller registered",
          contains(s, "InstallDir \"$PROGRAMFILES64\\My App $$1\"") &&
              contains(s, "File /r \"C:\\out\\My-App-1.2-windows-x64\\*.*\"") &&
              contains(s, "\"Publisher\" \"My Studio\"") && contains(s, "WriteUninstaller"));
    check("nsis: author's copyright at the bottom of the pages",
          contains(s, "BrandingText \"\xC2\xA9 20") && contains(s, " My Studio\"\n"));
    {
        auto noAuthor = in;
        noAuthor.author.clear();
        check("nsis: no author leaves the bottom line blank",
              contains(dist::nsisScript(noAuthor), "BrandingText \" \"\n"));
    }
    check("nsis: EULA page only when given",
          contains(s, "MUI_PAGE_LICENSE \"C:\\out\\work\\EULA.txt\"") &&
              !contains(s, "Third-party notices"));
    check("nsis: shortcuts checked, start at login unchecked, both as checkboxes",
          contains(s, "MUI_PAGE_COMPONENTS") &&
              contains(s, "Section \"Start menu and desktop shortcuts\"") &&
              contains(s, "Section /o \"Start My $\\\"App$\\\" $$1 when Windows starts\"") &&
              contains(s, "$SMPROGRAMS\\My App $$1") && contains(s, "$SMSTARTUP\\My App $$1.lnk") &&
              contains(s, "MUI_FINISHPAGE_RUN \"$INSTDIR\\myApp.exe\"") && !contains(s, "$$INSTDIR"));
    const size_t sub = s.find("RMDir \"$INSTDIR\\data\\sub\"");
    const size_t data = s.find("RMDir \"$INSTDIR\\data\"");
    check("nsis: uninstall deletes the listed files, folders deepest first, nothing recursive",
          contains(s, "Delete \"$INSTDIR\\data\\a b.txt\"") && contains(s, "Delete \"$INSTDIR\\myApp.exe\"") &&
              sub != std::string::npos && data != std::string::npos && sub < data &&
              !contains(s, "RMDir /r"));

    in.arch = "x86";
    in.eulaPage.clear();
    in.noticesPage = "C:/out/work/THIRD_PARTY_NOTICES.txt";
    in.options = {};
    const std::string t = dist::nsisScript(in);
    check("nsis: x86 Program Files, notices page with a Next button",
          contains(t, "InstallDir \"$PROGRAMFILES\\") && contains(t, "MUI_LICENSEPAGE_BUTTON") &&
              contains(t, "Third-party notices"));
    check("nsis: uninstall removes every shortcut whatever was chosen",
          contains(t, "Delete \"$SMSTARTUP\\") && contains(t, "Delete \"$DESKTOP\\"));
}

void testNames() {
    std::string v;
    check("version: v prefix dropped", dist::normalizeVersion("v1.2.0", v) && v == "1.2.0");
    check("version: one to four numbers",
          dist::normalizeVersion("1", v) && dist::normalizeVersion("1.2.3.4", v) && v == "1.2.3.4");
    check("version: invalid forms rejected",
          !dist::normalizeVersion("1.2.3.4.5", v) && !dist::normalizeVersion("beta", v) &&
              !dist::normalizeVersion("1..2", v) && !dist::normalizeVersion("", v) &&
              !dist::normalizeVersion("1.2-rc1", v) && !dist::normalizeVersion("70000.0", v));
    check("file-safe name", dist::fileSafeName("My App") == "My-App" &&
                                dist::fileSafeName("a/b:c?") == "abc" &&
                                dist::fileSafeName(" x ") == "x" && dist::fileSafeName("") == "app");
}

void testDistro() {
    const dist::LinuxDistro ubuntu = dist::parseOsRelease(
        "NAME=\"Ubuntu\"\nVERSION_ID=\"24.04\"\nID=ubuntu\nID_LIKE=debian\n# ID=commented\n");
    check("os-release: quoted VERSION_ID, plain ID, comment skipped",
          ubuntu.id == "ubuntu" && ubuntu.versionId == "24.04" &&
              ubuntu.idLike == std::vector<std::string>{"debian"});
    check("distro tag: id + version", dist::distroTag(ubuntu) == "ubuntu24.04");

    const dist::LinuxDistro arch = dist::parseOsRelease("NAME=\"Arch Linux\"\r\nID=arch\r\nBUILD_ID=rolling\r\n");
    check("distro tag: rolling release, CRLF", dist::distroTag(arch) == "arch");

    const dist::LinuxDistro mint = dist::parseOsRelease("ID=linuxmint\nVERSION_ID='22.1'\nID_LIKE=\"ubuntu debian\"\n");
    check("os-release: single quotes, ID_LIKE list",
          dist::distroTag(mint) == "linuxmint22.1" &&
              mint.idLike == std::vector<std::string>{"ubuntu", "debian"});

    check("distro tag: odd characters dropped",
          dist::distroTag(dist::parseOsRelease("ID=Open SUSE/x\nVERSION_ID=15 SP6\n")) == "opensusex15sp6");
    check("distro tag: unknown -> linux", dist::distroTag(dist::parseOsRelease("")) == "linux");
}

void testGlobs() {
    check("glob: ** spans folders", dist::globMatch("data/**/*.psd", "data/x/y/a.psd"));
    check("glob: ** spans no folder", dist::globMatch("data/**/*.psd", "data/a.psd") &&
                                          dist::globMatch("**/*.psd", "a.psd"));
    check("glob: * stays in one segment",
          !dist::globMatch("*.psd", "data/a.psd") && !dist::globMatch("data/*", "data/a/b"));
    check("glob: ? matches one character", dist::globMatch("data/r?w", "data/raw") &&
                                               !dist::globMatch("data/r?w", "data/rw"));
    check("exclude: a folder pattern covers its contents",
          dist::isExcluded({"data/raw"}, "data/raw/a.png") &&
              !dist::isExcluded({"data/raw"}, "data/rawx/a.png") &&
              dist::isExcluded({"data\\raw\\"}, "data/raw/a.png"));
}

void testBinary(const fs::path& self, const fs::path& scratch) {
    const auto info = dist::readBinaryInfo(self);
#if defined(_WIN32)
    bool kernel32 = false;
    for (const auto& dll : info.imports) kernel32 = kernel32 || dll == "kernel32.dll";
    check("PE header and imports of this executable",
          info.valid && info.format == "pe" && !info.arch.empty() && kernel32);
#elif defined(__APPLE__)
    check("Mach-O header of this executable", info.valid && info.format == "macho" && !info.arch.empty());
#else
    check("ELF header of this executable", info.valid && info.format == "elf" && !info.arch.empty());
#endif
    write(scratch / "text.txt", "not a binary");
    check("a text file is not a binary", !dist::readBinaryInfo(scratch / "text.txt").valid);
    check("debug CRT names", dist::isDebugCrt("vcruntime140d.dll") && dist::isDebugCrt("ucrtbased.dll") &&
                                 !dist::isDebugCrt("vcruntime140.dll"));
}

void testPackage(const fs::path& self, const fs::path& scratch) {
#if defined(_WIN32)
    const auto platform = dist::Platform::Windows;
#elif defined(__APPLE__)
    const auto platform = dist::Platform::MacOS;
#else
    const auto platform = dist::Platform::Linux;
#endif
    const fs::path root = scratch / "pkg";
    const fs::path tc = root / "TrussC";
    const fs::path project = root / "myApp";
    write(tc / "docs" / "LICENSE.md",
          "TrussC MIT\n\n| Library | License | Addon |\n|---|---|---|\n| **Foo** | MIT | tcxListed |\r\n");
    write(tc / "addons" / "tcxListed" / "addon.json", R"({"license": "MIT"})");
    write(tc / "addons" / "tcxGood" / "addon.json", R"({"license": "MIT"})");
    write(tc / "addons" / "tcxGood" / "LICENSES.md", "good notices\n");
    write(tc / "addons" / "tcxBare" / "addon.json", R"({"license": "MIT"})");

    const fs::path bin = project / "bin";
    write(bin / "data" / "a.txt", "keep");
    write(bin / "data" / "raw" / "b.psd", "exclude me");
    write(project / "extra" / "tool" / "run.js", "include me");
    write(project / "LICENSE-node", "node license\n");
    fs::path executable;
    if (platform == dist::Platform::Windows) {
        executable = bin / "myApp.exe";
        write(bin / "myApp.pdb", "symbols");
        write(bin / "myApp.ilk", "leftover");
    } else if (platform == dist::Platform::MacOS) {
        executable = bin / "myApp.app" / "Contents" / "MacOS" / "myApp";
        write(bin / "myApp.app" / "Contents" / "Info.plist",
              "<plist><dict><key>CFBundleShortVersionString</key><string>1.0</string>"
              "<key>CFBundleVersion</key><string>1.0</string></dict></plist>");
    } else {
        executable = bin / "myApp";
    }
    fs::create_directories(executable.parent_path());
    fs::copy_file(self, executable, fs::copy_options::overwrite_existing);

    dist::PlanInput input;
    input.platform = platform;
    input.projectDir = project;
    input.binDir = bin;
    input.outputDir = project / "dist";
    input.projectName = "myApp";
    input.config.name = "My App";
    input.config.version = "1.2.0";
    input.config.author = "My Studio";
    write(project / "EULA.txt", "my terms\n");  // picked up without eulaFile
    input.config.include = {{"extra/tool", "data/tool"}};
    input.config.exclude = {"data/raw"};
    input.licenses = dist::collectLicenses(
        tc, {{"tcxGood", tc / "addons" / "tcxGood"}, {"tcxBare", tc / "addons" / "tcxBare"},
             {"tcxListed", tc / "addons" / "tcxListed"}},
        {{"Node.js", "LICENSE-node"}, {"Missing", "nope.txt"}}, project);

    check("licenses: core, addon notices, gaps reported",
          input.licenses.size() == 6 && input.licenses[0].files.size() == 1 &&
              input.licenses[1].declared == "MIT" && input.licenses[1].files.size() == 1 &&
              input.licenses[2].files.empty() && contains(input.licenses[2].note, "no notice file") &&
              !input.licenses[2].inCoreList &&
              input.licenses[4].files.size() == 1 && contains(input.licenses[5].note, "file not found"));
    check("licenses: an addon in TrussC's LICENSE.md table is not a gap",
          input.licenses[3].inCoreList && input.licenses[3].files.empty() &&
              contains(input.licenses[3].note, "licenses/TrussC/LICENSE.md"));
    check("license report names the gaps",
          contains(dist::formatLicenseReport(input.licenses), "tcxBare") &&
              contains(dist::formatLicenseReport(input.licenses), "licenses/tcxGood/LICENSES.md"));

    // Windows: a runtime folder with the DLLs this executable imports.
    bool debugCrt = false;
    std::vector<std::string> runtimeImports;
    if (platform == dist::Platform::Windows) {
        input.vcRuntimeDir = root / "redist" / "Microsoft.VC143.CRT";
        for (const auto& dll : dist::readBinaryInfo(self).imports) {
            if (dist::isDebugCrt(dll)) debugCrt = true;
            if (dll.rfind("vcruntime140", 0) == 0 || dll.rfind("msvcp140", 0) == 0) {
                write(input.vcRuntimeDir / dll, "runtime");
                runtimeImports.push_back(dll);
            }
        }
        write(input.vcRuntimeDir / "concrt140.dll", "not imported");
    }

    dist::Plan plan;
    std::string error;
    const bool planned = dist::makePlan(input, plan, error);
    if (debugCrt) {
        // A Debug build of this test: the refusal is what there is to check.
        check("a debug CRT import is refused", !planned && contains(error, "Debug build"));
        return;
    }
    check("makePlan", planned);
    if (!planned) { std::printf("  error: %s\n", error.c_str()); return; }
    check("folder name", plan.folderName.rfind("My-App-1.2.0-" + std::string(dist::platformName(platform)) + "-", 0) == 0);
    bool excludedRaw = false;
    for (const auto& e : plan.excluded) excludedRaw = excludedRaw || e == "data/raw/b.psd";
    check("exclude leaves the file out", excludedRaw);
    check("writePackage", dist::writePackage(input, plan, error));
    if (!error.empty()) std::printf("  error: %s\n", error.c_str());

    const fs::path pkg = plan.packageDir;
    const fs::path resources = platform == dist::Platform::MacOS ? pkg / "myApp.app" / "Contents" / "Resources" : pkg;
    check("data copied", read(resources / "data" / "a.txt") == "keep");
    check("excluded file absent", !fs::exists(resources / "data" / "raw" / "b.psd"));
    check("include copied", read(resources / "data" / "tool" / "run.js") == "include me");
    check("licenses copied", read(pkg / "licenses" / "tcxGood" / "LICENSES.md") == "good notices\n" &&
                                 fs::exists(pkg / "licenses" / "TrussC" / "LICENSE.md") &&
                                 fs::exists(pkg / "licenses" / "Node.js" / "LICENSE-node"));
    const std::string notices = read(pkg / "THIRD_PARTY_NOTICES.txt");
    check("notices merged", contains(notices, "TrussC MIT") && contains(notices, "good notices") &&
                                contains(notices, "no notice file") && contains(notices, "node license"));
    check("notices name the author", contains(notices, "\xC2\xA9 ") && contains(notices, "My Studio"));
    check("EULA.txt in the project folder ships at the package root", read(pkg / "EULA.txt") == "my terms\n");

    if (platform == dist::Platform::Windows) {
        check("exe copied", fs::exists(pkg / "myApp.exe"));
        check("pdb in the symbols folder, not the package",
              fs::exists(plan.symbolsDir / "myApp.pdb") && !fs::exists(pkg / "myApp.pdb") &&
                  plan.symbolsDir.filename() == plan.folderName + "-symbols");
        check("build leftovers dropped", !fs::exists(pkg / "myApp.ilk"));
        bool allRuntime = !runtimeImports.empty();
        for (const auto& dll : runtimeImports) allRuntime = allRuntime && fs::exists(pkg / dll);
        check("imported VC++ runtime DLLs copied, others not",
              allRuntime && !fs::exists(pkg / "concrt140.dll") &&
                  fs::exists(pkg / "licenses" / "Microsoft-VC-Runtime" / "README.txt"));
    } else if (platform == dist::Platform::MacOS) {
        const std::string plist = read(pkg / "myApp.app" / "Contents" / "Info.plist");
        check("bundle copied with its version set",
              fs::exists(pkg / "myApp.app" / "Contents" / "MacOS" / "myApp") &&
                  contains(plist, "<string>1.2.0</string>") && !contains(plist, "<string>1.0</string>"));
        check("bundle copyright from author",
              contains(plist, "<key>NSHumanReadableCopyright</key>") && contains(plist, "My Studio"));
    } else {
        check("launcher written", contains(read(pkg / "myApp.sh"), "exec \"$here/myApp\""));
    }

    // Writing again replaces the folder (a file removed from bin/ is gone).
    fs::remove(bin / "data" / "a.txt");
    check("second run replaces the package",
          dist::makePlan(input, plan, error) && dist::writePackage(input, plan, error) &&
              !fs::exists(resources / "data" / "a.txt"));

    // eulaFile: another name ships as EULA.txt; no file at all ships none.
    auto other = input;
    write(project / "EULA-ko.txt", "korean terms\n");
    other.config.eulaFile = "EULA-ko.txt";
    check("eulaFile ships as EULA.txt",
          dist::makePlan(other, plan, error) && dist::writePackage(other, plan, error) &&
              read(plan.packageDir / "EULA.txt") == "korean terms\n");
    fs::remove(project / "EULA.txt");
    check("no EULA without eulaFile or EULA.txt",
          dist::makePlan(input, plan, error) && plan.eulaSource.empty());

    auto failing = input;
    failing.config.eulaFile = "EULA-missing.txt";
    check("a missing eulaFile fails", !dist::makePlan(failing, plan, error) && contains(error, "eulaFile"));
    failing.config.eulaFile.clear();
    failing.config.include = {{"no/such/folder", "data/x"}};
    check("missing include source fails", !dist::makePlan(failing, plan, error) && contains(error, "not found"));
    failing.config.include = {{"extra/tool", "../outside"}};
    check("include target outside the package fails",
          !dist::makePlan(failing, plan, error) && contains(error, "inside the package"));
}

} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    (void)argc;
    std::error_code ec;
    const fs::path self = fs::absolute(argv[0], ec);
    const fs::path scratch = fs::temp_directory_path() / ("tc_trusscliDist_" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(scratch, ec);

    testConfig();
    testNames();
    testDistro();
    testGlobs();
    testNsis();
#if defined(_WIN32) || (defined(__APPLE__) && !TARGET_OS_IPHONE) || \
    (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
    fs::path exe = self;
#ifdef _WIN32
    if (exe.extension() != ".exe") exe += ".exe";
#endif
    testBinary(exe, scratch);
    testPackage(exe, scratch);
#else
    std::printf("SKIP: packaging checks are desktop only\n");
#endif
    fs::remove_all(scratch, ec);
    std::printf("%s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
