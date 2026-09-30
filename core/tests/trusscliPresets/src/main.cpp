// =============================================================================
// core/tests/trusscliPresets — regression test for trusscli's project files.
//
// Headless, console, exit code = pass/fail (build_all.py runs it in CI on
// macOS / Linux / Windows). local.cmake compiles trusscli's own sources
// (tools/src/ProjectGenerator.cpp, ProjectState.cpp, ...) into this test.
//
// Guards (#350): `trusscli update`, `addon add` and `addon remove` keep the
// project's IDE, web / android / ios targets and web backend.
//   - ProjectGenerator writes the IDE into CMakePresets.json as
//     "vendor": {"trussc": {"ide": ...}}, and readPresetState() reads it,
//     the targets and the web backend back (round trip through the real
//     writer, for every IDE).
//   - applyGenerationOptions(): the presets fill the settings, explicit flags
//     win, and without a CMakePresets.json the old defaults stay.
//   - parseTargetFlag(): --no-web / --no-android / --no-ios, and --web with
//     --no-web is an error.
//
// Guards (#357): `trusscli build` and `trusscli clean`.
//   - ProjectGenerator::buildDirForPreset() is the one preset -> build folder
//     mapping (ios -> xcode-ios), and the written presets' binaryDir follow it.
//   - planConfigure(): a build folder without a CMake cache is configured
//     first (one message, the build-type pin folded in), a cache that holds
//     what was asked for stays configure-free.
//   - checkPresetToolchain() / shouldRefreshPresets(): a Visual Studio
//     update that removed a pinned MSVC / SDK / ninja path is found (fake
//     filesystem), and only a native Windows build refreshes the presets.
//     On Windows, also through the real writer and filesystem.
// Not covered: the commands themselves (tools/src/main.cpp), which call these
// functions; the IDE files and CMake configure that `update` runs; the
// Visual Studio detection and the refresh on a real Windows toolchain change.
// =============================================================================

#include <TrussC.h>

#include "BuildSetup.h"
#include "ProjectGenerator.h"
#include "ProjectState.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;
using namespace tc;
namespace fs = std::filesystem;

static int g_fail = 0;
static void check(const string& name, bool ok) {
    std::printf("%-64s %s\n", name.c_str(), ok ? "PASS" : "FAIL");
    std::fflush(stdout);   // flush per line so CI logs survive a later crash
    if (!ok) ++g_fail;
}

