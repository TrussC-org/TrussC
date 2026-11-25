"""ProjectGenerator GUI updates, using only local project/addon fixtures.

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
            'namespace tc { using Json = nlohmann::json; }\n', encoding="utf-8")
        source = scratch / "main.cpp"
        source.write_text(r'''
#include "ProjectGenerator.h"
#include "GeneratedFile.h"
#include <filesystem>
#include <fstream>
#include <iostream>

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
        else settings.addonSelected = {0, 0, 0, 0};
    }
    ProjectGenerator generator(settings);
    std::string error = std::string(argv[3]) == "gui"
        ? generator.updateFromGui(argv[2]) : generator.update(argv[2]);
    if (!error.empty()) { std::cout << error; return 1; }
}
''', encoding="utf-8")
        cls.driver = scratch / "driver"
        subprocess.run([
            os.environ.get("CXX", "c++"), "-std=c++17", "-Wall", "-Wextra", "-Werror",
            "-I", str(scratch), "-I", str(ROOT / "tools/src"),
            "-I", str(ROOT / "core/include"), str(source),
            str(ROOT / "tools/src/ProjectGenerator.cpp"),
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

    def test_plain_installed_dependencies_update_including_cycles(self):
        (self.framework / "addons/tcxClone/.git").mkdir()
        self.manifest("tcxClone", ["tcxNew"])
        self.manifest("tcxNew", [{"name": "tcxKeep"}, "tcxBridge"])
        self.manifest("tcxBridge", ["tcxNew"])
        result = self.update(selection="clone")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual((self.project / "addons.make").read_text(encoding="utf-8"),
                         "tcxKeep\ntcxNew\ntcxClone\n")

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
                self.assertNotIn("addonPaths", data["vendor"]["trussc"])
                self.assertNotIn("addonOrder", data["vendor"]["trussc"])
                self.assertEqual(data["vendor"]["trussc"]["custom"], "preserve")
                script = self.base / "effective-addons.cmake"
                lines = [f'set(CMAKE_CURRENT_SOURCE_DIR "{self.project.as_posix()}")',
                         f'include("{(ROOT / "core/cmake/use_addon.cmake").as_posix()}")']
                for preset in data["configurePresets"]:
                    cache = preset.get("cacheVariables", {})
                    for key in ("TRUSSC_ADDON_PATHS", "TRUSSC_ADDON_ORDER"):
                        self.assertNotIn(key, cache)
                        lines.append(f'set({key} [==[{cache.get(key, "")}]==])')
                    lines.extend(['_tc_project_addons(actual)',
                                  f'if(NOT "${{actual}}" STREQUAL "{expected}")',
                                  'message(FATAL_ERROR "Effective addons: ${actual}")', 'endif()'])
                script.write_text("\n".join(lines) + "\n", encoding="utf-8")
                subprocess.run(["cmake", "-P", str(script)], check=True, capture_output=True)

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
