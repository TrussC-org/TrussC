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
//   - Saved settings that cannot be used are reported, not dropped silently:
//     an unparsable file, wrongly typed entries, an unknown IDE id, an IDE
//     this OS cannot generate (xcode off macOS, vs off Windows), and an ios
//     target off macOS (left out of the summary too; an explicit --ios off
//     macOS gets the same warning).
//   - TC_WEB_BACKEND is read the way CMake builds it ("WGPU" or unset is
//     WebGPU, anything else GLES3, warned unless "GLES3"), as a string or in
//     the {"type": ..., "value": ...} form.
//   - prepareRegeneration(): the settings setup that update / addon add /
//     addon remove share (tools/src/main.cpp calls it for all three). A
//     target dropped with --no-<target> whose build folder or generated
//     build script is still there gets a one-line notice (nothing removed).
//   - The toolchainFile of a kept web / android preset survives a
//     regeneration from a shell without emsdk / the NDK (chooseToolchainFile,
//     and through the real writer with EMSDK / PATH / ANDROID_* set per case);
//     an ANDROID_NDK_HOME without the toolchain file does not beat it.
//   - A kept target whose configure fails is a warning, a target asked for
//     by a flag an error (ProjectGenerator::update with a toolchain that
//     fails on purpose; needs cmake in PATH).
//
// Guards (#357): `trusscli build` and `trusscli clean`.
//   - ProjectGenerator::buildDirForPreset() is the one preset -> build folder
//     mapping (ios -> xcode-ios), and the written presets' binaryDir follow it.
//   - planConfigure(): a build folder without a CMake cache, or with only the
//     cache of a failed configure, is configured first (one message, the
//     build-type pin folded in); a cache that holds what was asked for stays
//     configure-free.
//   - inspectBuildFolder() / buildFoldersToClean(): the folder `build` looks
//     at (ios -> xcode-ios) and what it finds there on a real filesystem,
//     and the folders `clean` / `clean --all` remove; buildScriptsToClean():
//     the generated build-web.* scripts `clean --all` removes too.
//   - checkPresetToolchain() / shouldRefreshPresets(): a Visual Studio
//     update that removed a pinned MSVC / SDK / ninja path is found (fake
//     filesystem), and only a native Windows build refreshes the presets.
//     On Windows, also through the real writer and filesystem.
//   - windowsToolchainPins() / repinWindowsToolchain(): the refresh replaces
//     only the windows preset's pins, keeps TRUSSC_DIR, the other presets
//     and the IDE, and the no-VS fallback entry cannot pin.
// Guards (#354), on Windows: the build_win.bat configure/retry/build paths
// and a generated build-web.bat stop on positive and negative CMake exits,
// while zero exits and a successful configure retry still complete. Scratch
// copies run in cmd with CMake calls replaced by `cmd /c exit <code>`.
// Not covered: the argument parsing and output of the commands themselves
// (tools/src/main.cpp), which call these functions; the IDE files and the
// native CMake configure; the Visual Studio detection and the refresh on a
// real Windows toolchain change.
// =============================================================================

#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include "BuildSetup.h"
#include "ProjectGenerator.h"
#include "ProjectState.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;
using namespace tc;

namespace {
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

// Set / unset an environment variable for one case, restored on scope exit
class EnvOverride {
public:
    EnvOverride(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) { had_ = true; old_ = old; }
        set(value);
    }
    ~EnvOverride() { set(had_ ? old_.c_str() : nullptr); }
    EnvOverride(const EnvOverride&) = delete;
    EnvOverride& operator=(const EnvOverride&) = delete;
private:
    void set(const char* value) {
#ifdef _WIN32
        _putenv_s(name_.c_str(), value ? value : "");
#else
        if (value) setenv(name_.c_str(), value, 1);
        else unsetenv(name_.c_str());
#endif
    }
    string name_;
    string old_;
    bool had_ = false;
};

// toolchainFile of the named configure preset in <project>/CMakePresets.json
static string presetToolchain(const fs::path& project, const string& name) {
    Json j = Json::parse(readFile(project / "CMakePresets.json"), nullptr, false);
    if (!j.is_object() || !j.contains("configurePresets")) return "<no presets>";
    for (const auto& p : j["configurePresets"]) {
        if (p.value("name", "") == name) return p.value("toolchainFile", "<none>");
    }
    return "<no " + name + " preset>";
}

static const char* kEmEnvToolchain =
    "$env{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake";
static const char* kNdkEnvToolchain =
    "$env{ANDROID_NDK_HOME}/build/cmake/android.toolchain.cmake";

static const IdeType kAllIdes[] = {
    IdeType::CMakeOnly, IdeType::VSCode, IdeType::Cursor,
    IdeType::Xcode, IdeType::VisualStudio,
};

