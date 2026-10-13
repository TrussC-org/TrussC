"""Headless LF/atomic-write regressions for #761 PR-D; set TRUSSCLI to test."""

import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()


class WriteTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="trusscli-writes-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.project = self.base / "sample"
        (self.project / "src").mkdir(parents=True)
        (self.project / "CMakeLists.txt").write_bytes(b"# TRUSSC_DIR\r\n")
        (self.project / "addons.make").write_bytes(
            b"# TrussC addons - one addon per line\r\n")

    def cli(self, *args, success=True, env=None):
        result = subprocess.run(
            [str(CLI), *map(str, args)], cwd=self.project,
            env=dict(os.environ, TRUSSC_DIR=str(ROOT), **(env or {})),
            capture_output=True, text=True, check=False,
        )
        if success:
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        else:
            self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stderr.count("Error:"), 1, result.stderr)
        return result

    def snapshot(self, project=None):
        project = project or self.project
        return {str(p.relative_to(project)): p.read_bytes()
                for p in project.rglob("*") if p.is_file()}

    def assert_lf(self, files):
        for name, data in files.items():
            self.assertNotIn(b"\r", data, name)
            self.assertFalse(name.endswith(".tmp"), name)

    def test_update_add_remove_are_lf_and_repeatable(self):
        self.cli("update", "--ide", "cmake")
        before = self.snapshot()
        self.assert_lf(before)
        self.cli("update")
        self.assertEqual(before, self.snapshot())
        self.cli("addon", "add", "tcxOsc")
        self.assertIn(b"tcxOsc\n", (self.project / "addons.make").read_bytes())
        added = self.snapshot()
        self.assert_lf(added)
        self.cli("update")
        self.assertEqual(added, self.snapshot())
        self.cli("addon", "remove", "tcxOsc")
        self.assertEqual(before, self.snapshot())
        self.assert_lf(self.snapshot())

    def test_new_normalizes_crlf_template(self):
        framework = self.base / "framework"
        template = framework / "examples/templates/emptyExample"
        shutil.copytree(ROOT / "examples/templates/emptyExample", template,
                        ignore=shutil.ignore_patterns("build*", "CMakePresets.json"))
        expected = {}
        for path in template.rglob("*"):
            if path.is_file():
                data = path.read_bytes().replace(b"\r\n", b"\n")
                expected[str(path.relative_to(template))] = data
                path.write_bytes(data.replace(b"\n", b"\r\n"))
        # An opaque icon must not be normalized along with template text.
        icon = b"\x00\x01\r\n\xff"
        (template / "icon/app.ico").write_bytes(icon)
        destination = self.base / "new_project"
        self.cli("new", destination, "--ide", "cmake", "--tc-root", framework)
        actual = self.snapshot(destination)
        self.assertEqual(actual.pop("icon/app.ico"), icon)
        self.assert_lf(actual)
        for name, data in expected.items():
            self.assertEqual(actual[name], data, name)

    def test_update_failure_preserves_file_and_reports_once(self):
        self.cli("update", "--ide", "cmake")
        for name in ("addons.make", "CMakeLists.txt", "CMakePresets.json"):
            with self.subTest(name=name):
                before = self.snapshot()
                # A nonempty directory blocks opening the fixed .tmp file even
                # when tests run as root; it must not damage the previous file.
                blocker = self.project / (name + ".tmp")
                blocker.mkdir()
                (blocker / "keep").write_bytes(b"keep")
                try:
                    result = self.cli("update", success=False)
                    self.assertIn(name, result.stderr)
                    self.assertEqual((self.project / name).read_bytes(), before[name])
                finally:
                    shutil.rmtree(blocker)
                self.assertEqual(before, self.snapshot())

    def test_add_remove_write_failure_propagates(self):
        self.cli("update", "--ide", "cmake")
        self.cli("addon", "add", "tcxOsc")
        before = self.snapshot()
        blocker = self.project / "addons.make.tmp"
        blocker.mkdir()
        (blocker / "keep").write_bytes(b"keep")
        for command in ("add", "remove"):
            with self.subTest(command=command):
                self.cli("addon", command, "tcxOsc", success=False)
                self.assertEqual((self.project / "addons.make").read_bytes(),
                                 before["addons.make"])

    def test_parent_creation_failure_propagates(self):
        self.cli("update", "--ide", "cmake")
        (self.project / ".vscode").write_bytes(b"not a directory")
        self.cli("update", "--ide", "vscode", success=False)
        self.assertEqual((self.project / ".vscode").read_bytes(), b"not a directory")

    @unittest.skipUnless(sys.platform == "linux", "mocks the Linux cmake command")
    def test_ide_and_web_outputs_use_checked_lf_writes(self):
        commands = self.base / "commands"
        commands.mkdir()
        cmake = commands / "cmake"
        cmake.write_bytes(b"#!/bin/sh\nexit 0\n")
        cmake.chmod(0o755)
        env = {"PATH": str(commands) + os.pathsep + os.environ["PATH"]}
        self.cli("update", "--ide", "vscode", "--web", env=env)
        before = self.snapshot()
        self.assert_lf(before)
        outputs = (".vscode/launch.json", ".vscode/settings.json",
                   ".vscode/tasks.json", ".vscode/extensions.json", "build-web.sh")
        for name in outputs:
            self.assertIn(name, before)
        self.assertTrue(os.access(self.project / "build-web.sh", os.X_OK))
        self.cli("update", env=env)
        self.assertEqual(before, self.snapshot())
        for name in outputs:
            with self.subTest(name=name):
                blocker = self.project / (name + ".tmp")
                blocker.mkdir()
                (blocker / "keep").write_bytes(b"keep")
                try:
                    result = self.cli("update", success=False, env=env)
                    self.assertIn(name, result.stderr)
                    self.assertEqual((self.project / name).read_bytes(), before[name])
                finally:
                    shutil.rmtree(blocker)
                self.assertEqual(before, self.snapshot())

    @unittest.skipUnless(os.name == "posix" and Path("/dev/full").exists(),
                         "requires /dev/full")
    def test_full_disk_keeps_previous_file_and_removes_temporary(self):
        self.cli("update", "--ide", "cmake")
        before = self.snapshot()
        temporary = self.project / "addons.make.tmp"
        temporary.symlink_to("/dev/full")
        result = self.cli("update", success=False)
        self.assertIn("Write error", result.stderr)
        self.assertFalse(temporary.is_symlink())
        self.assertEqual(before, self.snapshot())


if __name__ == "__main__":
    unittest.main()
