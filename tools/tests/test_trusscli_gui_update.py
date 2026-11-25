"""Shared project-operation GUI updates, using only local project/addon fixtures.

Run through tcbuild.sh. The production generator and IDE helper are compiled
with a Json-only TrussC header and a file I/O shim; no display is needed.
"""

import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
REFUSAL = (
    "This project uses a pinned TrussC version or external addons. Use "
    "`trusscli update -p <path>` (GUI support is planned for v0.9.1)."
)


class GuiUpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="gui-update-driver-")
        cls.addClassCleanup(cls.build.cleanup)
        scratch = Path(cls.build.name)
        (scratch / "TrussC.h").write_text(
            '#pragma once\n#include <nlohmann/json.hpp>\n'
            # GCC 13+ misdiagnoses ProjectState's reference to a static vector.
            '#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 13\n'
            '#pragma GCC diagnostic ignored "-Wdangling-reference"\n'
            '#endif\n'
            'namespace tc { using Json = nlohmann::json; }\n', encoding="utf-8")
        source = scratch / "main.cpp"
        source.write_text(r'''
#include "ProjectGenerator.h"
#include "ProjectOps.h"
#include "GeneratedFile.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include "TrussC.h"

// Replace framework file I/O only. All update decisions and file contents
// come from the production ProjectGenerator.
void writeGeneratedFile(const fs::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
void removeBuildDirectory(const fs::path& path) { fs::remove_all(path); }

int main(int argc, char** argv) {
    if (argc != 4 && argc != 5) return 2;
    ProjectSettings settings;
    settings.tcRoot = argv[1];
    settings.projectName = "sample";
    settings.ideType = IdeType::CMakeOnly;
    settings.addons = {"tcxKeep", "tcxRemove", "tcxNew", "tcxClone"};
    settings.addonSelected = {1, 0, 1, 0};
    if (argc == 5) {
        if (std::string(argv[4]) == "clone") settings.addonSelected[3] = 1;
        else if (std::string(argv[4]) == "keep") settings.addonSelected = {1, 0, 0, 0};
        else settings.addonSelected = {0, 0, 0, 0};
    }
    if (std::string(argv[3]) == "operation") {
        projectOps::UpdateInput input;
        input.projectPath = argv[2];
        input.tcRoot = argv[1];
        input.flags.ide = IdeType::CMakeOnly;
        input.fetchVersion = false;
        input.addonLookup = [](const addonVersions::Spec&, bool) {
            projectOps::diagnostic("registry callback fixture", projectOps::MessageKind::Diagnostic);
            return addonVersions::Source{};
        };
        if (argc == 5) input.requirePlainProject = true;
        tc::Json events = tc::Json::array();
        std::ostringstream printed;
        auto* previousOut = std::cout.rdbuf(printed.rdbuf());
        auto* previousErr = std::cerr.rdbuf(printed.rdbuf());
        auto result = projectOps::update(input, [&](const std::string& text, projectOps::MessageKind kind) {
            events.push_back({{"message", text}, {"warning", kind == projectOps::MessageKind::Warning}});
        });
        std::cout.rdbuf(previousOut);
        std::cerr.rdbuf(previousErr);
        if (!printed.str().empty()) return 4;
        std::cout << tc::Json{{"success", result.success}, {"error", result.failureReason},
            {"warnings", result.warnings}, {"logs", result.logLines}, {"events", events}};
        return result.success ? 0 : 1;
    }
    ProjectGenerator generator(settings);
    std::string error;
    if (std::string(argv[3]) == "gui") {
        projectOps::UpdateInput input;
        input.projectPath = argv[2];
        input.settings = settings;
        input.requirePlainProject = true;
        auto result = projectOps::update(input);
        error = result.failureReason;
        if (result.success != error.empty()) return 3;
    } else error = generator.update(argv[2]);
    if (!error.empty()) { std::cout << error; return 1; }
}
''', encoding="utf-8")
        cls.driver = scratch / "driver"
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-include", str(scratch / "TrussC.h"),
            "-I", str(scratch), "-I", str(ROOT / "tools/src"),
            "-I", str(ROOT / "core/include"), str(source),
            str(ROOT / "tools/src/ProjectGenerator.cpp"),
            str(ROOT / "tools/src/ProjectOps.cpp"),
            str(ROOT / "tools/src/ProjectState.cpp"),
            str(ROOT / "tools/src/IdeHelper.cpp"),
            str(ROOT / "tools/src/VsDetector.cpp"), "-o", str(cls.driver),
            str(ROOT / "tools/src/AddonVersions.cpp"),
            str(ROOT / "tools/src/TrussCVersion.cpp"),
            str(ROOT / "tools/src/ProcessCapture.cpp"),
        ], check=True)

    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix="gui-update-project-")
        self.addCleanup(scratch.cleanup)
        self.base = Path(scratch.name)
        self.framework = self.base / "framework"
        for addon in ("tcxKeep", "tcxRemove", "tcxNew", "tcxClone"):
            (self.framework / "addons" / addon).mkdir(parents=True)
        cmake = self.framework / "core/cmake"
        cmake.mkdir(parents=True)
        (cmake / "trussc_app.cmake").touch()
        self.project = self.base / "sample"
        (self.project / "src").mkdir(parents=True)
        (self.project / "CMakeLists.txt").write_text("# TRUSSC_DIR\n", encoding="utf-8")
        (self.project / "addons.make").write_text("tcxKeep\ntcxRemove\n", encoding="utf-8")
        (self.project / "build-linux").mkdir()
        (self.project / "build-linux/canary").write_text("keep", encoding="utf-8")

    def update(self, mode="gui", selection=None):
        command = [str(self.driver), str(self.framework), str(self.project), mode]
        if selection:
            command.append(selection)
        return subprocess.run(command,
                              text=True, capture_output=True, encoding="utf-8")

    def snapshot(self):
        return {p.relative_to(self.project).as_posix(): p.read_bytes()
                for p in self.project.rglob("*") if p.is_file()}

    def assert_refused(self, selection=None):
        before = self.snapshot()
        result = self.update(selection=selection)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(result.stdout, REFUSAL)
        self.assertEqual(self.snapshot(), before, "refusal must precede every write/cleanup")

    def test_unknown_lines_keep_order_and_position_while_selection_changes(self):
        addons = self.project / "addons.make"
        addons.write_text("# custom header\nowner/tcxFirst@v1\ntcxKeep\n\n"
                          "tcxOldRegistry\ntcxRemove\n  ../local/tcxOverride  \n"
                          "# trailing comment\ntcxLast@branch", encoding="utf-8")
        result = self.update("generator")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        expected = ("# custom header\nowner/tcxFirst@v1\ntcxKeep\n\n"
                    "tcxOldRegistry\n  ../local/tcxOverride  \n"
                    "# trailing comment\ntcxLast@branch\ntcxNew\n")
        self.assertEqual(addons.read_text(encoding="utf-8"), expected)
        self.assertEqual(self.update("generator").returncode, 0)
        self.assertEqual(addons.read_text(encoding="utf-8"), expected)

    def test_plain_gui_project_updates_and_changes_only_local_selection(self):
        result = self.update()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.project / "addons.make").read_text(encoding="utf-8"),
                         "tcxKeep\ntcxNew\n")
        self.assertTrue((self.project / "CMakePresets.json").is_file())
        self.assertIn("trussc_app(", (self.project / "CMakeLists.txt").read_text(encoding="utf-8"))

    def test_direct_operation_restores_addons_and_presets(self):
        addons = self.project / "addons.make"
        original = "# keep comments\ntcxKeep\ntcxRemove\n"
        addons.write_text(original, encoding="utf-8")
        self.manifest("tcxKeep", ["tcxNew"])
        presets = self.project / "CMakePresets.json"
        presets.write_text(json.dumps({"vendor": {"trussc": {"custom": "keep"}}}),
                           encoding="utf-8")
        result = self.update("operation")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertTrue(report["success"])
        self.assertEqual(report["error"], "")
        self.assertEqual(addons.read_text(encoding="utf-8"), original)
        self.assert_effective_addons("tcxKeep;tcxRemove;tcxNew")
        vendor = json.loads(presets.read_text(encoding="utf-8"))["vendor"]["trussc"]
        self.assertEqual(vendor["custom"], "keep")
        self.assertEqual(vendor["ide"], "cmake")
        self.assertEqual(report["logs"], [event["message"] for event in report["events"]
                                          if not event["warning"]])
        self.assertEqual(report["warnings"], [event["message"] for event in report["events"]
                                              if event["warning"]])
        self.assertEqual(report["logs"][-1], "Project updated: " + str(self.project))

    def test_direct_operation_plain_requirement_refuses_before_local_override(self):
        (self.project / ".trussc-version").touch()
        before = self.snapshot()
        result = self.update("operation", "plain")
        self.assertEqual(result.returncode, 1, result.stderr)
        report = json.loads(result.stdout)
        self.assertFalse(report["success"])
        self.assertEqual(report["error"], REFUSAL)
        self.assertEqual(report["logs"], [])
        self.assertEqual(self.snapshot(), before)

    def test_direct_operation_returns_preset_warnings(self):
        (self.project / "CMakePresets.json").write_text(
            json.dumps({"configurePresets": 17}), encoding="utf-8")
        result = self.update("operation")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertTrue(report["warnings"])
        self.assertEqual(report["events"][0],
                         {"message": report["warnings"][0], "warning": True})

    def test_direct_operation_captures_resolver_warnings(self):
        (self.project / ".trussc-version").write_text("v1.0.0\n", encoding="utf-8")
        (self.framework / "addons/tcxKeep/addon.json").write_text(
            json.dumps({"trussc_version": "unsupported"}), encoding="utf-8")
        result = self.update("operation")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        expected = "tcxKeep has an unsupported trussc_version range 'unsupported'"
        self.assertIn(expected, report["warnings"])
        self.assertEqual(report["events"][0], {"message": expected, "warning": True})

    def test_direct_operation_captures_registry_diagnostics(self):
        (self.framework / "addons/tcxClone/.git").mkdir()
        (self.project / "addons.make").write_text("tcxClone\n", encoding="utf-8")
        result = self.update("operation")
        self.assertEqual(result.returncode, 0, result.stderr + result.stdout)
        report = json.loads(result.stdout)
        self.assertEqual(report["logs"][0], "registry callback fixture")
        self.assertEqual(report["events"][0],
                         {"message": "registry callback fixture", "warning": False})

    def test_version_file_refuses_even_when_empty(self):
        (self.project / ".trussc-version").touch()
        self.assert_refused()

    def test_use_local_refuses_in_both_preset_locations(self):
        for filename in ("CMakePresets.json", "CMakeUserPresets.json"):
            with self.subTest(filename=filename):
                path = self.project / filename
                path.write_text(json.dumps({"vendor": {"trussc": {"localPath": "../other"}}}),
                                encoding="utf-8")
                self.assert_refused()
                path.unlink()

    def test_external_lines_refuse_including_legacy_plain_names(self):
        # A legacy registry/addon-clone folder outside tcRoot/addons is not
        # visible in the GUI, even when addons.make uses an unqualified name.
        (self.base / "tcxOldRegistry").mkdir()
        for line in ("tcxKeep@v1", "owner/tcxKeep@branch", "owner/tcxKeep",
                     "tcxOldRegistry", "../tcxOldRegistry", "tcxKeep = ../override"):
            with self.subTest(line=line):
                (self.project / "addons.make").write_text(line + "\n", encoding="utf-8")
                self.assert_refused()

    def test_local_addon_override_refuses_even_for_a_bundled_name(self):
        (self.project / "CMakePresets.json").write_text(json.dumps({
            "vendor": {"trussc": {"addonLocalPaths": {"tcxKeep": "../override"}}}
        }), encoding="utf-8")
        self.assert_refused()

    def test_addon_clone_inside_framework_updates_like_a_local_addon(self):
        # CLI update also uses the installed folder for a plain name, so the GUI keeps handling it.
        (self.framework / "addons/tcxClone/.git").mkdir()
        (self.project / "addons.make").write_text("tcxClone\n", encoding="utf-8")
        result = self.update(selection="clone")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.project / "addons.make").read_text(encoding="utf-8"),
                         "tcxClone\ntcxKeep\ntcxNew\n")

    def manifest(self, name, dependencies):
        path = self.framework / "addons" / name
        path.mkdir(parents=True, exist_ok=True)
        (path / "addon.json").write_text(json.dumps({"dependencies": dependencies}),
                                        encoding="utf-8")

    def test_selected_clone_and_bundled_addons_refuse_external_dependencies(self):
        (self.framework / "addons/tcxClone/.git").mkdir()
        for name in ("tcxClone", "tcxKeep", "tcxNew"):
            with self.subTest(name=name):
                # tcxKeep stays from addons.make; tcxClone/tcxNew are newly checked.
                self.manifest(name, ["alice/tcxExternal@v1"])
                self.assert_refused(selection="clone")
                (self.framework / "addons" / name / "addon.json").unlink()

    def test_transitive_external_dependencies_refuse_including_object_specs(self):
        self.manifest("tcxNew", ["tcxBridge"])
        for dependency in ("alice/tcxExternal@v1", "alice/tcxKeep", "tcxKeep@v1",
                           "tcxMissing", {"name": "tcxKeep", "tag": "v1"},
                           {"name": "tcxKeep", "commit": "abc123"},
                           {"name": "tcxKeep", "branch": "main"},
                           {"name": "tcxKeep", "url": "https://github.com/alice/tcxKeep.git"}):
            with self.subTest(dependency=dependency):
                self.manifest("tcxBridge", [dependency])
                self.assert_refused()

    def test_plain_installed_dependencies_stay_effective_until_root_is_unchecked(self):
        (self.project / "addons.make").write_text("tcxKeep\n", encoding="utf-8")
        self.manifest("tcxKeep", ["tcxBridge"])
        # Installed clones also resolve locally, without a registry or fetch.
        (self.framework / "addons/tcxBridge/.git").mkdir(parents=True)
        for selection, expected in (("keep", "tcxKeep;tcxBridge"), ("empty", "")):
            with self.subTest(selection=selection):
                result = self.update(selection=selection)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual((self.project / "addons.make").read_text(encoding="utf-8"),
                                 "tcxKeep\n" if selection == "keep" else "")
                self.assert_effective_addons(expected)

    def test_unchecked_addon_dependencies_do_not_block_removal(self):
        self.manifest("tcxRemove", ["alice/tcxExternal@v1"])
        result = self.update()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_cli_resolver_map_cannot_keep_unchecked_addons_enabled(self):
        for selection, expected in ((None, "tcxKeep;tcxNew"), ("empty", "")):
            with self.subTest(selection=selection):
                vendor = {"addonPaths": {name: str(self.framework / "addons" / name)
                                         for name in ("tcxKeep", "tcxRemove")},
                          "addonOrder": "tcxKeep;tcxRemove", "custom": "preserve"}
                presets = self.project / "CMakePresets.json"
                presets.write_text(json.dumps({"vendor": {"trussc": vendor}}), encoding="utf-8")
                result = self.update(selection=selection)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                data = json.loads(presets.read_text(encoding="utf-8"))
                self.assertEqual(data["vendor"]["trussc"]["custom"], "preserve")
                self.assert_effective_addons(expected)

    def assert_effective_addons(self, expected):
        data = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
        vendor = data["vendor"]["trussc"]
        names = expected.split(";") if expected else []
        self.assertEqual(vendor["addonPaths"],
                         {name: str(self.framework / "addons" / name) for name in names})
        self.assertEqual(vendor["addonOrder"], expected)
        script = self.base / "effective-addons.cmake"
        lines = [f'set(CMAKE_CURRENT_SOURCE_DIR "{self.project.as_posix()}")',
                 f'include("{(ROOT / "core/cmake/use_addon.cmake").as_posix()}")']
        self.assertTrue(data["configurePresets"])
        for preset in data["configurePresets"]:
            cache = preset["cacheVariables"]
            if names:
                self.assertEqual(json.loads(cache["TRUSSC_ADDON_PATHS"]), vendor["addonPaths"])
                self.assertEqual(cache["TRUSSC_ADDON_ORDER"], expected)
                for key in ("TRUSSC_ADDON_PATHS", "TRUSSC_ADDON_ORDER"):
                    lines.append(f'set({key} [==[{cache[key]}]==])')
            else:
                # An empty map is not passed to CMake; addons.make alone decides.
                self.assertNotIn("TRUSSC_ADDON_PATHS", cache)
                self.assertNotIn("TRUSSC_ADDON_ORDER", cache)
            lines.extend(['_tc_project_addons(actual)',
                          f'if(NOT "${{actual}}" STREQUAL "{expected}")',
                          'message(FATAL_ERROR "Effective addons: ${actual}")', 'endif()'])
        script.write_text("\n".join(lines) + "\n", encoding="utf-8")
        result = subprocess.run(["cmake", "-P", str(script)], text=True, capture_output=True,
                                encoding="utf-8")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_plain_operation_without_settings_bounds_addons_make(self):
        path = self.project / "addons.make"
        with path.open("wb") as output:
            output.truncate(16 * 1024 * 1024 + 1)
        before = self.snapshot()
        result = self.update("operation", "plain")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertEqual(json.loads(result.stdout)["error"],
                         "addons.make exceeds the 16777216-byte limit")
        self.assertEqual(self.snapshot(), before)

    def test_oversized_project_and_addon_files_fail_before_writes(self):
        for relative, mode in (("addons.make", "gui"), ("addons.make", "generator"),
                               ("CMakePresets.json", "gui"), ("CMakeUserPresets.json", "gui"),
                               ("../framework/addons/tcxNew/addon.json", "gui")):
            with self.subTest(relative=relative, mode=mode):
                path = self.project / relative
                original = path.read_bytes() if path.exists() else None
                with path.open("wb") as output:
                    output.truncate(16 * 1024 * 1024 + 1)
                before = self.snapshot()
                result = self.update(mode)
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertEqual(result.stdout, path.name + " exceeds the 16777216-byte limit")
                self.assertEqual(self.snapshot(), before)
                if original is None:
                    path.unlink()
                else:
                    path.write_bytes(original)


if __name__ == "__main__":
    unittest.main()
