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
//     escaped for NSIS ("$" and '"'), Program Files and the registry view by
//     arch, the installed size, the EULA and
//     notices pages only when asked for, shortcuts (checked) and start at
//     login (unchecked) as checkboxes, and an uninstaller
//     that deletes the listed files and any of the shortcuts, then the
//     folders deepest first, never recursively.
//   - Versions: "v1.2.0" -> "1.2.0", exactly three numbers of at most 65535,
//     anything else rejected ("1.2." too). File-safe names: spaces -> '-', characters not
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
//     ships under the name EULA.txt, neither ships none; one that is not
//     UTF-8 ships with a warning. UTF-8 detection. A missing eulaFile,
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
              config.eulaFile.empty() && warnings.empty());
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
    check("nsis: x64 registers in the 64-bit registry view, install and uninstall",
          contains(s, "Function .onInit\n  SetRegView 64\n") &&
              contains(s, "Function un.onInit\n  SetRegView 64\n") &&
              contains(s, "ReadRegStr $0 HKLM \"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\") &&
              !contains(s, "InstallDirRegKey"));
    check("nsis: installed size for Apps & features",
          contains(s, "!include \"FileFunc.nsh\"") && contains(s, "${GetSize} \"$INSTDIR\" \"/S=0K\" $0 $1 $2") &&
              contains(s, "\"EstimatedSize\" $0"));

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
    check("nsis: x86 keeps the 32-bit registry view",
          !contains(t, "SetRegView") && contains(t, "InstallDirRegKey HKLM"));
}

void testUtf8Text() {
    check("utf-8: ASCII, Korean, Japanese, emoji, with or without BOM",
          dist::isUtf8Text("") && dist::isUtf8Text("EULA\r\n") &&
              dist::isUtf8Text("\xEC\x82\xAC\xEC\x9A\xA9\xEA\xB6\x8C") &&      // 사용권
              dist::isUtf8Text("\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E") &&      // 日本語
              dist::isUtf8Text("\xF0\x9F\x98\x80") && dist::isUtf8Text("\xEF\xBB\xBFok"));
    check("utf-8: CP949 / CP932 text is not UTF-8",
          !dist::isUtf8Text("\xBB\xE7\xBF\xEB\xB1\xC7") &&                    // 사용권 in CP949
              !dist::isUtf8Text("\x93\xFA\x96\x7B\x8C\xEA"));                  // 日本語 in CP932
    check("utf-8: overlong, surrogate, cut off and out-of-range forms rejected",
          !dist::isUtf8Text("\xC0\xAF") && !dist::isUtf8Text("\xE0\x80\xAF") &&
              !dist::isUtf8Text("\xED\xA0\x80") && !dist::isUtf8Text("\xEC\x82") &&
              !dist::isUtf8Text("\xF4\x90\x80\x80"));
}

void testNames() {
    std::string v;
    check("version: v prefix dropped", dist::normalizeVersion("v1.2.0", v) && v == "1.2.0");
    check("version: three numbers", dist::normalizeVersion("10.0.65535", v) && v == "10.0.65535");
    check("version: not three numbers rejected",
          !dist::normalizeVersion("1", v) && !dist::normalizeVersion("1.2", v) &&
              !dist::normalizeVersion("1.2.3.4", v));
    check("version: an empty part rejected (\"1.2.\" became \"1.2..0\" for NSIS)",
          !dist::normalizeVersion("1.2.", v) && !dist::normalizeVersion(".1.2", v) &&
              !dist::normalizeVersion("1..2", v) && !dist::normalizeVersion("1.2.3.", v));
    check("version: invalid forms rejected",
          !dist::normalizeVersion("beta", v) && !dist::normalizeVersion("", v) && !dist::normalizeVersion("v", v) &&
              !dist::normalizeVersion("1.2.0-rc1", v) && !dist::normalizeVersion("70000.0.0", v) &&
              !dist::normalizeVersion("1.2.99999999999", v));
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

    using F = dist::LinuxPackageFormat;
    check("package format: arch, ID_LIKE arch, ubuntu, ID_LIKE ubuntu, unknown",
          dist::packageFormatFor(arch) == F::Pacman &&
              dist::packageFormatFor(dist::parseOsRelease("ID=endeavouros\nID_LIKE=arch\n")) == F::Pacman &&
              dist::packageFormatFor(ubuntu) == F::Deb && dist::packageFormatFor(mint) == F::Deb &&
              dist::packageFormatFor(dist::parseOsRelease("ID=fedora\n")) == F::None);
}