// Whether this host can generate projects for `ide` (the GUI's IDE list)
static bool ideOnThisHost(IdeType ide) {
#ifndef __APPLE__
    if (ide == IdeType::Xcode) return false;
#endif
#ifndef _WIN32
    if (ide == IdeType::VisualStudio) return false;
#endif
    return true;
}

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
        if (ideOnThisHost(ide)) {
            check("ide round trip: " + id + " written and read back",
                  written && st.found && st.hasIde && st.ide == ide &&
                  st.ideWarning.empty() && st.warnings.empty());
        } else {
            // Written elsewhere (shared folder), read here: not used, reported
            check("ide round trip: " + id + " is ignored with a warning on this OS",
                  written && st.found && !st.hasIde &&
                  st.ideWarning.find(id) != string::npos && st.warnings.empty());
        }

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
    check("no CMakePresets.json: found == false, no warning",
          !st.found && st.warnings.empty() && st.ideWarning.empty());

    PresetState malformed = parsePresetState("{ \"configurePresets\": [], }");
    check("malformed JSON: found == false, warned",
          !malformed.found && malformed.warnings.size() == 1);
    PresetState array = parsePresetState("[1, 2]");
    check("JSON array: found == false, warned", !array.found && array.warnings.size() == 1);

    // An unparsable file on disk is reported too
    fs::path broken = makeProject("broken-presets");
    writeFile(broken / "CMakePresets.json", "{ \"vendor\": {\"trussc\": {\"ide\": \"cursor\"}}, }");
    PresetState brokenSt = readPresetState(broken.string());
    check("unparsable file on disk: found == false, warned",
          !brokenSt.found && !brokenSt.hasIde && brokenSt.warnings.size() == 1);

    fs::path dirPresets = makeProject("presets-is-a-dir");
    fs::create_directories(dirPresets / "CMakePresets.json");
    PresetState dirSt = readPresetState(dirPresets.string());
    check("CMakePresets.json that cannot be read: found == false, warned",
          !dirSt.found && dirSt.warnings.size() == 1);

    // Written by a trusscli before #350: no vendor entry
    PresetState old = parsePresetState(R"({
        "version": 6,
        "configurePresets": [
            {"name": "linux"},
            {"name": "web", "cacheVariables": {"TC_WEB_BACKEND": "GLES3"}}
        ]
    })");
    check("pre-#350 presets: targets read, no IDE, no warning",
          old.found && old.web && old.webBackend == 1 && !old.hasIde &&
          old.ideWarning.empty() && old.warnings.empty());

    PresetState unknown = parsePresetState(
        R"({"configurePresets": [], "vendor": {"trussc": {"ide": "emacs"}}})");
    check("unknown IDE id is ignored with a warning naming it",
          unknown.found && !unknown.hasIde &&
          unknown.ideWarning.find("'emacs'") != string::npos);

    // Ids are exact: a hand-edited "Cursor" is not "cursor"
    PresetState miscased = parsePresetState(R"({"vendor": {"trussc": {"ide": "Cursor"}}})");
    check("miscased IDE id is ignored with a warning",
          miscased.found && !miscased.hasIde && !miscased.ideWarning.empty());

    PresetState wrongType = parsePresetState(
        R"({"configurePresets": {"name": "web"}, "vendor": {"trussc": "cursor"}})");
    check("wrongly typed fields are ignored with warnings",
          wrongType.found && !wrongType.web && !wrongType.hasIde &&
          wrongType.warnings.size() == 1 && !wrongType.ideWarning.empty());

    PresetState ideNumber = parsePresetState(R"({"vendor": {"trussc": {"ide": 3}}})");
    PresetState vendorArray = parsePresetState(R"({"vendor": ["trussc"]})");
    check("non-string ide / non-object vendor: ignored with a warning",
          !ideNumber.hasIde && !ideNumber.ideWarning.empty() &&
          !vendorArray.hasIde && !vendorArray.ideWarning.empty());

    // Other tools' vendor data is none of our business
    PresetState otherVendor = parsePresetState(R"({"vendor": {"someTool": 1}})");
    check("vendor entry without trussc: no IDE, no warning",
          otherVendor.found && !otherVendor.hasIde && otherVendor.ideWarning.empty());

    // A project folder shared between OSes: xcode / vs saved elsewhere
    PresetState xcode = parsePresetState(R"({"vendor": {"trussc": {"ide": "xcode"}}})");
    PresetState vs = parsePresetState(R"({"vendor": {"trussc": {"ide": "vs"}}})");
#ifdef __APPLE__
    check("saved xcode is used on macOS", xcode.hasIde && xcode.ide == IdeType::Xcode);
#else
    check("saved xcode is ignored off macOS, with a warning",
          !xcode.hasIde && xcode.ideWarning.find("macOS") != string::npos);
#endif
#ifdef _WIN32
    check("saved vs is used on Windows", vs.hasIde && vs.ide == IdeType::VisualStudio);
#else
    check("saved vs is ignored off Windows, with a warning",
          !vs.hasIde && vs.ideWarning.find("Windows") != string::npos);
#endif
    {
        // ...and the regeneration then uses the default IDE, targets kept
        PresetState foreign = parsePresetState(R"({
            "configurePresets": [{"name": "web"}],
            "vendor": {"trussc": {"ide": ")" +
            string(IdeHelper::getIdeId(ideOnThisHost(IdeType::Xcode)
                                           ? IdeType::VisualStudio : IdeType::Xcode)) +
            R"("}}})");
        ProjectSettings s;
        applyGenerationOptions(s, foreign, GenerationFlags());
        check("IDE of another OS: regenerates with the default IDE, web kept",
              s.ideType == IdeType::VSCode && s.generateWebBuild);
    }

    PresetState noBackend = parsePresetState(R"({"configurePresets": [{"name": "web"}]})");
    check("web preset without TC_WEB_BACKEND means WebGPU",
          noBackend.web && noBackend.webBackend == 0 && noBackend.warnings.empty());

    // A hand-edited file with CRLF line endings still parses
    writeFile(project / "CMakePresets.json",
              "{\r\n \"configurePresets\": [{\"name\": \"android\"}],\r\n"
              " \"vendor\": {\"trussc\": {\"ide\": \"cmake\"}}\r\n}\r\n");
    PresetState crlf = readPresetState(project.string());
    check("CRLF file read from disk",
          crlf.found && crlf.android && crlf.hasIde && crlf.ide == IdeType::CMakeOnly);
}

// -----------------------------------------------------------------------------
// 3b. TC_WEB_BACKEND, mapped the way core/CMakeLists.txt and trussc_app.cmake
//     build it: "WGPU" (or unset) is WebGPU, every other value GLES3
// -----------------------------------------------------------------------------
static PresetState parseBackend(const string& entry) {
    return parsePresetState(R"({"configurePresets": [{"name": "web", )"
                            R"("cacheVariables": {"TC_WEB_BACKEND": )" + entry + "}}]}");
}