static string readFile(const fs::path& path) {
    ifstream f(path, ios::binary);
    stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void writeFile(const fs::path& path, const string& text) {
    ofstream f(path, ios::binary);
    f << text;
}

// A fresh scratch project folder for one case.
static fs::path g_root;
static fs::path makeProject(const string& name) {
    fs::path dir = g_root / name;
    fs::remove_all(dir);
    fs::create_directories(dir / "src");
    return dir;
}

// Settings as `trusscli new` / the GUI would pass them, minus the build
// environment (no Visual Studio detection: the presets then carry no
// pinned toolchain, which this test does not need).
static ProjectSettings baseSettings(const fs::path& project) {
    ProjectSettings s;
    s.projectName = project.filename().string();
    s.projectDir = project.parent_path().string();
    s.tcRoot = g_root.string();
    return s;
}

static bool writePresets(const ProjectSettings& s, const fs::path& project) {
    ProjectGenerator gen(s);
    return gen.writePresets(project.string()).empty() &&
           fs::exists(project / "CMakePresets.json");
}

static const IdeType kAllIdes[] = {
    IdeType::CMakeOnly, IdeType::VSCode, IdeType::Cursor,
    IdeType::Xcode, IdeType::VisualStudio,
};

// -----------------------------------------------------------------------------
// 1. The IDE survives a write + read of CMakePresets.json, for every IDE
// -----------------------------------------------------------------------------
static void testIdeRoundTrip() {
    for (IdeType ide : kAllIdes) {
        string id = IdeHelper::getIdeId(ide);
        fs::path project = makeProject("ide-" + id);
        ProjectSettings s = baseSettings(project);
        s.ideType = ide;
        bool written = writePresets(s, project);
        PresetState st = readPresetState(project.string());
        check("ide round trip: " + id + " written and read back",
              written && st.found && st.hasIde && st.ide == ide);

        // Stored where the decision put it, under the stable --ide id
        Json j = Json::parse(readFile(project / "CMakePresets.json"), nullptr, false);
        bool stored = j.is_object() && j.contains("vendor") &&
                      j["vendor"].contains("trussc") &&
                      j["vendor"]["trussc"].value("ide", "") == id;
        check("ide round trip: " + id + " stored as vendor.trussc.ide", stored);

        IdeType parsed = IdeType::VSCode;
        check("ide id: " + id + " parses back",
              IdeHelper::parseIdeId(id, parsed) && parsed == ide);
    }
}

// -----------------------------------------------------------------------------
// 2. Targets and the web backend survive a write + read
// -----------------------------------------------------------------------------
static void testTargetRoundTrip() {
    {
        fs::path project = makeProject("targets-web-gl-android");
        ProjectSettings s = baseSettings(project);
        s.generateWebBuild = true;
        s.webBackend = 1;   // WebGL
        s.generateAndroidBuild = true;
        bool written = writePresets(s, project);
        PresetState st = readPresetState(project.string());
        check("targets round trip: web (WebGL) + android",
              written && st.found && st.web && st.webBackend == 1 && st.android);
    }
    {
        fs::path project = makeProject("targets-web-gpu");
        ProjectSettings s = baseSettings(project);
        s.generateWebBuild = true;
        s.webBackend = 0;   // WebGPU
        bool written = writePresets(s, project);
        PresetState st = readPresetState(project.string());
        check("targets round trip: web (WebGPU), no android / ios",
              written && st.web && st.webBackend == 0 && !st.android && !st.ios);
    }
    {
        fs::path project = makeProject("targets-native");
        ProjectSettings s = baseSettings(project);
        bool written = writePresets(s, project);
        PresetState st = readPresetState(project.string());
        check("targets round trip: native only",
              written && st.found && !st.web && !st.android && !st.ios);
    }
    {
        // The ios preset is written on a macOS host only
        fs::path project = makeProject("targets-ios");
        ProjectSettings s = baseSettings(project);
        s.generateIosBuild = true;
        bool written = writePresets(s, project);
        PresetState st = readPresetState(project.string());
#ifdef __APPLE__
        check("targets round trip: ios (macOS host)", written && st.ios);
#else
        check("targets round trip: no ios preset off macOS", written && !st.ios);
#endif
    }
}

// -----------------------------------------------------------------------------
// 3. Reading presets that are missing, broken or from an older trusscli
// -----------------------------------------------------------------------------
static void testParseEdgeCases() {
    fs::path project = makeProject("no-presets");
    PresetState st = readPresetState(project.string());
    check("no CMakePresets.json: found == false", !st.found);

    check("malformed JSON: found == false", !parsePresetState("{ not json").found);
    check("JSON array: found == false", !parsePresetState("[1, 2]").found);

    // Written by a trusscli before #350: no vendor entry
    PresetState old = parsePresetState(R"({
        "version": 6,
        "configurePresets": [
            {"name": "linux"},
            {"name": "web", "cacheVariables": {"TC_WEB_BACKEND": "GLES3"}}
        ]
    })");
    check("pre-#350 presets: targets read, no IDE",
          old.found && old.web && old.webBackend == 1 && !old.hasIde);

    PresetState unknown = parsePresetState(
        R"({"configurePresets": [], "vendor": {"trussc": {"ide": "emacs"}}})");
    check("unknown IDE id is ignored", unknown.found && !unknown.hasIde);

    PresetState wrongType = parsePresetState(
        R"({"configurePresets": {"name": "web"}, "vendor": {"trussc": "cursor"}})");
    check("wrongly typed fields are ignored",
          wrongType.found && !wrongType.web && !wrongType.hasIde);

    PresetState noBackend = parsePresetState(R"({"configurePresets": [{"name": "web"}]})");
    check("web preset without TC_WEB_BACKEND means WebGPU",
          noBackend.web && noBackend.webBackend == 0);

    // A hand-edited file with CRLF line endings still parses
    writeFile(project / "CMakePresets.json",
              "{\r\n \"configurePresets\": [{\"name\": \"android\"}],\r\n"
              " \"vendor\": {\"trussc\": {\"ide\": \"cmake\"}}\r\n}\r\n");
    PresetState crlf = readPresetState(project.string());
    check("CRLF file read from disk",
          crlf.found && crlf.android && crlf.hasIde && crlf.ide == IdeType::CMakeOnly);
}