void testLinuxPackage() {
    check("package name: lowercase, spaces -> '-', others dropped",
          dist::linuxPackageName("My App: Deluxe!") == "my-app-deluxe" &&
              dist::linuxPackageName("--Tool_2.0--") == "tool_2.0" && dist::linuxPackageName("日本") == "app");
    check("pacman arch", dist::pacmanArch("x64") == "x86_64" && dist::pacmanArch("arm64") == "aarch64");

    const std::string objdump =
        "Dynamic Section:\n  NEEDED               libGL.so.1\n  NEEDED               libc.so.6\n"
        "  SONAME               libx.so\n";
    check("NEEDED entries", dist::parseNeeded(objdump) == std::vector<std::string>{"libGL.so.1", "libc.so.6"});

    const auto ldd = dist::parseLdd(
        "\tlinux-vdso.so.1 (0x00007ffd)\n\tlibz.so.1 => /usr/lib/libz.so.1 (0x00007f00)\n"
        "\tlibmine.so => not found\n\t/lib64/ld-linux-x86-64.so.2 => /usr/lib64/ld-linux-x86-64.so.2 (0x0)\n");
    check("ldd: resolved libraries only",
          ldd.size() == 2 && ldd[0].first == "libz.so.1" && ldd[0].second == "/usr/lib/libz.so.1");

    dist::DesktopEntryInput desktop;
    desktop.name = "My\nApp";
    desktop.exec = "/opt/my-app/My App.sh";
    desktop.icon = "my-app";
    desktop.wmClass = "myApp";
    const std::string entry = dist::desktopEntry(desktop);
    check("desktop entry: one-line name, quoted Exec, WM class",
          contains(entry, "Name=My App\n") && contains(entry, "Exec=\"/opt/my-app/My App.sh\"\n") &&
              contains(entry, "Icon=my-app\n") && contains(entry, "StartupWMClass=myApp\n"));

    dist::PacmanInput pkg;
    pkg.pkgname = "my-app";
    pkg.version = "1.2.0";
    pkg.description = "It's mine";
    pkg.arch = "x86_64";
    pkg.depends = {"ffmpeg", "gtk3"};
    pkg.packageDir = "/tmp/dist/My App's";
    pkg.launcher = "myApp.sh";
    pkg.desktopFile = "/tmp/work/my-app.desktop";
    pkg.icon = "/tmp/icon.png";
    const std::string script = dist::pkgbuild(pkg);
    check("PKGBUILD: name, version, depends, no debug package",
          contains(script, "pkgname=my-app\n") && contains(script, "pkgver=1.2.0\n") &&
              contains(script, "depends=('ffmpeg' 'gtk3')\n") && contains(script, "options=('!debug')"));
    check("PKGBUILD: single quotes escaped",
          contains(script, "pkgdesc='It'\\''s mine'") && contains(script, "cp -a '/tmp/dist/My App'\\''s/.'"));
    check("PKGBUILD: /opt, /usr/bin link, menu entry, icon, licenses",
          contains(script, "\"$pkgdir/opt/my-app/\"") &&
              contains(script, "ln -s '/opt/my-app/myApp.sh' \"$pkgdir/usr/bin/my-app\"") &&
              contains(script, "/usr/share/applications/my-app.desktop") &&
              contains(script, "/usr/share/pixmaps/my-app.png") &&
              contains(script, "ln -s '/opt/my-app/licenses' \"$pkgdir/usr/share/licenses/my-app\""));

    check("deb arch", dist::debArch("x64") == "amd64" && dist::debArch("arm64") == "arm64" &&
                          dist::debArch("x86") == "i386");
    check("shlibs:Depends found among warnings",
          dist::parseShlibdeps("dpkg-shlibdeps: warning: x\nshlibs:Depends=libc6 (>= 2.34), libx11-6\r\n") ==
                  "libc6 (>= 2.34), libx11-6" &&
              dist::parseShlibdeps("nothing here\n").empty());

    dist::DebInput deb;
    deb.package = "my-app";
    deb.version = "1.2.0";
    deb.arch = "amd64";
    deb.maintainer = "Studio\nX";
    deb.description = "My App by Studio";
    deb.depends = "libc6 (>= 2.34)";
    deb.installedSizeKiB = 42;
    const std::string control = dist::debControl(deb);
    check("deb control: fields, one-line values",
          contains(control, "Package: my-app\n") && contains(control, "Version: 1.2.0\n") &&
              contains(control, "Architecture: amd64\n") && contains(control, "Maintainer: Studio X\n") &&
              contains(control, "Installed-Size: 42\n") && contains(control, "Depends: libc6 (>= 2.34)\n") &&
              contains(control, "Description: My App by Studio\n"));
    deb.depends.clear();
    check("deb control: no Depends line without dependencies",
          !contains(dist::debControl(deb), "Depends:"));
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

    // A PE32+ header (x64, no sections) whose NumberOfRvaAndSizes claims 16
    // data directories while the optional header has room for `room`, and the
    // file ends right after it: only the directories that are there are read.
    auto damagedPe = [](uint16_t room) {
        const uint16_t optionalSize = static_cast<uint16_t>(112 + room * 8);
        std::string d(0x40 + 24 + optionalSize, '\0');
        auto put16 = [&](size_t o, uint16_t v) { d[o] = char(v & 0xFF); d[o + 1] = char(v >> 8); };
        auto put32 = [&](size_t o, uint32_t v) { put16(o, uint16_t(v & 0xFFFF)); put16(o + 2, uint16_t(v >> 16)); };
        d[0] = 'M';
        d[1] = 'Z';
        put32(0x3C, 0x40);
        d.replace(0x40, 4, std::string("PE\0\0", 4));
        put16(0x40 + 4, 0x8664);
        put16(0x40 + 20, optionalSize);
        put16(0x40 + 24, 0x20B);
        put32(0x40 + 24 + 108, 16);
        return d;
    };
    for (uint16_t room : {0, 2}) {
        const fs::path file = scratch / ("damaged" + std::to_string(room) + ".exe");
        write(file, damagedPe(room));
        const auto damaged = dist::readBinaryInfo(file);
        check(room == 0 ? "PE claiming 16 directories with room for none: read within the file"
                        : "PE claiming 16 directories with room for 2: read within the file",
              damaged.valid && damaged.arch == "x64" && damaged.imports.empty());
    }
    check("debug CRT names", dist::isDebugCrt("vcruntime140d.dll") && dist::isDebugCrt("ucrtbased.dll") &&
                                 !dist::isDebugCrt("vcruntime140.dll"));
}