static void testWebBackend() {
    struct Case { const char* entry; int backend; bool warned; const char* name; };
    const Case cases[] = {
        {R"("WGPU")", 0, false, "string WGPU"},
        {R"("GLES3")", 1, false, "string GLES3"},
        {R"({"type": "STRING", "value": "WGPU"})", 0, false, "object form WGPU"},
        {R"({"type": "STRING", "value": "GLES3"})", 1, false, "object form GLES3"},
        {R"({"value": "GLES3"})", 1, false, "object form without type, GLES3"},
        // CMake compares with STREQUAL "WGPU": anything else is GLES3
        {R"("gles3")", 1, true, "string gles3 (miscased)"},
        {R"("wgpu")", 1, true, "string wgpu (miscased) builds GLES3"},
        {R"("WebGL")", 1, true, "unknown string"},
        {R"("")", 1, true, "empty string"},
        {R"({"type": "STRING", "value": "WEBGL2"})", 1, true, "object form, unknown value"},
        {R"(true)", 1, true, "boolean (TRUE)"},
        // null unsets the variable: CMake's default
        {R"(null)", 0, false, "null (unset)"},
        // CMake refuses these; left out, the default stays
        {R"(3)", 0, true, "number"},
        {R"({"type": "STRING"})", 0, true, "object form without value"},
    };
    for (const Case& c : cases) {
        PresetState st = parseBackend(c.entry);
        bool warnedOk = c.warned ? st.warnings.size() == 1 : st.warnings.empty();
        check(string("TC_WEB_BACKEND ") + c.name + (c.backend ? " -> GLES3" : " -> WebGPU") +
                  (c.warned ? ", warned" : ""),
              st.found && st.web && st.webBackend == c.backend && warnedOk);
    }
    // The warning says which backend is used, and names the value
    PresetState unknown = parseBackend(R"("WebGL")");
    check("unknown TC_WEB_BACKEND warning names the value and GLES3",
          unknown.warnings.size() == 1 &&
          unknown.warnings[0].find("\"WebGL\"") != string::npos &&
          unknown.warnings[0].find("GLES3") != string::npos);
    PresetState number = parseBackend("3");
    check("unusable TC_WEB_BACKEND warning names WGPU",
          number.warnings.size() == 1 && number.warnings[0].find("WGPU") != string::npos);
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
        // Kept targets (no flag this run) are marked; a flag makes it asked for
        ProjectSettings s;
        PresetState both = saved;
        both.android = true;
        GenerationFlags f;
        f.android = true;
        applyGenerationOptions(s, both, f);
        check("web from presets is kept, android from a flag is not",
              s.webKept && !s.androidKept && !s.iosKept);
        ProjectSettings none;
        GenerationFlags webFlag;
        webFlag.web = true;
        applyGenerationOptions(none, PresetState(), webFlag);
        check("no presets: a flagged target is not kept", !none.webKept);
    }
}

// -----------------------------------------------------------------------------
// 4b. The shared setup of update / addon add / addon remove
// -----------------------------------------------------------------------------
static void testPrepareRegeneration() {
    {
        // The whole loop through the real writer: a cursor + web project,
        // then a regeneration without flags writes the same options again.
        fs::path project = makeProject("regenerate");
        ProjectSettings first = baseSettings(project);
        first.ideType = IdeType::Cursor;
        first.generateWebBuild = true;
        first.webBackend = 1;
        bool w1 = writePresets(first, project);

        RegenerationSetup again = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, GenerationFlags());
        bool w2 = writePresets(again.settings, project);
        PresetState st = readPresetState(project.string());
        check("regenerate without flags keeps cursor + web (WebGL)",
              w1 && w2 && st.hasIde && st.ide == IdeType::Cursor &&
              st.web && st.webBackend == 1);
        check("regenerate: web target marked as kept, no warnings",
              again.settings.webKept && again.warnings.empty());
        check("regenerate: summary names the kept IDE and target",
              again.summary.find("IDE cursor") != string::npos &&
              again.summary.find("web (WebGL)") != string::npos);
        check("regenerate: project name, addons and template path set",
              again.settings.projectName == "regenerate" &&
              again.settings.tcRoot == g_root.string() &&
              again.settings.templatePath ==
                  g_root.string() + "/examples/templates/emptyExample");
    }
    {
        // update --ide vscode --no-web on the same kind of project
        fs::path project = makeProject("regenerate-flags");
        ProjectSettings first = baseSettings(project);
        first.ideType = IdeType::Cursor;
        first.generateWebBuild = true;
        writePresets(first, project);
        GenerationFlags f;
        f.ide = IdeType::VSCode;
        f.web = false;
        RegenerationSetup setup = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, f);
        check("regenerate with flags: the flags win",
              setup.settings.ideType == IdeType::VSCode && !setup.settings.generateWebBuild);
    }
    {
        // No CMakePresets.json (fresh clone): defaults, no summary, no warning
        fs::path project = makeProject("regenerate-fresh");
        RegenerationSetup setup = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, GenerationFlags());
        check("regenerate without presets: defaults, no summary",
              setup.settings.ideType == IdeType::VSCode &&
              !setup.settings.generateWebBuild && setup.summary.empty() &&
              setup.warnings.empty());
    }
    {
        // Unusable saved settings reach the warnings
        fs::path project = makeProject("regenerate-warn");
        writeFile(project / "CMakePresets.json", R"({
            "configurePresets": [{"name": "web", "cacheVariables": {"TC_WEB_BACKEND": "gl"}}],
            "vendor": {"trussc": {"ide": "emacs"}}
        })");
        RegenerationSetup setup = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, GenerationFlags());
        check("regenerate: backend and IDE problems are warned",
              setup.warnings.size() == 2 && setup.settings.webBackend == 1 &&
              setup.summary.find("the IDE is the default") != string::npos);
    }
    {
        // A target dropped by a flag leaves its build folder and generated
        // build script behind: named once, nothing removed
        fs::path project = makeProject("regenerate-leftovers");
        writeFile(project / "CMakePresets.json",
                  R"({"configurePresets": [{"name": "web"}, {"name": "android"}]})");
        fs::create_directories(project / "build-web");
        fs::create_directories(project / "build-android");
        fs::create_directories(project / "xcode-ios");
        writeFile(project / "build-web.sh", "#!/bin/bash\n");
        writeFile(project / "build-web.bat", "@echo off\n");
        writeFile(project / "build-web.sh.bak", "not generated\n");
        auto setupWith = [&](const GenerationFlags& f) {
            return prepareRegeneration(project.string(), g_root.string(), {}, {}, f);
        };
        GenerationFlags noWeb;
        noWeb.web = false;
        RegenerationSetup dropped = setupWith(noWeb);
        check("--no-web: build-web/ and its scripts are named, left in place",
              leftoversOfDroppedTargets(project.string(), noWeb) ==
                  vector<string>({"build-web/", "build-web.bat", "build-web.sh"}) &&
              dropped.leftoverNotice ==
                  "build-web/, build-web.bat and build-web.sh are left in place; "
                  "'trusscli clean --all' removes them if not needed." &&
              fs::is_directory(project / "build-web") &&
              fs::is_regular_file(project / "build-web.sh"));
        GenerationFlags noAndroid;
        noAndroid.android = false;
        check("--no-android: build-android/ is named alone",
              setupWith(noAndroid).leftoverNotice ==
                  "build-android/ is left in place; "
                  "'trusscli clean --all' removes it if not needed.");
        GenerationFlags noIos;
        noIos.ios = false;
        check("--no-ios: its folder is xcode-ios/",
              setupWith(noIos).leftoverNotice.rfind("xcode-ios/ is left", 0) == 0);
        GenerationFlags web;
        web.web = true;
        check("no notice without a --no-<target> flag",
              setupWith(GenerationFlags()).leftoverNotice.empty() &&
              setupWith(web).leftoverNotice.empty());
        fs::path clean = makeProject("regenerate-no-leftovers");
        check("--no-web with nothing left behind: no notice",
              prepareRegeneration(clean.string(), g_root.string(), {}, {}, noWeb)
                  .leftoverNotice.empty());
    }
    {
        // A saved ios target: kept on macOS; elsewhere writeCMakePresets()
        // would drop it from the rewritten file, so it is dropped with a warning
        fs::path project = makeProject("regenerate-ios");
        writeFile(project / "CMakePresets.json", R"({
            "configurePresets": [{"name": "web"}, {"name": "ios"}, {"name": "ios"}],
            "vendor": {"trussc": {"ide": "cmake"}}
        })");
        RegenerationSetup setup = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, GenerationFlags());