// -----------------------------------------------------------------------------
// 4. Precedence: presets fill the settings, explicit flags win
// -----------------------------------------------------------------------------
static void testPrecedence() {
    PresetState saved;
    saved.found = true;
    saved.web = true;
    saved.webBackend = 1;
    saved.hasIde = true;
    saved.ide = IdeType::Cursor;

    {
        ProjectSettings s;
        applyGenerationOptions(s, PresetState(), GenerationFlags());
        check("no presets, no flags: defaults (vscode, native, WebGPU)",
              s.ideType == IdeType::VSCode && !s.generateWebBuild &&
              !s.generateAndroidBuild && !s.generateIosBuild && s.webBackend == 0);
    }
    {
        // addon add / addon remove / plain update
        ProjectSettings s;
        applyGenerationOptions(s, saved, GenerationFlags());
        check("presets, no flags: IDE, web and backend kept",
              s.ideType == IdeType::Cursor && s.generateWebBuild &&
              s.webBackend == 1 && !s.generateAndroidBuild);
    }
    {
        ProjectSettings s;
        GenerationFlags f;
        f.ide = IdeType::VSCode;
        applyGenerationOptions(s, saved, f);
        check("--ide vscode switches back, targets kept",
              s.ideType == IdeType::VSCode && s.generateWebBuild && s.webBackend == 1);
    }
    {
        ProjectSettings s;
        GenerationFlags f;
        f.web = false;
        applyGenerationOptions(s, saved, f);
        check("--no-web drops a saved web target, IDE kept",
              !s.generateWebBuild && s.ideType == IdeType::Cursor);
    }
    {
        ProjectSettings s;
        GenerationFlags f;
        f.android = true;
        applyGenerationOptions(s, saved, f);
        check("--android adds a target, saved web kept",
              s.generateAndroidBuild && s.generateWebBuild && s.webBackend == 1);
    }
    {
        ProjectSettings s;
        GenerationFlags f;
        f.web = true;
        f.ide = IdeType::Xcode;
        applyGenerationOptions(s, PresetState(), f);
        check("no presets: flags alone decide",
              s.generateWebBuild && s.ideType == IdeType::Xcode && s.webBackend == 0);
    }
    {
        // Presets without a vendor entry (older trusscli): IDE stays default
        PresetState old = saved;
        old.hasIde = false;
        old.ide = IdeType::Xcode;   // must not be used when hasIde is false
        ProjectSettings s;
        applyGenerationOptions(s, old, GenerationFlags());
        check("presets without IDE entry: IDE default, targets kept",
              s.ideType == IdeType::VSCode && s.generateWebBuild);
    }
    {
        // The whole loop through the real writer: a cursor + web project,
        // then a regeneration without flags writes the same options again.
        fs::path project = makeProject("regenerate");
        ProjectSettings first = baseSettings(project);
        first.ideType = IdeType::Cursor;
        first.generateWebBuild = true;
        first.webBackend = 1;
        bool w1 = writePresets(first, project);

        ProjectSettings again = baseSettings(project);
        applyGenerationOptions(again, readPresetState(project.string()), GenerationFlags());
        bool w2 = writePresets(again, project);
        PresetState st = readPresetState(project.string());
        check("regenerate without flags keeps cursor + web (WebGL)",
              w1 && w2 && st.hasIde && st.ide == IdeType::Cursor &&
              st.web && st.webBackend == 1);
    }
}