// Symlinks in bin/ and include folders (Linux .so name chains): kept as
// links when they stay inside the folder, refused otherwise.
void testSymlinks(const fs::path& self, const fs::path& scratch) {
#if defined(_WIN32)
    (void)self;
    (void)scratch;
#else
    const fs::path project = scratch / "links" / "myApp";
    const fs::path bin = project / "bin";
    auto plan = [&](const std::vector<dist::IncludeEntry>& include, dist::Plan& out, std::string& error) {
        dist::PlanInput input;
        input.platform = dist::Platform::Linux;
        input.projectDir = project;
        input.binDir = bin;
        input.outputDir = project / "dist";
        input.projectName = "myApp";
        input.config.name = "My App";
        input.config.version = "1.0.0";
        input.config.include = include;
        const bool ok = dist::makePlan(input, out, error);
        if (ok && !dist::writePackage(input, out, error)) return false;
        return ok;
    };
    auto link = [](const fs::path& target, const fs::path& at) {
        fs::create_directories(at.parent_path());
        fs::remove(at);
        fs::create_symlink(target, at);
    };
    fs::remove_all(scratch / "links");
    fs::create_directories(bin);
    fs::copy_file(self, bin / "myApp");
    write(bin / "libfoo.so.1.2", "library");
    link("libfoo.so.1.2", bin / "libfoo.so.1");
    link("../libfoo.so.1.2", bin / "lib" / "libfoo.so");
    write(project / "sdk" / "libsdk.so.3.0", "sdk");
    link("libsdk.so.3.0", project / "sdk" / "libsdk.so.3");

    dist::Plan p;
    std::string error;
    const bool ok = plan({{"sdk", "sdk"}}, p, error);
    check("symlinks inside bin/ and an include folder are packaged", ok);
    if (!ok) {
        std::printf("  error: %s\n", error.c_str());
    } else {
        const fs::path pkg = p.packageDir;
        check("the SONAME link stays a link to the same name",
              fs::is_symlink(pkg / "libfoo.so.1") && fs::read_symlink(pkg / "libfoo.so.1") == "libfoo.so.1.2" &&
                  read(pkg / "libfoo.so.1") == "library");
        check("a link into a parent folder of bin/ stays a link",
              fs::is_symlink(pkg / "lib" / "libfoo.so") && read(pkg / "lib" / "libfoo.so") == "library");
        check("a link in an include folder stays a link",
              fs::is_symlink(pkg / "sdk" / "libsdk.so.3") && read(pkg / "sdk" / "libsdk.so.3") == "sdk");
        check("the dry-run list shows links", contains(dist::formatPlan(p), "libfoo.so.1  -> libfoo.so.1.2"));
    }

    // Links whose target would not be in the package.
    struct Bad { const char* name; fs::path target; const char* message; };
    const Bad bad[] = {
        {"an absolute link is refused", "/usr/lib/libz.so.1", "points outside"},
        {"a link out of bin/ is refused", "../../outside.so", "points outside"},
        {"a dangling link is refused", "missing.so", "points to nothing"},
    };
    for (const Bad& b : bad) {
        link(b.target, bin / "bad.so");
        dist::Plan q;
        std::string message;
        check(b.name, !plan({}, q, message) && contains(message, b.message) && contains(message, "bad.so"));
        fs::remove(bin / "bad.so");
    }

    // A link out of bin/ names the include entry that would copy it.
    link("../../outside-models", bin / "data" / "models");
    {
        dist::Plan q;
        std::string message;
        check("a link out of bin/ suggests an include entry",
              !plan({}, q, message) && contains(message, "\"include\"") &&
                  contains(message, "outside-models\", \"to\": \"data/models\"}"));
    }
    fs::remove(bin / "data" / "models");

    // Nothing is written through a link the package has: an include entry
    // aimed into the folder link bin/sub -> real.
    write(bin / "real" / "a.txt", "real");
    link("real", bin / "sub");
    write(project / "extra.txt", "extra");
    {
        dist::Plan q;
        std::string message;
        check("no file is written through a link in the package",
              !plan({{"extra.txt", "sub/extra.txt"}}, q, message) && contains(message, "through the symlink") &&
                  !fs::exists(q.packageDir / "real" / "extra.txt"));
    }
    fs::remove(bin / "sub");
    fs::remove_all(bin / "real");

    // macOS bundle (laid out by hand, so it runs here too).
    const fs::path macProject = scratch / "links" / "macApp";
    const fs::path app = macProject / "bin" / "myApp.app";
    fs::create_directories(app / "Contents" / "MacOS");
    fs::copy_file(self, app / "Contents" / "MacOS" / "myApp");
    write(app / "Contents" / "Info.plist", "<plist><dict></dict></plist>");
    write(macProject / "bin" / "data" / "a.txt", "app data");
    const fs::path framework = app / "Contents" / "Frameworks" / "Foo.framework";
    write(framework / "Versions" / "A" / "Foo", "framework");
    link("A", framework / "Versions" / "Current");
    link("Versions/Current/Foo", framework / "Foo");
    auto macInput = [&] {
        dist::PlanInput input;
        input.platform = dist::Platform::MacOS;
        input.projectDir = macProject;
        input.binDir = macProject / "bin";
        input.outputDir = macProject / "dist";
        input.projectName = "myApp";
        input.config.name = "My App";
        input.config.version = "1.0.0";
        return input;
    };
    {
        dist::Plan q;
        std::string message;
        const auto input = macInput();
        const bool packaged = dist::makePlan(input, q, message) && dist::writePackage(input, q, message);
        const fs::path out = q.packageDir / "myApp.app" / "Contents";
        check("bundle: framework links inside it are kept",
              packaged && fs::is_symlink(out / "Frameworks" / "Foo.framework" / "Versions" / "Current") &&
                  fs::is_symlink(out / "Frameworks" / "Foo.framework" / "Foo") &&
                  read(out / "Resources" / "data" / "a.txt") == "app data");
    }
    // Contents/Resources linking out of the bundle: replacing data/ in the
    // copy would have removed the folder it points to.
    const fs::path outside = scratch / "links" / "outside";
    write(outside / "data" / "precious.txt", "keep");
    link(outside, app / "Contents" / "Resources");
    {
        dist::Plan q;
        std::string message;
        const auto input = macInput();
        check("bundle: a link out of it is refused before anything is written",
              !dist::makePlan(input, q, message) && contains(message, "points outside") &&
                  read(outside / "data" / "precious.txt") == "keep" && !fs::exists(outside / "data" / "a.txt"));
    }
#endif
}

