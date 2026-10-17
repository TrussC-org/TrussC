"""Simulated git ls-files output; no index mutations or GPU needed."""
import contextlib
import io
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import check_test_generated_files as guard


class GeneratedTestFilesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        project = self.root / "core/tests/fixture"
        project.mkdir(parents=True)
        (project / ".gitignore").write_text(
            "# Generated project files\nCMakeLists.txt\n/CMakePresets.json\nbin/*\n")
        (project / "src").mkdir()
        (project / "addons.make").write_text("# TrussC addons - one addon per line\n")
        # Present on disk even in the clean-index case.
        (project / "CMakeLists.txt").write_text("trussc_app()\n")
        (project / "CMakePresets.json").write_text("{}\n")
        (project / "bin").mkdir()
        (project / "bin/fixture").write_bytes(b"generated binary")

    def run_guard(self, tracked):
        output = io.StringIO()
        listing = ("\0".join(tracked) + "\0").encode()
        with patch.object(guard.subprocess, "check_output", return_value=listing) as git:
            with contextlib.redirect_stdout(output):
                result = guard.main(self.root)
        git.assert_called_once_with(["git", "ls-files", "-z"], cwd=self.root)
        return result, output.getvalue()

    def test_untracked_generated_files_pass(self):
        result, output = self.run_guard([
            "core/tests/fixture/.gitignore", "core/tests/fixture/src/main.cpp"])
        self.assertEqual(result, 0)
        self.assertIn("0 violation(s)", output)

    def test_missing_core_addons_make_fails(self):
        (self.root / "core/tests/fixture/addons.make").unlink()
        result, output = self.run_guard([
            "core/tests/fixture/.gitignore", "core/tests/fixture/src/main.cpp"])
        self.assertEqual(result, 1)
        self.assertIn("1 violation(s)", output)
        self.assertIn("missing or empty core test project file: core/tests/fixture/addons.make", output)

    def test_empty_core_addons_make_fails(self):
        (self.root / "core/tests/fixture/addons.make").write_text("")
        result, output = self.run_guard([
            "core/tests/fixture/.gitignore", "core/tests/fixture/src/main.cpp"])
        self.assertEqual(result, 1)
        self.assertIn("missing or empty core test project file: core/tests/fixture/addons.make", output)

    def test_standalone_cmake_without_addons_make_passes(self):
        project = self.root / "core/tests/standalone"
        project.mkdir(parents=True)
        (project / "CMakeLists.txt").write_text("project(standalone)\n")
        (project / ".gitignore").write_text("build/\nbuild-*/\n")
        self.assertEqual(self.run_guard([
            "core/tests/standalone/CMakeLists.txt",
            "core/tests/standalone/.gitignore",
            "core/tests/standalone/main.cpp"])[0], 0)

    def test_tracked_generated_project_files_fail(self):
        paths = ["core/tests/fixture/" + name for name in sorted(guard.GENERATED)]
        result, output = self.run_guard(paths)
        self.assertEqual(result, 1)
        self.assertIn("2 violation(s)", output)
        for path in paths:
            self.assertIn(path, output)

    def test_build_outputs_directly_under_bin_fail(self):
        paths = ["core/tests/fixture/bin/fixture",
                 "core/tests/fixture/bin/fixture.pdb",
                 "addons/tcxFixture/tests/bin/fixture.exe"]
        result, output = self.run_guard(paths)
        self.assertEqual(result, 1)
        self.assertIn("3 violation(s)", output)
        for path in paths:
            self.assertIn(path, output)

    def test_tracked_bin_data_trees_pass(self):
        paths = ["core/tests/fixture/bin/data/image.png",
                 "core/tests/fixture/bin/data/nested/image.png",
                 "addons/tcxFixture/tests/bin/data/nested/movie.mov",
                 "addons/tcxHap/tests/bin/data/sine_sowt.mov",
                 "addons/tcxHap/tests/bin/data/sine_twos.mov",
                 "addons/tcxHap/tests/bin/data/sine_fl32be.mov",
                 "addons/tcxHap/tests/bin/data/sine_fl32le_enda.mov"]
        result, output = self.run_guard(paths)
        self.assertEqual(result, 0)
        self.assertIn("0 violation(s)", output)

    def test_addon_generated_project_files_fail(self):
        project = self.root / "addons/tcxFixture/tests"
        project.mkdir(parents=True)
        (project / ".gitignore").write_text("CMakeLists.txt\n/CMakePresets.json\n")
        paths = ["addons/tcxFixture/tests/" + name for name in sorted(guard.GENERATED)]
        result, output = self.run_guard(paths)
        self.assertEqual(result, 1)
        self.assertIn("2 violation(s)", output)
        for path in paths:
            self.assertIn(path, output)

    def test_handwritten_cmake_and_other_directories_pass(self):
        paths = ["core/tests/standalone/CMakeLists.txt",
                 "core/tests/standalone/CMakePresets.json",
                 "core/tests/fixture/src/CMakeLists.txt",
                 "examples/fixture/bin/data/image.png",
                 "addons/tcxFixture/CMakeLists.txt"]
        self.assertEqual(self.run_guard(paths)[0], 0)

    def test_ci_runs_guard_and_regression(self):
        workflow = (guard.ROOT / ".github/workflows/build.yml").read_text()
        job = workflow.split("  header-state-check:", 1)[1].split("  ccache-prune:", 1)[0]
        self.assertIn("python3 tools/check_test_generated_files.py", job)
        self.assertIn("python3 tools/tests/test_test_generated_files.py -v", job)


if __name__ == "__main__":
    unittest.main()