#ifdef __APPLE__
        check("regenerate: saved ios kept on macOS, named in the summary",
              setup.settings.generateIosBuild && setup.settings.iosKept &&
              setup.warnings.empty() && setup.summary.find(", ios") != string::npos);
#else
        check("regenerate: saved ios dropped off macOS, warned once",
              !setup.settings.generateIosBuild && !setup.settings.iosKept &&
              setup.settings.generateWebBuild && setup.warnings.size() == 1 &&
              setup.warnings[0].find("ios") != string::npos &&
              setup.warnings[0].find("macOS") != string::npos);
        check("regenerate: dropped ios is not in the summary",
              !setup.summary.empty() && setup.summary.find("ios") == string::npos);
        // An explicit --ios off macOS is dropped with the same warning,
        // without a saved ios target too
        fs::path plain = makeProject("regenerate-ios-flag");
        writeFile(plain / "CMakePresets.json", R"({
            "configurePresets": [{"name": "web"}],
            "vendor": {"trussc": {"ide": "cmake"}}
        })");
        GenerationFlags f;
        f.ios = true;
        RegenerationSetup flagged = prepareRegeneration(plain.string(), g_root.string(),
                                                        {}, {}, f);
        check("regenerate: --ios off macOS dropped with the same warning",
              !flagged.settings.generateIosBuild && !flagged.settings.iosKept &&
              flagged.warnings.size() == 1 &&
              flagged.warnings[0].find("--ios") != string::npos &&
              flagged.warnings[0].find("is macOS only; dropped on this OS") !=
                  string::npos);
        check("regenerate: --ios off macOS is not in the summary",
              !flagged.summary.empty() && flagged.summary.find("ios") == string::npos);
        GenerationFlags noFlag;
        check("regenerate: no --ios, no ios warning",
              prepareRegeneration(plain.string(), g_root.string(), {}, {}, noFlag)
                  .warnings.empty());
#endif
    }
}

