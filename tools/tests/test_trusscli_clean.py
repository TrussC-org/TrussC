"""Headless CLI regressions for #459; set TRUSSCLI to the binary to test."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()
NATIVE = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]
BUILD_DIRS = ("build", "build-linux", "build-macos", "build-windows",
              "build-web", "build-android", "xcode-ios")
SCRIPTS = ("build-web.sh", "build-web.command", "build-web.bat")


class CleanTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="clean-test-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.project = self.base / "sample project"
        (self.project / "src/scenes/menu").mkdir(parents=True)
        for folder in BUILD_DIRS:
            (self.project / folder).mkdir()
            (self.project / folder / "sentinel").write_text("keep until cleaned", encoding="utf-8")
        for script in SCRIPTS:
            (self.project / script).write_text("generated script", encoding="utf-8")
        (self.project / "notes.txt").write_text("keep", encoding="utf-8")

    def markers(self):
        for marker in ("CMakeLists.txt", "addons.make"):
            (self.project / marker).touch()

    def cli(self, *args, cwd=None):
        return subprocess.run(
            [str(CLI), *map(str, args)], cwd=cwd or self.project,
            env=dict(os.environ, TRUSSC_DIR=str(ROOT)),
            capture_output=True, text=True, check=False,
            encoding="utf-8", errors="replace",
        )

    def snapshot(self):
        return {str(p.relative_to(self.project)): p.read_bytes()
                for p in self.project.rglob("*") if p.is_file()}

    def test_missing_markers_refuse_all_deletion(self):
        for markers in ((), ("CMakeLists.txt",), ("addons.make",)):
            for marker in ("CMakeLists.txt", "addons.make"):
                (self.project / marker).unlink(missing_ok=True)
            for marker in markers:
                (self.project / marker).touch()
            for location in ("root", "nested", "-p", "--path"):
                for options in ([], ["--all"]):
                    with self.subTest(markers=markers, location=location, options=options):
                        before = self.snapshot()
                        cwd = self.project
                        args = ["clean", *options]
                        if location == "nested":
                            cwd /= "src/scenes/menu"
                        elif location in ("-p", "--path"):
                            cwd = self.base
                            args += [location, self.project.name]
                        result = self.cli(*args, cwd=cwd)
                        self.assertNotEqual(result.returncode, 0)
                        self.assertIn(self.project.name, result.stderr)
                        self.assertIn("not a TrussC project", result.stderr)
                        self.assertIn("Nothing was removed", result.stderr)
                        self.assertEqual(before, self.snapshot())

    def test_valid_project_native_cleanup(self):
        self.markers()
        result = self.cli("clean", cwd=self.project / "src/scenes/menu")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(result.stdout.startswith(f"Cleaning project: {self.project}\n"))
        for folder in BUILD_DIRS:
            removed = folder in ("build", "build-" + NATIVE)
            self.assertEqual((self.project / folder).exists(), not removed)
            if removed:
                self.assertIn(f"  Removing {folder}/\n", result.stdout)
        for script in SCRIPTS:
            self.assertTrue((self.project / script).exists())
        self.assertTrue((self.project / "notes.txt").exists())

    def linked_build_dir(self, folder):
        # The build folder is a symlink to a place outside the project, with a
        # symlink inside it to a third place that must never be touched.
        target = self.base / "elsewhere"
        outside = self.base / "outside"
        (target / "sub").mkdir(parents=True)
        outside.mkdir()
        (target / "stale.o").write_text("stale", encoding="utf-8")
        (target / "sub/deep.o").write_text("stale", encoding="utf-8")
        (outside / "canary.txt").write_text("keep", encoding="utf-8")
        (target / "link").symlink_to(outside, target_is_directory=True)
        shutil.rmtree(self.project / folder)
        (self.project / folder).symlink_to(target, target_is_directory=True)
        return target, outside

    @unittest.skipIf(sys.platform == "win32", "symlinks need privileges on Windows")
    def test_symlinked_build_dir_is_emptied_and_kept(self):
        # #1102: the configured location survives, nothing behind a nested link is deleted
        self.markers()
        folder = "build-" + NATIVE
        target, outside = self.linked_build_dir(folder)
        result = self.cli("clean")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.project / folder).is_symlink())
        self.assertEqual(Path(os.readlink(self.project / folder)), target)
        self.assertEqual(list(target.iterdir()), [])
        self.assertEqual((outside / "canary.txt").read_text(encoding="utf-8"), "keep")

    @unittest.skipIf(sys.platform == "win32", "symlinks need privileges on Windows")
    def test_dangling_symlink_build_dir_is_left_alone(self):
        self.markers()
        folder = "build-" + NATIVE
        shutil.rmtree(self.project / folder)
        (self.project / folder).symlink_to(self.base / "missing", target_is_directory=True)
        result = self.cli("clean")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue((self.project / folder).is_symlink())

    def test_valid_project_explicit_all_cleanup(self):
        self.markers()
        result = self.cli("clean", "--all", "-p", self.project, cwd=self.base)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue(result.stdout.startswith(f"Cleaning project: {self.project}\n"))
        for entry in (*BUILD_DIRS, *SCRIPTS):
            self.assertFalse((self.project / entry).exists(), entry)
            self.assertIn(f"  Removing {entry}", result.stdout)
        for entry in ("src", "CMakeLists.txt", "addons.make", "notes.txt"):
            self.assertTrue((self.project / entry).exists(), entry)
        result = self.cli("clean", "-p", ".")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(str(self.project), result.stdout)
        self.assertIn("Nothing to clean.", result.stdout)

    def nested(self, depth):
        path = self.project.joinpath(*(f"level{i}" for i in range(depth)))
        path.mkdir(parents=True, exist_ok=True)
        return path

    def test_project_five_levels_up_is_detected(self):
        # CWD plus five parents: a project five levels up is found.
        nested = self.nested(5)
        result = self.cli("info", "project", "--json", cwd=nested)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["path"], str(self.project))

    def test_six_levels_below_project_is_not_detected(self):
        self.markers()
        nested = self.nested(6)
        before = self.snapshot()
        result = self.cli("clean", "--all", cwd=nested)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("not inside a TrussC project", result.stderr)
        self.assertEqual(before, self.snapshot())
        result = self.cli("info", "project", "--json", cwd=nested)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIsNone(json.loads(result.stdout))

    def test_search_uses_src_only(self):
        # Detection still keys on src/ alone (update needs it), so a project
        # without CMakeLists.txt / addons.make is found from inside it.
        nested = self.project / "bin/data/sounds/bgm"
        nested.mkdir(parents=True)
        result = self.cli("info", "project", "--json", cwd=nested)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(json.loads(result.stdout)["path"], str(self.project))

    def test_cp_keeps_same_marker_check(self):
        destination = self.base / "copy"
        for markers in ((), ("CMakeLists.txt",), ("addons.make",),
                        ("CMakeLists.txt", "addons.make")):
            with self.subTest(markers=markers):
                for marker in ("CMakeLists.txt", "addons.make"):
                    (self.project / marker).unlink(missing_ok=True)
                for marker in markers:
                    (self.project / marker).touch()
                result = self.cli("cp", self.project, destination, "--dry-run", "--no-git")
                if len(markers) == 2:
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                else:
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn("not a TrussC project", result.stderr)
                self.assertFalse(destination.exists())


if __name__ == "__main__":
    unittest.main()
