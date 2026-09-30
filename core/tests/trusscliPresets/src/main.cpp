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
//     an unparsable file, wrongly typed entries, an unknown IDE id, and an IDE
//     this OS cannot generate (xcode off macOS, vs off Windows).
// Not covered: the commands themselves (tools/src/main.cpp), which call these
// functions; the IDE files and CMake configure that `update` runs.
// =============================================================================

#include <TrussC.h>

#include "ProjectGenerator.h"
#include "ProjectState.h"

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

    std::error_code ec;
    fs::remove_all(g_root, ec);

    std::printf("\n%s (%d failure%s)\n", g_fail == 0 ? "ALL PASS" : "FAILED",
                g_fail, g_fail == 1 ? "" : "s");
    return g_fail == 0 ? 0 : 1;
}