// -----------------------------------------------------------------------------
// 5. The target flags of `trusscli update`
// -----------------------------------------------------------------------------
static void testTargetFlags() {
    {
        GenerationFlags f;
        string err;
        bool a = parseTargetFlag("--no-web", f, err);
        bool b = parseTargetFlag("--no-android", f, err);
        bool c = parseTargetFlag("--no-ios", f, err);
        check("--no-web / --no-android / --no-ios turn targets off",
              a && b && c && err.empty() &&
              f.web == false && f.android == false && f.ios == false);
    }
    {
        GenerationFlags f;
        string err;
        bool a = parseTargetFlag("--web", f, err);
        bool b = parseTargetFlag("--android", f, err);
        bool c = parseTargetFlag("--ios", f, err);
        check("--web / --android / --ios turn targets on",
              a && b && c && err.empty() &&
              f.web == true && f.android == true && f.ios == true);
    }
    {
        GenerationFlags f;
        string err;
        parseTargetFlag("--web", f, err);
        bool handled = parseTargetFlag("--no-web", f, err);
        check("--web with --no-web is an error", handled && !err.empty());
    }
    {
        GenerationFlags f;
        string err;
        parseTargetFlag("--android", f, err);
        parseTargetFlag("--android", f, err);
        check("a repeated flag is fine", err.empty() && f.android == true);
    }
    {
        GenerationFlags f;
        string err;
        bool ide = parseTargetFlag("--ide", f, err);
        bool other = parseTargetFlag("--no-webgl", f, err);
        bool none = parseTargetFlag("web", f, err);
        check("other arguments are left alone",
              !ide && !other && !none && err.empty() && !f.web && !f.ide);
    }
}

// -----------------------------------------------------------------------------
// 6. One preset -> build folder mapping, used by the writer too
// -----------------------------------------------------------------------------
static void testBuildDirMapping() {
    check("build dir: macos -> build-macos",
          ProjectGenerator::buildDirForPreset("macos") == "build-macos");
    check("build dir: linux -> build-linux",
          ProjectGenerator::buildDirForPreset("linux") == "build-linux");
    check("build dir: windows -> build-windows",
          ProjectGenerator::buildDirForPreset("windows") == "build-windows");
    check("build dir: web -> build-web",
          ProjectGenerator::buildDirForPreset("web") == "build-web");
    check("build dir: android -> build-android",
          ProjectGenerator::buildDirForPreset("android") == "build-android");
    check("build dir: ios -> xcode-ios",
          ProjectGenerator::buildDirForPreset("ios") == "xcode-ios");

    const auto& all = ProjectGenerator::allPresetNames();
    bool hasIos = find(all.begin(), all.end(), "ios") != all.end();
    check("all preset names include ios (clean --all removes xcode-ios)",
          all.size() == 6 && hasIos);

    // Every preset the writer emits puts its build folder where the helper says
    fs::path project = makeProject("binary-dirs");
    ProjectSettings s = baseSettings(project);
    s.generateWebBuild = true;
    s.generateAndroidBuild = true;
    s.generateIosBuild = true;
    bool written = writePresets(s, project);
    Json j = Json::parse(readFile(project / "CMakePresets.json"), nullptr, false);
    int presets = 0, matching = 0;
    if (j.is_object() && j.contains("configurePresets")) {
        for (const auto& p : j["configurePresets"]) {
            ++presets;
            string expected = "${sourceDir}/" +
                ProjectGenerator::buildDirForPreset(p.value("name", ""));
            if (p.value("binaryDir", "") == expected) ++matching;
        }
    }
    check("written presets' binaryDir match the helper",
          written && presets >= 3 && matching == presets);
}

// -----------------------------------------------------------------------------
// 7. `trusscli build`: when to configure first
// -----------------------------------------------------------------------------
static bool hasDefine(const ConfigurePlan& p, const string& d) {
    return find(p.defines.begin(), p.defines.end(), d) != p.defines.end();
}
static bool firstMessageHas(const ConfigurePlan& p, const string& text) {
    return !p.messages.empty() && p.messages[0].find(text) != string::npos;
}