// -----------------------------------------------------------------------------
// 4c. Toolchain files of kept web / android presets
// -----------------------------------------------------------------------------
static void testToolchainFiles() {
    fs::path dir = makeProject("toolchains");
    fs::path savedFile = dir / "saved" / "Emscripten.cmake";
    fs::path foundFile = dir / "found" / "Emscripten.cmake";
    fs::create_directories(savedFile.parent_path());
    fs::create_directories(foundFile.parent_path());
    writeFile(savedFile, "# saved\n");
    writeFile(foundFile, "# found\n");
    const string saved = fs::absolute(savedFile).string();
    const string found = fs::absolute(foundFile).string();
    const string gone = fs::absolute(dir / "removed" / "Emscripten.cmake").string();

    check("toolchain: saved path kept when detection falls back",
          chooseToolchainFile("", saved, kEmEnvToolchain) == saved);
    check("toolchain: detection result used when it finds one",
          chooseToolchainFile(found, saved, kEmEnvToolchain) == found);
    check("toolchain: $env{} form when the saved file is gone",
          chooseToolchainFile("", gone, kEmEnvToolchain) == kEmEnvToolchain);
    check("toolchain: $env{} form when nothing was saved",
          chooseToolchainFile("", "", kEmEnvToolchain) == kEmEnvToolchain);
    check("toolchain: a saved $env{} form is not a path",
          chooseToolchainFile("", kEmEnvToolchain, kEmEnvToolchain) == kEmEnvToolchain);
    check("toolchain: a saved directory is not a toolchain file",
          chooseToolchainFile("", fs::absolute(dir).string(), kEmEnvToolchain) ==
              kEmEnvToolchain);

    // Through the real writer, detection controlled by the environment:
    // no EMSDK and no emcc in PATH is "a shell without emsdk_env".
    fs::path emptyBin = dir / "empty-bin";
    fs::create_directories(emptyBin);
    auto writeWeb = [&](const string& savedPath) {
        fs::path project = makeProject("toolchain-web");
        ProjectSettings s = baseSettings(project);
        s.generateWebBuild = true;
        s.savedWebToolchainFile = savedPath;
        writePresets(s, project);
        return presetToolchain(project, "web");
    };
    {
        EnvOverride emsdk("EMSDK", nullptr);
        EnvOverride path("PATH", emptyBin.string().c_str());
        check("web preset: saved toolchain kept in a shell without emsdk",
              writeWeb(saved) == saved);
        check("web preset: $env{EMSDK} form when the saved file is gone",
              writeWeb(gone) == kEmEnvToolchain);
    }
    {
        fs::path emsdkDir = dir / "emsdk";
        fs::path emFile = emsdkDir / "upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake";
        fs::create_directories(emFile.parent_path());
        writeFile(emFile, "# emsdk\n");
        EnvOverride emsdk("EMSDK", emsdkDir.string().c_str());
        check("web preset: the emsdk of this shell wins over the saved one",
              fs::path(writeWeb(saved)) == emFile);
    }
    {
        // Android: no ANDROID_NDK_HOME / ANDROID_HOME in this shell
        EnvOverride ndk("ANDROID_NDK_HOME", nullptr);
        EnvOverride home("ANDROID_HOME", nullptr);
        auto writeAndroid = [&](const string& savedPath) {
            fs::path project = makeProject("toolchain-android");
            ProjectSettings s = baseSettings(project);
            s.generateAndroidBuild = true;
            s.savedAndroidToolchainFile = savedPath;
            writePresets(s, project);
            return presetToolchain(project, "android");
        };
        check("android preset: saved toolchain kept in a shell without the NDK",
              writeAndroid(saved) == saved);
        check("android preset: $env{ANDROID_NDK_HOME} form when nothing is saved",
              writeAndroid("") == kNdkEnvToolchain);

        // A stale ANDROID_NDK_HOME (no toolchain file there) is not a detected
        // NDK: the saved toolchain that still exists wins, else $env{}
        fs::path staleNdk = dir / "stale-ndk";
        fs::create_directories(staleNdk);
        {
            EnvOverride stale("ANDROID_NDK_HOME", staleNdk.string().c_str());
            check("android preset: stale ANDROID_NDK_HOME loses to the saved toolchain",
                  writeAndroid(saved) == saved);
            check("android preset: stale ANDROID_NDK_HOME, nothing saved: $env{} form",
                  writeAndroid("") == kNdkEnvToolchain);
        }
        // ...while one that has the toolchain file wins over the saved one
        fs::path ndkFile = dir / "real-ndk" / "build/cmake/android.toolchain.cmake";
        fs::create_directories(ndkFile.parent_path());
        writeFile(ndkFile, "# ndk\n");
        {
            EnvOverride real("ANDROID_NDK_HOME", (dir / "real-ndk").string().c_str());
            check("android preset: the NDK of this shell wins over the saved one",
                  fs::path(writeAndroid(saved)) == fs::path(ndkFile));
        }
    }
    {
        // The saved path is read back from the presets and reaches the settings
        fs::path project = makeProject("toolchain-roundtrip");
        Json j;   // built with nlohmann: the path may need escaping (Windows)
        j["configurePresets"] = Json::array();
        j["configurePresets"].push_back({{"name", "web"}, {"toolchainFile", saved}});
        j["configurePresets"].push_back({{"name", "android"}, {"toolchainFile", 7}});
        writeFile(project / "CMakePresets.json", j.dump());
        RegenerationSetup setup = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, GenerationFlags());
        check("saved web toolchainFile reaches the settings",
              setup.settings.savedWebToolchainFile == saved);
        check("non-string android toolchainFile is ignored with a warning",
              setup.settings.savedAndroidToolchainFile.empty() &&
              setup.warnings.size() == 1 &&
              setup.warnings[0].find("android") != string::npos);
    }
}

