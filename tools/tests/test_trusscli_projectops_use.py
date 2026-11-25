"""Direct selection operations, using the production driver and offline sources."""

import json
import subprocess
import sys
import unittest

import test_trusscli_gui_update as gui
import test_trusscli_versions as versions


class UseOperationTests(unittest.TestCase):
    setUpClass = classmethod(gui.GuiUpdateTests.setUpClass.__func__)
    setUp = versions.VersionTests.setUp
    git = versions.VersionTests.git
    advance = versions.VersionTests.advance

    def operate(self, mode, selection, success=True):
        completed = subprocess.run(
            [str(self.driver), str(self.source), str(self.project), mode, str(selection)],
            env=self.env, capture_output=True, text=True, encoding="utf-8", check=False)
        self.assertEqual(completed.stderr, "")
        self.assertEqual(completed.returncode, 0 if success else 1, completed.stdout)
        report = json.loads(completed.stdout)
        self.assertEqual(report["success"], success)
        self.assertEqual(report["logs"], [event["message"] for event in report["events"]
                                         if not event["warning"]])
        return report

    def snapshot(self):
        return {str(path.relative_to(self.project)): path.read_bytes()
                for path in self.project.rglob("*") if path.is_file()}

    def test_tag_and_short_commit_acquire_and_regenerate(self):
        for ref, pin in (("v1", "v1"), (self.initial[:9], self.initial)):
            with self.subTest(ref=ref):
                report = self.operate("use", ref)
                self.assertEqual((self.project / ".trussc-version").read_text(encoding="utf-8"), pin + "\n")
                self.assertEqual(report["logs"][-1], "Project updated: " + str(self.project))
                presets = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
                self.assertNotIn("localPath", presets["vendor"]["trussc"])

    def test_local_selection_and_clear_preserve_pin(self):
        (self.project / ".trussc-version").write_text("v1\n", encoding="utf-8")
        self.operate("use-local", self.source)
        presets = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
        self.assertEqual(presets["vendor"]["trussc"]["localPath"], self.source.as_posix())
        self.operate("use-local", "--clear")
        presets = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
        self.assertNotIn("localPath", presets["vendor"]["trussc"])
        self.assertEqual((self.project / ".trussc-version").read_text(encoding="utf-8"), "v1\n")

    def test_clear_version_removes_both_selections(self):
        self.operate("use", "v1")
        self.operate("use-local", self.source)
        self.operate("use", "--clear")
        self.assertFalse((self.project / ".trussc-version").exists())
        presets = json.loads((self.project / "CMakePresets.json").read_text(encoding="utf-8"))
        self.assertNotIn("localPath", presets["vendor"]["trussc"])

    def test_missing_local_path_preserves_files(self):
        before = self.snapshot()
        report = self.operate("use-local", self.base / "missing", success=False)
        self.assertIn("local TrussC path does not exist:", report["error"])
        self.assertEqual(self.snapshot(), before)

    @unittest.skipIf(sys.platform == "win32", "CreateProcess also searches system directories for curl")
    def test_spawn_failure_is_captured_without_console_output(self):
        before = self.snapshot()
        empty_path = self.base / "empty-path"
        empty_path.mkdir()
        self.env.update(PATH=str(empty_path), TRUSSC_FETCH="tarball")
        report = self.operate("use", "v1", success=False)
        diagnostic = "Error: failed to spawn 'curl'"
        self.assertTrue(any(diagnostic in line for line in report["logs"]))
        self.assertIn(diagnostic, report["error"])
        self.assertEqual(self.snapshot(), before)


if __name__ == "__main__":
    unittest.main()