static void testConfigurePlan() {
    ConfigureInputs native;
    native.buildDir = "build-linux";
    native.isNative = true;

    {
        // `trusscli clean` then a plain `trusscli build`
        ConfigurePlan p = planConfigure(native);
        check("no cache, no flag: configure, no -D, one message",
              p.configure && p.defines.empty() && p.messages.size() == 1 &&
              firstMessageHas(p, "No CMake cache in build-linux"));
    }
    {
        ConfigureInputs in = native;
        in.requestedBuildType = "Release";
        ConfigurePlan p = planConfigure(in);
        check("no cache, --release: one configure with the type, one message",
              p.configure && p.defines.size() == 1 &&
              hasDefine(p, "-DCMAKE_BUILD_TYPE=Release") && p.messages.size() == 1 &&
              firstMessageHas(p, "No CMake cache in build-linux") &&
              firstMessageHas(p, "CMAKE_BUILD_TYPE=Release"));
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.cachedBuildType = "RelWithDebInfo";
        ConfigurePlan p = planConfigure(in);
        check("cache holds the default, no flag: no configure (steady state)",
              !p.configure && p.defines.empty() && p.messages.empty());
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.cachedBuildType = "Release";
        in.requestedBuildType = "Release";
        ConfigurePlan p = planConfigure(in);
        check("cache holds the requested type: no configure", !p.configure);
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.cachedBuildType = "Debug";
        ConfigurePlan p = planConfigure(in);
        check("cache Debug, no flag: switch back to RelWithDebInfo",
              p.configure && hasDefine(p, "-DCMAKE_BUILD_TYPE=RelWithDebInfo") &&
              firstMessageHas(p, "Switching build type: Debug -> RelWithDebInfo"));
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.cachedBuildType = "RelWithDebInfo";
        in.warnings = true;
        ConfigurePlan p = planConfigure(in);
        check("--warnings with a cache: configure with TRUSSC_WARNINGS only",
              p.configure && p.defines.size() == 1 && hasDefine(p, "-DTRUSSC_WARNINGS=ON"));
    }
    {
        ConfigureInputs in;
        in.buildDir = "build-web";
        in.requestedBuildType = "Release";
        ConfigurePlan p = planConfigure(in);
        check("web, no cache: configure, keeps the preset's build type",
              p.configure && p.defines.empty() && p.messages.size() == 1 &&
              firstMessageHas(p, "No CMake cache in build-web"));
    }
    {
        ConfigureInputs in;
        in.buildDir = "build-web";
        in.hasCache = true;
        in.cachedBuildType = "MinSizeRel";
        in.requestedBuildType = "Debug";
        ConfigurePlan p = planConfigure(in);
        check("web with a cache: no configure, no build-type pin", !p.configure);
    }
    {
        ConfigureInputs in;
        in.buildDir = "xcode-ios";
        ConfigurePlan p = planConfigure(in);
        check("ios, no cache: configure xcode-ios",
              p.configure && p.defines.empty() && firstMessageHas(p, "xcode-ios"));
    }
}

// -----------------------------------------------------------------------------
// 8. Stale Visual Studio toolchain pinned in the windows preset
// -----------------------------------------------------------------------------
static const char* kVs = "C:/Program Files/Microsoft Visual Studio/2022/Community";
static const char* kKits = "C:/Program Files (x86)/Windows Kits/10";

// A windows preset in the shape writeCMakePresets() writes it.
static string windowsPresets(const string& msvc, const string& sdk, bool withEnv = true,
                             bool withNinja = true) {
    string vs = kVs, kits = kKits;
    Json p;
    p["name"] = "windows";
    p["binaryDir"] = "${sourceDir}/build-windows";
    if (withNinja) {
        p["cacheVariables"]["CMAKE_MAKE_PROGRAM"] =
            vs + "/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe";
    }
    if (withEnv) {
        p["environment"]["INCLUDE"] = vs + "/VC/Tools/MSVC/" + msvc + "/include;" +
            kits + "/Include/" + sdk + "/ucrt;" + kits + "/Include/" + sdk + "/um";
        p["environment"]["LIB"] = vs + "/VC/Tools/MSVC/" + msvc + "/lib/x64;" +
            kits + "/Lib/" + sdk + "/um/x64";
        p["environment"]["PATH"] = vs + "/VC/Tools/MSVC/" + msvc + "/bin/Hostx64/x64;" +
            kits + "/bin/" + sdk + "/x64;$penv{PATH}";
    }
    Json j;
    j["version"] = 6;
    j["configurePresets"] = Json::array({p});
    return j.dump(2);
}

