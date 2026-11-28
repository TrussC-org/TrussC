"""Real read-only inspection and project creation, with offline temp fixtures.

Run through tcbuild.sh; only framework file output is shimmed.
"""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class HubOperations(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix="gui2-driver-")
        cls.addClassCleanup(cls.temp.cleanup)
        scratch = Path(cls.temp.name)
        (scratch / "TrussC.h").write_text(
            '#pragma once\n#include <nlohmann/json.hpp>\n'
            '#include "tc/utils/tcFileIO.h"\n'
            'namespace trussc { using Json = nlohmann::json; }\n'
            'namespace tc = trussc;\n'
            '#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 13\n'
            '#pragma GCC diagnostic ignored "-Wdangling-reference"\n'
            '#endif\n', encoding="utf-8")
        source = scratch / "main.cpp"
        source.write_text(r'''
#include "TrussC.h"
#include "ProjectHub.h"
#include "GeneratedFile.h"
#include <fstream>
#include <iostream>
void writeGeneratedFile(const std::filesystem::path& path, const std::string& text) {
    std::ofstream(path, std::ios::binary) << text;
}
void removeBuildDirectory(const std::filesystem::path& path) { std::filesystem::remove_all(path); }
int main(int argc, char** argv) {
    if (argc != 4) return 2;
    const std::string mode = argv[1];
    if (mode == "create") {
        ProjectSettings settings;
        settings.tcRoot = argv[2];
        settings.ideType = IdeType::CMakeOnly;
        auto result = projectHub::create(argv[3], settings);
        std::cout << tc::Json{{"success", result.success}, {"error", result.failureReason}};
    } else if (mode == "documents") {
        std::cout << tc::Json{{"path", tc::pathToUtf8(projectHub::documentsDirectory())},
            {"checkout", projectHub::defaultIsFramework(argv[3])}};
    } else {
        auto result = projectHub::inspect(argv[3], argv[2]);
        std::cout << tc::Json{{"error", result.error}, {"path", result.path}, {"name", result.name},
            {"root", result.root}, {"pin", result.pin}, {"local", result.localRoot},
            {"missing", result.missing}, {"addons", result.addons}, {"diagnostics", result.diagnostics}};
    }
}
''', encoding="utf-8")
        cls.driver = scratch / "driver"
        sources = ["ProjectGenerator", "ProjectOps", "ProjectState", "IdeHelper",
                   "VsDetector", "AddonVersions", "TrussCVersion", "ProcessCapture"]
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-pthread",
                        "-Wall", "-Wextra", "-Werror", "-include", str(scratch / "TrussC.h"),
                        "-I", str(scratch), "-I", str(ROOT / "core/include"),
                        "-I", str(ROOT / "tools/src"), "-I", str(ROOT / "tools/gui2/src"),
                        str(source), str(ROOT / "tools/gui2/src/ProjectHub.cpp"),
                        *[str(ROOT / "tools/src" / (name + ".cpp")) for name in sources],
                        "-o", str(cls.driver)], check=True)

    def setUp(self):
        temp = tempfile.TemporaryDirectory(prefix="gui2-project-")
        self.addCleanup(temp.cleanup)
        self.base = Path(temp.name)
        self.framework = self.base / "framework"
        (self.framework / "core/cmake").mkdir(parents=True)
        (self.framework / "core/cmake/trussc_app.cmake").touch()
        template = self.framework / "examples/templates/emptyExample"
        (template / "src").mkdir(parents=True)
        (template / "src/tcApp.cpp").write_text("// empty template\n", encoding="utf-8")
        (template / "CMakeLists.txt").write_text("# TRUSSC_DIR\n", encoding="utf-8")
        self.project = self.base / "光の実験"
        self.env = dict(os.environ, HOME=str(self.base), USERPROFILE=str(self.base),
                        XDG_CONFIG_HOME=str(self.base / ".config"))

    def call(self, mode, path=None, fallback=None):
        result = subprocess.run([str(self.driver), mode, str(fallback or self.framework), str(path or self.project)],
                                env=self.env, check=True, capture_output=True, text=True, encoding="utf-8")
        return json.loads(result.stdout)

    def snapshot(self):
        return {str(p.relative_to(self.base)): p.read_bytes()
                for p in self.base.rglob("*") if p.is_file()}

    def test_create_unicode_and_inspect_without_writes(self):
        self.assertTrue(self.call("create")["success"])
        self.assertTrue((self.project / "src/tcApp.cpp").exists())
        self.assertFalse((self.base / "____").exists())
        before = self.snapshot()
        selected = self.call("inspect")
        self.assertEqual(selected["error"], "")
        self.assertEqual(selected["name"], "光の実験")
        self.assertEqual(selected["path"], str(self.project))
        self.assertEqual(selected["root"], str(self.framework))
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(self.call("create")["success"])
        self.assertEqual(self.snapshot(), before)

    def test_inspect_uses_generated_preset_root_before_gui_fallback(self):
        self.assertTrue(self.call("create")["success"])
        other = self.base / "other-framework"
        (other / "core/cmake").mkdir(parents=True)
        (other / "core/cmake/trussc_app.cmake").touch()
        before = self.snapshot()
        selected = self.call("inspect", fallback=other)
        self.assertEqual(selected["error"], "")
        self.assertEqual(selected["pin"], "")
        self.assertEqual(selected["local"], "")
        self.assertEqual(selected["root"], str(self.framework))
        self.assertEqual(self.snapshot(), before)

    def test_inspect_keeps_addon_specs_and_reports_uncached_sources(self):
        self.assertTrue(self.call("create")["success"])
        (self.project / "addons.make").write_text("# selected\nowner/tcxDemo@v1\n", encoding="utf-8")
        before = self.snapshot()
        selected = self.call("inspect")
        self.assertEqual(selected["addons"], ["owner/tcxDemo@v1"])
        self.assertTrue(selected["missing"] or selected["diagnostics"])
        self.assertEqual(self.snapshot(), before)

    def test_reject_nonproject_and_unrelated_cmake(self):
        self.project.mkdir()
        self.assertTrue(self.call("inspect")["error"])
        (self.project / "src").mkdir()
        (self.project / "CMakeLists.txt").write_text("project(unrelated)\n", encoding="utf-8")
        self.assertIn("Refusing", self.call("inspect")["error"])

    def test_missing_pin_and_broken_local_are_disclosed_without_fallback(self):
        self.assertTrue(self.call("create")["success"])
        (self.project / ".trussc-version").write_text("v99.99.99\n", encoding="utf-8")
        before = self.snapshot()
        selected = self.call("inspect")
        self.assertEqual(selected["pin"], "v99.99.99")
        self.assertTrue(selected["missing"])
        self.assertNotEqual(selected["root"], str(self.framework))
        self.assertEqual(self.snapshot(), before)
        presets = self.project / "CMakePresets.json"
        data = json.loads(presets.read_text(encoding="utf-8"))
        data["vendor"]["trussc"]["localPath"] = str(self.base / "absent")
        presets.write_text(json.dumps(data), encoding="utf-8")
        before = self.snapshot()
        selected = self.call("inspect")
        self.assertEqual(selected["root"], "")
        self.assertTrue(selected["diagnostics"])
        self.assertEqual(self.snapshot(), before)

    @unittest.skipUnless(os.name == "posix" and os.uname().sysname == "Linux", "Linux XDG resolver")
    def test_documents_xdg_and_framework_notice(self):
        config = self.base / ".config"
        config.mkdir()
        (config / "user-dirs.dirs").write_text('XDG_DOCUMENTS_DIR="$HOME/書類"\n', encoding="utf-8")
        self.assertEqual(self.call("documents")["path"], str(self.base / "書類"))
        (self.framework / "trusscli").touch()
        self.assertTrue(self.call("documents", self.framework)["checkout"])


if __name__ == "__main__":
    unittest.main()