// -----------------------------------------------------------------------------
// 4d. A kept target that fails to configure is a warning; a flagged one fails
// -----------------------------------------------------------------------------
static void testKeptTargetConfigureFailure() {
    // An "NDK" whose toolchain stops CMake at once
    fs::path ndk = g_root / "failing-ndk";
    fs::create_directories(ndk / "build/cmake");
    writeFile(ndk / "build/cmake/android.toolchain.cmake",
              "message(FATAL_ERROR \"trusscliPresets: toolchain fails on purpose\")\n");
    EnvOverride ndkEnv("ANDROID_NDK_HOME", ndk.string().c_str());

    auto run = [&](const string& name, const GenerationFlags& flags,
                   string& err, vector<string>& warnings) {
        fs::path project = makeProject(name);
        writeFile(project / "src" / "main.cpp", "int main() { return 0; }\n");
        ProjectSettings first = baseSettings(project);
        first.ideType = IdeType::CMakeOnly;   // no native configure
        first.generateAndroidBuild = true;
        writePresets(first, project);
        RegenerationSetup setup = prepareRegeneration(project.string(), g_root.string(),
                                                      {}, {}, flags);
        ProjectGenerator gen(setup.settings);
        err = gen.update(project.string());
        warnings = gen.getWarnings();
    };
    {
        string err;
        vector<string> warnings;
        run("kept-android-fails", GenerationFlags(), err, warnings);
        check("kept android target failing to configure: update succeeds",
              err.empty());
        check("kept android target failing to configure: warning names --no-android",
              warnings.size() == 1 &&
              warnings[0].find("trusscli update --no-android") != string::npos);
    }
    {
        string err;
        vector<string> warnings;
        GenerationFlags f;
        f.android = true;
        run("flagged-android-fails", f, err, warnings);
        check("--android target failing to configure: update fails",
              !err.empty() && warnings.empty());
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
    check("all preset names: six, ios among them",
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
        in.generated = true;
        in.cachedBuildType = "RelWithDebInfo";
        ConfigurePlan p = planConfigure(in);
        check("cache holds the default, no flag: no configure (steady state)",
              !p.configure && p.defines.empty() && p.messages.empty());
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.generated = true;
        in.cachedBuildType = "Release";
        in.requestedBuildType = "Release";
        ConfigurePlan p = planConfigure(in);
        check("cache holds the requested type: no configure", !p.configure);
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.generated = true;
        in.cachedBuildType = "Debug";
        ConfigurePlan p = planConfigure(in);
        check("cache Debug, no flag: switch back to RelWithDebInfo",
              p.configure && hasDefine(p, "-DCMAKE_BUILD_TYPE=RelWithDebInfo") &&
              firstMessageHas(p, "Switching build type: Debug -> RelWithDebInfo"));
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.generated = true;
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
        in.generated = true;
        in.cachedBuildType = "MinSizeRel";
        in.requestedBuildType = "Debug";
        ConfigurePlan p = planConfigure(in);
        check("web with a cache: no configure, no build-type pin", !p.configure);
    }
    {
        ConfigureInputs in;
        in.buildDir = "xcode-ios";
        ConfigurePlan p = planConfigure(in);
        check("no cache: the message names the folder it was given",
              p.configure && p.defines.empty() && firstMessageHas(p, "xcode-ios"));
    }
    {
        // A configure that failed (emsdk not active, a broken local.cmake)
        // leaves CMakeCache.txt but no Makefile / build.ninja
        ConfigureInputs in = native;
        in.hasCache = true;
        in.cachedBuildType = "RelWithDebInfo";
        ConfigurePlan p = planConfigure(in);
        check("cache without build files (failed configure): configure again",
              p.configure && p.defines.empty() && p.messages.size() == 1 &&
              firstMessageHas(p, "No build files in build-linux"));
    }
    {
        ConfigureInputs in = native;
        in.hasCache = true;
        in.cachedBuildType = "RelWithDebInfo";
        in.requestedBuildType = "Debug";
        ConfigurePlan p = planConfigure(in);
        check("failed configure, --debug: one configure with the type, one message",
              p.configure && p.defines.size() == 1 &&
              hasDefine(p, "-DCMAKE_BUILD_TYPE=Debug") && p.messages.size() == 1 &&
              firstMessageHas(p, "No build files in build-linux"));
    }
}

// #417: doctor must not mistake another object's dependencies for main's.
static void testWindowsHeaderDependencies() {
    check("deps: main has headers", hasMainHeaderDependencies(
        "CMakeFiles/app.dir/src/main.cpp.obj: #deps 133, deps mtime 42 (VALID)\r\n"
        "    C:/project/src/tcApp.h\r\n\r\n"));
    check("deps: backslash paths", hasMainHeaderDependencies(
        "CMakeFiles\\app.dir\\src\\main.cpp.obj: #deps 1, deps mtime 42 (VALID)\n"));
    check("deps: zero main deps despite another good object", !hasMainHeaderDependencies(
        "CMakeFiles/app.dir/src/tcApp.cpp.obj: #deps 133, deps mtime 42 (VALID)\n"
        "CMakeFiles/app.dir/src/main.cpp.obj: #deps 0, deps mtime 42 (VALID)\n"));
    check("deps: missing main", !hasMainHeaderDependencies(
        "CMakeFiles/app.dir/src/tcApp.cpp.obj: #deps 133, deps mtime 42 (VALID)\n"));
    check("deps: empty log", !hasMainHeaderDependencies(""));
    check("deps: malformed count", !hasMainHeaderDependencies("main.cpp.obj: #deps unknown\n"));
    check("deps: similarly named object", !hasMainHeaderDependencies(
        "not-main.cpp.obj: #deps 133, deps mtime 42 (VALID)\n"));
    check("deps: any zero main record fails", !hasMainHeaderDependencies(
        "a/main.cpp.obj: #deps 133, deps mtime 42 (VALID)\n"
        "b/main.cpp.obj: #deps 0, deps mtime 42 (VALID)\n"));

    check("Windows argv: empty", quoteWindowsArgument("") == "\"\"");
    check("Windows argv: spaces", quoteWindowsArgument("C:\\Program Files\\cmake.exe") ==
          "\"C:\\Program Files\\cmake.exe\"");
    check("Windows argv: trailing slash", quoteWindowsArgument("C:\\folder\\") ==
          "\"C:\\folder\\\\\"");
    check("Windows argv: embedded quote", quoteWindowsArgument("a\"b") == "\"a\\\"b\"");

    fs::path cache = makeProject("deps-cache") / "CMakeCache.txt";
    writeFile(cache, "// Comment\nCMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n"
                    "CMAKE_MAKE_PROGRAM:FILEPATH=C:/Program Files/Ninja/ninja.exe\n");
    check("cache: Ninja path with spaces", readCMakeCacheValue(cache.string(), "CMAKE_MAKE_PROGRAM") ==
          "C:/Program Files/Ninja/ninja.exe");
    check("cache: missing entry", readCMakeCacheValue(cache.string(), "MISSING").empty());
}

// -----------------------------------------------------------------------------
// 7b. The build folder on disk: which folder `build` looks at, and what
//     `clean` removes
// -----------------------------------------------------------------------------
static void testBuildFolders() {
    fs::path project = makeProject("build-folders");
    {
        ConfigureInputs in = inspectBuildFolder(project.string(), "linux", "linux");
        check("inspect: empty project -> build-linux, native, no cache",
              in.buildDir == "build-linux" && in.isNative && !in.hasCache &&
              !in.generated && in.cachedBuildType.empty());
    }
    {
        // A configure that failed: the cache (with a build type) and no Makefile
        fs::create_directories(project / "build-linux" / "CMakeFiles");
        writeFile(project / "build-linux" / "CMakeCache.txt",
                  "# comment\nCMAKE_BUILD_TYPE:STRING=Debug\nOTHER:BOOL=ON\n");
        ConfigureInputs in = inspectBuildFolder(project.string(), "linux", "linux");
        check("inspect: cache only -> cache, type read, not generated",
              in.hasCache && !in.generated && in.cachedBuildType == "Debug");
        writeFile(project / "build-linux" / "Makefile", "all:\n");
        in = inspectBuildFolder(project.string(), "linux", "linux");
        check("inspect: cache + Makefile -> generated", in.hasCache && in.generated);
    }
    {
        fs::create_directories(project / "build-web");
        writeFile(project / "build-web" / "CMakeCache.txt", "");
        writeFile(project / "build-web" / "build.ninja", "");
        ConfigureInputs in = inspectBuildFolder(project.string(), "web", "linux");
        check("inspect: web on linux -> build-web, not native, build.ninja counts",
              in.buildDir == "build-web" && !in.isNative && in.hasCache && in.generated);
    }
    {
        // iOS builds in xcode-ios (not build-ios); a stray build-ios is ignored
        fs::create_directories(project / "build-ios");
        writeFile(project / "build-ios" / "CMakeCache.txt", "");
        ConfigureInputs in = inspectBuildFolder(project.string(), "ios", "macos");
        bool before = !in.hasCache && in.buildDir == "xcode-ios";
        fs::create_directories(project / "xcode-ios" / "app.xcodeproj");
        writeFile(project / "xcode-ios" / "CMakeCache.txt", "");
        in = inspectBuildFolder(project.string(), "ios", "macos");
        check("inspect: ios looks in xcode-ios, the .xcodeproj counts",
              before && in.buildDir == "xcode-ios" && !in.isNative &&
              in.hasCache && in.generated);
    }

    auto has = [](const vector<string>& v, const string& x) {
        return find(v.begin(), v.end(), x) != v.end();
    };
    vector<string> all = buildFoldersToClean("macos", true);
    check("clean --all: every preset's folder, xcode-ios not build-ios, and build",
          all.size() == 7 && has(all, "xcode-ios") && !has(all, "build-ios") &&
          has(all, "build-web") && has(all, "build-android") &&
          has(all, "build-linux") && has(all, "build") && all.back() == "build");
    vector<string> native = buildFoldersToClean("linux", false);
    check("clean: the native folder and build only",
          native == vector<string>({"build-linux", "build"}));
    check("clean: no native preset -> build only",
          buildFoldersToClean("", false) == vector<string>({"build"}));
    check("clean --all: the generated build-web scripts of every OS, nothing else",
          buildScriptsToClean(true) ==
              vector<string>({"build-web.bat", "build-web.command", "build-web.sh"}));
    check("clean: no build scripts without --all", buildScriptsToClean(false).empty());
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

    // The refresh: re-pin only the windows preset's toolchain
    {
        VsVersionInfo fallback;   // what VsDetector returns when no VS is found
        fallback.version = 17;
        fallback.displayName = "Visual Studio 2022";
        VsVersionInfo vs = fallback;
        vs.installPath = "C:\\Program Files\\Microsoft Visual Studio\\2022\\Community";
        vs.ninjaPath = vs.installPath + "\\Common7\\IDE\\CommonExtensions\\Microsoft\\CMake\\Ninja\\ninja.exe";
        vs.vcToolsVersion = "14.44.35207";
        vs.windowsSdkVersion = "10.0.26100.0";
        check("repin: the no-VS fallback entry cannot pin, a real one can",
              !canPinToolchain(fallback) && canPinToolchain(vs));

        WindowsToolchainPins pins = windowsToolchainPins(vs);
        WindowsToolchainPins none = windowsToolchainPins(fallback);
        check("repin: pins use forward slashes and the detected versions",
              pins.makeProgram == string(kVs) +
                  "/Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja/ninja.exe" &&
              pins.include.find(string(kVs) + "/VC/Tools/MSVC/14.44.35207/include;") == 0 &&
              pins.lib.find("/Lib/10.0.26100.0/um/x64") != string::npos &&
              pins.path.find(";$penv{PATH}") != string::npos &&
              none.makeProgram.empty() && none.include.empty() && none.path.empty());

        // An old project: stale VS pins, plus things the refresh must keep
        Json old = Json::parse(windowsPresets("14.43.34808", "10.0.22621.0"));
        Json& win = old["configurePresets"][0];
        win["cacheVariables"]["TRUSSC_DIR"] = "D:/TrussC/core";
        win["environment"]["MY_VAR"] = "keep";
        Json web;
        web["name"] = "web";
        web["toolchainFile"] = "C:/emsdk/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake";
        old["configurePresets"].push_back(web);
        old["vendor"]["trussc"]["ide"] = "cursor";
        const string oldText = old.dump(2);
        check("repin: old presets are stale", checkPresetToolchain(oldText, installed).stale());

        string text = repinWindowsToolchain(oldText, pins);
        Json j = Json::parse(text, nullptr, false);
        bool keeps = false;
        if (j.is_object()) {
            const Json& w = j["configurePresets"][0];
            keeps = w["cacheVariables"].value("TRUSSC_DIR", "") == "D:/TrussC/core" &&
                    w["environment"].value("MY_VAR", "") == "keep" &&
                    w.value("binaryDir", "") == "${sourceDir}/build-windows" &&
                    j["configurePresets"][1] == web &&
                    j["vendor"]["trussc"].value("ide", "") == "cursor";
        }
        check("repin: fresh pins are current", !text.empty() &&
              checkPresetToolchain(text, installed).pinned &&
              !checkPresetToolchain(text, installed).stale());
        check("repin: TRUSSC_DIR, other env, web toolchainFile and IDE stay", keeps);

        // A VS without the bundled ninja: the stale ninja pin goes, env stays
        VsVersionInfo noNinja = vs;
        noNinja.ninjaPath.clear();
        Json k = Json::parse(repinWindowsToolchain(oldText, windowsToolchainPins(noNinja)),
                             nullptr, false);
        check("repin: no ninja -> CMAKE_MAKE_PROGRAM removed, environment pinned",
              k.is_object() &&
              !k["configurePresets"][0]["cacheVariables"].contains("CMAKE_MAKE_PROGRAM") &&
              k["configurePresets"][0]["environment"].contains("INCLUDE"));

        check("repin: no windows preset or broken text -> \"\"",
              repinWindowsToolchain(R"({"configurePresets": [{"name": "linux"}]})", pins).empty() &&
              repinWindowsToolchain("{ nope", pins).empty());
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
        // The writer and the refresh pin the same values
        Json j = Json::parse(readFile(project / "CMakePresets.json"), nullptr, false);
        WindowsToolchainPins pins = windowsToolchainPins(vs);
        bool same = false;
        if (j.is_object()) {
            for (const auto& p : j["configurePresets"]) {
                if (p.value("name", "") != "windows" || !p.contains("cacheVariables") ||
                    !p.contains("environment")) continue;
                same = p["cacheVariables"].value("CMAKE_MAKE_PROGRAM", "") == pins.makeProgram &&
                       p["environment"].value("INCLUDE", "") == pins.include &&
                       p["environment"].value("LIB", "") == pins.lib &&
                       p["environment"].value("PATH", "") == pins.path;
            }
        }
        check("toolchain (Windows): writer pins what windowsToolchainPins() gives", written && same);
    }
#endif
}

#ifdef _WIN32
// Keep the real batch control flow. Only replace external CMake calls; omit
// VS setup, interactive pause and installation/GUI steps in the scratch copy.
// A fake cmake.cmd would not return to its caller without `call`.
static string batchWithExits(const string& source, const vector<int>& codes, bool native) {
    istringstream in(source);
    string line, text;
    size_t call = 0;
    while (getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (native && line.find("REM Create symlink to binary") == 0) break;
        if (native && (line.find("vswhere.exe") != string::npos ||
                       line.find("if defined VS_PATH call") == 0 ||
                       line.find("pause") != string::npos)) continue;
        const size_t at = line.find_first_not_of(' ');
        if (at != string::npos && line.compare(at, 6, "cmake ") == 0) {
            if (call >= codes.size()) return {};
            const string indent = line.substr(0, at);
            text += indent + "echo CMAKE_CALL_" + to_string(call) + "\r\n";
            line = indent + "cmd /d /c exit " + to_string(codes[call++]);
        }
        text += line + "\r\n";
    }
    if (call != codes.size()) return {};
    if (native) text += "echo BUILD_SCRIPT_SUCCESS\r\nexit /b 0\r\n";
    return text;
}

static void checkBatch(const string& name, const string& source, const vector<int>& codes,
                       bool native, bool retry, bool build, const string& error) {
    const fs::path project = makeProject("batch-" + name);
    const fs::path script = project / "test.bat", log = project / "output.txt";
    const string text = batchWithExits(source, codes, native);
    check("batch " + name + ": scratch copy prepared", !text.empty());
    if (text.empty()) return;
    writeFile(script, text);
    const string command = "cmd /d /c call \"" + script.string() + "\" > \"" +
                           log.string() + "\" 2>&1";
    const int status = std::system(command.c_str());
    const string output = readFile(log);
    auto has = [&](const string& marker) { return output.find(marker) != string::npos; };
    const bool success = error.empty();
    const string done = native ? "BUILD_SCRIPT_SUCCESS" : "Build complete!";
    const bool ok = status == (success ? 0 : 1) && has(done) == success &&
                    has("CMAKE_CALL_0") && has(native ? "CMAKE_CALL_2" : "CMAKE_CALL_1") == build &&
                    (!native || (has("retrying once") == retry && has("CMAKE_CALL_1") == retry)) &&
                    (success || !native || has(error));
    check("batch " + name + ": exit and later steps", ok);
    if (!ok) std::printf("  exit: %d\n%s\n", status, output.c_str());
}

static void testWindowsBatchExits() {
    constexpr int crash = -1073741515;  // Windows DLL-not-found status
    const fs::path repo = fs::path(__FILE__).parent_path().parent_path().parent_path().parent_path().parent_path();
    const string native = readFile(repo / "tools/build_win.bat");
    check("batch: repository build_win.bat read", !native.empty());
    if (native.empty()) return;
    checkBatch("native-zero", native, {0, 0, 0}, true, false, true, "");
    checkBatch("native-negative-recovery", native, {crash, 0, 0}, true, true, true, "");
    checkBatch("native-positive-recovery", native, {1, 0, 0}, true, true, true, "");
    for (const auto& codes : vector<vector<int>>{{crash, crash, 0}, {1, crash, 0},
                                                {crash, 1, 0}, {1, 1, 0}}) {
        checkBatch("native-configure-" + to_string(codes[0]) + "-retry-" + to_string(codes[1]),
                   native, codes, true, true, false, "ERROR: CMake configuration failed!");
    }
    for (int code : {crash, 1}) {
        checkBatch("native-build-" + to_string(code), native, {0, 0, code},
                   true, false, true, "ERROR: Build failed!");
    }

    // Exercise the real writer via the update path. A kept web target may
    // fail to configure on this host without preventing script generation.
    const fs::path project = makeProject("batch-web-generated");
    ProjectSettings settings = baseSettings(project);
    settings.ideType = IdeType::CMakeOnly;
    settings.generateWebBuild = true;
    settings.webKept = true;
    ProjectGenerator generator(settings);
    const string error = generator.update(project.string());
    const string web = readFile(project / "build-web.bat");
    check("batch: generated build-web.bat read", error.empty() && !web.empty());
    if (!error.empty() || web.empty()) return;
    EnvOverride emsdk("EMSDK", nullptr);  // no external SDK setup in scratch runs
    checkBatch("web-zero", web, {0, 0}, false, false, true, "");
    for (int code : {crash, 1}) {
        checkBatch("web-configure-" + to_string(code), web, {code, 0},
                   false, false, false, "configure failed");
        checkBatch("web-build-" + to_string(code), web, {0, code},
                   false, false, true, "build failed");
    }
}
#endif

} // namespace

TC_CORE_TEST_MAIN() {
    g_root = fs::temp_directory_path() /
             ("trusscliPresets-" + to_string(chrono::steady_clock::now()
                                                .time_since_epoch().count()));
    fs::create_directories(g_root);

    testIdeRoundTrip();
    testTargetRoundTrip();
    testParseEdgeCases();
    testWebBackend();
    testPrecedence();
    testPrepareRegeneration();
    testToolchainFiles();
    testKeptTargetConfigureFailure();
    testTargetFlags();
    testBuildDirMapping();
    testConfigurePlan();
    testWindowsHeaderDependencies();
    testBuildFolders();
    testToolchainCheck();
#ifdef _WIN32
    testWindowsBatchExits();
#endif

    std::error_code ec;
    fs::remove_all(g_root, ec);

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