static void testToolchainCheck() {
    // The machine after an update: MSVC 14.44 and SDK 26100 are installed
    auto installed = [](const string& path) {
        if (path.find('$') != string::npos) return false;   // a macro is never a path
        return path.find("/MSVC/14.43.") == string::npos &&
               path.find("/10.0.22621.0/") == string::npos &&
               path.find("/2019/") == string::npos;
    };

    {
        ToolchainCheck c = checkPresetToolchain(windowsPresets("14.44.35207", "10.0.26100.0"), installed);
        check("toolchain: all pinned paths exist -> not stale",
              c.pinned && c.missing.empty() && !c.stale());
    }
    {
        ToolchainCheck c = checkPresetToolchain(windowsPresets("14.43.34808", "10.0.26100.0"), installed);
        bool allMsvc = !c.missing.empty();
        for (const auto& m : c.missing) allMsvc = allMsvc && m.find("/MSVC/14.43.") != string::npos;
        check("toolchain: MSVC folder replaced -> stale, lists include/lib/bin",
              c.stale() && c.missing.size() == 3 && allMsvc);
    }
    {
        ToolchainCheck c = checkPresetToolchain(windowsPresets("14.44.35207", "10.0.22621.0"), installed);
        check("toolchain: Windows SDK removed -> stale", c.stale() && c.missing.size() == 4);
    }
    {
        // VS 2019 -> 2022 move: ninja lives under the old install too
        string text = windowsPresets("14.44.35207", "10.0.26100.0");
        string from = "/2022/";
        size_t pos = text.find(from);
        text.replace(pos, from.size(), "/2019/");   // only the ninja path (first occurrence)
        ToolchainCheck c = checkPresetToolchain(text, installed);
        check("toolchain: ninja under a removed VS -> stale, only ninja listed",
              c.stale() && c.missing.size() == 1 &&
              c.missing[0].find("ninja.exe") != string::npos);
    }
    {
        ToolchainCheck c = checkPresetToolchain(
            windowsPresets("14.43.34808", "10.0.26100.0", /*withEnv=*/false, /*withNinja=*/false),
            installed);
        check("toolchain: no environment, no ninja (VS not found) -> nothing pinned",
              !c.pinned && !c.stale());
    }
    {
        ToolchainCheck c = checkPresetToolchain(
            windowsPresets("14.43.34808", "10.0.26100.0", /*withEnv=*/false, /*withNinja=*/true),
            installed);
        check("toolchain: ninja only, present -> pinned, not stale", c.pinned && !c.stale());
    }
    {
        int macroCalls = 0;
        auto counting = [&](const string& path) {
            if (path.find('$') != string::npos) ++macroCalls;
            return true;
        };
        checkPresetToolchain(windowsPresets("14.44.35207", "10.0.26100.0"), counting);
        check("toolchain: $penv{PATH} is not checked as a path", macroCalls == 0);
    }
    {
        // Same stale path twice (hand-edited PATH) is listed once
        Json j = Json::parse(windowsPresets("14.43.34808", "10.0.26100.0"));
        string bin = string(kVs) + "/VC/Tools/MSVC/14.43.34808/bin/Hostx64/x64";
        j["configurePresets"][0]["environment"]["PATH"] = bin + ";" + bin + ";$penv{PATH}";
        ToolchainCheck c = checkPresetToolchain(j.dump(), installed);
        int binCount = 0;
        for (const auto& m : c.missing) if (m == bin) ++binCount;
        check("toolchain: a missing path is listed once", c.stale() && binCount == 1);
    }
    {
        // CMAKE_MAKE_PROGRAM in the {"type", "value"} form
        Json j = Json::parse(windowsPresets("14.44.35207", "10.0.26100.0", false, false));
        j["configurePresets"][0]["cacheVariables"]["CMAKE_MAKE_PROGRAM"] =
            Json{{"type", "FILEPATH"}, {"value", "C:/Program Files/Microsoft Visual Studio/2019/ninja.exe"}};
        ToolchainCheck c = checkPresetToolchain(j.dump(), installed);
        check("toolchain: CMAKE_MAKE_PROGRAM object form is checked",
              c.stale() && c.missing.size() == 1);
    }
    {
        auto none = [](const string&) { return false; };
        ToolchainCheck other = checkPresetToolchain(
            R"({"configurePresets": [{"name": "linux", "cacheVariables": {"CMAKE_MAKE_PROGRAM": "/nope/make"}}]})",
            none);
        ToolchainCheck broken = checkPresetToolchain("{ nope", none);
        check("toolchain: other presets and broken text are not checked",
              !other.pinned && !broken.pinned);
    }

    // The refresh keeps the project's TRUSSC_DIR from the old presets
    {
        const char* text = R"({"configurePresets": [
            {"name": "windows", "cacheVariables": {"TRUSSC_DIR": "D:/TrussC/core"}},
            {"name": "web", "cacheVariables": {"TRUSSC_DIR": {"type": "PATH", "value": "E:/x/core"}}}
        ]})";
        check("preset cache variable: string and object forms, missing ones empty",
              presetCacheVariable(text, "windows", "TRUSSC_DIR") == "D:/TrussC/core" &&
              presetCacheVariable(text, "web", "TRUSSC_DIR") == "E:/x/core" &&
              presetCacheVariable(text, "linux", "TRUSSC_DIR").empty() &&
              presetCacheVariable(text, "windows", "CMAKE_MAKE_PROGRAM").empty() &&
              presetCacheVariable("{ nope", "windows", "TRUSSC_DIR").empty());
    }

    // What `trusscli build` does with the result
    ToolchainCheck stale;
    stale.pinned = true;
    stale.missing = {"C:/old/ninja.exe"};
    ToolchainCheck fine;
    fine.pinned = true;
    check("refresh: native windows build with a stale toolchain",
          shouldRefreshPresets("windows", "windows", stale));
    check("refresh: not for a web / android build on Windows",
          !shouldRefreshPresets("windows", "web", stale) &&
          !shouldRefreshPresets("windows", "android", stale));
    check("refresh: not on macOS / Linux",
          !shouldRefreshPresets("linux", "linux", stale) &&
          !shouldRefreshPresets("macos", "macos", stale));
    check("refresh: not when the toolchain is current",
          !shouldRefreshPresets("windows", "windows", fine) &&
          !shouldRefreshPresets("windows", "windows", ToolchainCheck()));