// The output folder must be a real absolute path: an empty one would put the
// package folder in the current directory and replace a folder of that name.
void testOutputFolder(const fs::path& self, const fs::path& scratch) {
    const fs::path project = scratch / "output" / "myApp";
    const fs::path cwd = scratch / "output" / "cwd";
    fs::remove_all(scratch / "output");
    fs::create_directories(project / "bin");
#if defined(_WIN32)
    fs::copy_file(self, project / "bin" / "myApp.exe");
    const auto platform = dist::Platform::Windows;
#else
    fs::copy_file(self, project / "bin" / "myApp");
    const auto platform = dist::Platform::Linux;
#endif
    dist::PlanInput input;
    input.platform = platform;
    input.projectDir = project;
    input.binDir = project / "bin";
    input.outputDir = "";
    input.projectName = "myApp";
    input.config.name = "My App";
    input.config.version = "1.0.0";
    dist::Plan plan;
    std::string error;
    const bool planned = dist::makePlan(input, plan, error);
    write(cwd / plan.folderName / "keep.txt", "my files");
    const fs::path before = fs::current_path();
    fs::current_path(cwd);
    const bool written = planned && dist::writePackage(input, plan, error);
    fs::current_path(before);
    check("an empty output folder is refused, nothing in the current directory is replaced",
          planned && !written && contains(error, "absolute") && read(cwd / plan.folderName / "keep.txt") == "my files");
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
        check("launcher follows a link to it", contains(read(pkg / "myApp.sh"), "readlink -f \"$0\""));
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
    {
        // --init, then dist, on an app without an EULA.
        auto fromInit = input;
        std::vector<std::string> initWarnings;
        check("the --init template packages an app without an EULA",
              dist::parseConfig(dist::configTemplate("My App", "1.2.0", ""), fromInit.config, initWarnings, error) &&
                  dist::makePlan(fromInit, plan, error) && plan.eulaSource.empty());
        write(project / "EULA.txt", "my terms\n");
        check("the --init template ships EULA.txt once it is there",
              dist::makePlan(fromInit, plan, error) && plan.eulaSource.filename() == "EULA.txt");
        auto warnsAboutEncoding = [&] {
            for (const auto& w : plan.warnings)
                if (contains(w, "EULA.txt is not UTF-8")) return true;
            return false;
        };
        check("a UTF-8 EULA gives no encoding warning", !warnsAboutEncoding());
        write(project / "EULA.txt", "\xBB\xE7\xBF\xEB\xB1\xC7\n");  // CP949 text
        check("an EULA saved in a system code page is shipped with a warning",
              dist::makePlan(fromInit, plan, error) && plan.eulaSource.filename() == "EULA.txt" &&
                  warnsAboutEncoding());
        fs::remove(project / "EULA.txt");
    }

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
    testLinuxPackage();
    testGlobs();
    testNsis();
    testUtf8Text();
#if defined(_WIN32) || (defined(__APPLE__) && !TARGET_OS_IPHONE) || \
    (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
    fs::path exe = self;
#ifdef _WIN32
    if (exe.extension() != ".exe") exe += ".exe";
#endif
    testBinary(exe, scratch);
    testPackage(exe, scratch);
    testSymlinks(exe, scratch);
    testOutputFolder(exe, scratch);
#else
    std::printf("SKIP: packaging checks are desktop only\n");
#endif
    fs::remove_all(scratch, ec);
    std::printf("%s\n", failures ? "FAILED" : "ALL PASS");
    return failures ? 1 : 0;
}