#ifdef _WIN32
    // Through the real writer on Windows: a preset pinned to a Visual Studio
    // that is not installed is stale on the real filesystem.
    {
        fs::path project = makeProject("stale-vs");
        ProjectSettings s = baseSettings(project);
        VsVersionInfo vs;
        vs.version = 17;
        vs.installPath = "C:/NoSuchVisualStudio/2022";
        vs.ninjaPath = "C:/NoSuchVisualStudio/2022/ninja.exe";
        vs.vcToolsVersion = "14.99.99999";
        vs.windowsSdkVersion = "10.0.99999.0";
        s.installedVsVersions = {vs};
        bool written = writePresets(s, project);
        ToolchainCheck c = checkPresetToolchain(
            readFile(project / "CMakePresets.json"),
            [](const string& p) { std::error_code ec; return fs::exists(p, ec); });
        bool ninjaListed = find(c.missing.begin(), c.missing.end(),
                                "C:/NoSuchVisualStudio/2022/ninja.exe") != c.missing.end();
        check("toolchain (Windows): writer's pinned paths are checked", written && c.stale() && ninjaListed);
    }
#endif
}

int main() {
    g_root = fs::temp_directory_path() /
             ("trusscliPresets-" + to_string(chrono::steady_clock::now()
                                                .time_since_epoch().count()));
    fs::create_directories(g_root);

    testIdeRoundTrip();
    testTargetRoundTrip();
    testParseEdgeCases();
    testPrecedence();
    testTargetFlags();
    testBuildDirMapping();
    testConfigurePlan();
    testToolchainCheck();

    std::error_code ec;
    fs::remove_all(g_root, ec);

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
