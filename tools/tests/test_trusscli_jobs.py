"""Headless CLI checks for trusscli -j / --jobs and --dry-run (#838).

Set TRUSSCLI to the binary to test. `trusscli build --dry-run` prints the cmake
commands instead of running them, so these checks need no compiler, no cmake
and no timing: they read the generated command lines.
"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()
NATIVE = {"linux": "linux", "darwin": "macos", "win32": "windows"}[sys.platform]


class JobsTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="jobs-test-")
        self.addCleanup(self.scratch.cleanup)
        self.project = Path(self.scratch.name) / "sample project"
        (self.project / "src").mkdir(parents=True)
        (self.project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(sample NONE)\n", encoding="utf-8")
        (self.project / "addons.make").write_text("", encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(
            '{"version": 3, "configurePresets": [{"name": "%s", '
            '"binaryDir": "${sourceDir}/build-%s"}], '
            '"buildPresets": [{"name": "%s", "configurePreset": "%s"}]}\n'
            % (NATIVE, NATIVE, NATIVE, NATIVE), encoding="utf-8")

    def cli(self, *args, env=None):
        environment = dict(os.environ, TRUSSC_DIR=str(ROOT))
        environment.pop("CMAKE_BUILD_PARALLEL_LEVEL", None)
        environment.update(env or {})
        # trusscli prints UTF-8 (e.g. an em dash in upgrade --help), whatever
        # the console code page is.
        return subprocess.run([str(CLI), *map(str, args)], cwd=self.project,
                              env=environment, capture_output=True, encoding="utf-8",
                              errors="replace", check=False)

    def dry_run(self, *args, env=None):
        result = self.cli("build", "--dry-run", "-p", self.project, *args, env=env)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        lines = [line for line in result.stdout.splitlines() if line.startswith("[dry-run]")]
        builds = [line.split()[1:] for line in lines if "--build" in line.split()]
        self.assertEqual(len(builds), 1, result.stdout)
        return lines, builds[0], result.stdout

    def parallel_args(self, argv):
        return [a for a in argv if a.startswith("--parallel")]

    def test_job_count_forms_reach_cmake(self):
        for args in (["-j", "3"], ["-j3"], ["--jobs", "3"], ["--jobs=3"]):
            with self.subTest(args=args):
                lines, build, out = self.dry_run(*args)
                self.assertEqual(self.parallel_args(build), ["--parallel=3"], out)
                self.assertIn("--preset", build)
                self.assertIn(NATIVE, build)
                self.assertIn("Parallel jobs: 3 (-j)", out)
                # No build folder yet, so a configure line comes first.
                self.assertTrue(any("--preset" in l.split() and "--build" not in l.split()
                                    for l in lines), out)

    def test_default_is_unchanged(self):
        for env in ({}, {"CMAKE_BUILD_PARALLEL_LEVEL": "2"}):
            with self.subTest(env=env):
                _, build, out = self.dry_run(env=env)
                parallel = self.parallel_args(build)
                self.assertEqual(len(parallel), 1, out)
                self.assertNotIn("(-j)", out)
                if sys.platform == "linux":
                    # Linux sizes the job count by available RAM.
                    value = parallel[0].partition("=")[2]
                    self.assertTrue(parallel[0] == "--parallel" or int(value) >= 1, out)
                else:
                    self.assertEqual(parallel, ["--parallel"], out)

    def test_invalid_job_counts_are_errors(self):
        for args in (["-j", "0"], ["-j", "abc"], ["-j"], ["--jobs="], ["-j", "-1"],
                     ["--jobs", "99999"]):
            with self.subTest(args=args):
                result = self.cli("build", "--dry-run", "-p", self.project, *args)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("Error:", result.stderr)
                self.assertNotIn("[dry-run]", result.stdout)
        for command in ("run", "upgrade"):
            with self.subTest(command=command):
                result = self.cli(command, "-j", "0")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid job count", result.stderr)

    def test_dry_run_writes_nothing(self):
        before = sorted(p.relative_to(self.project) for p in self.project.rglob("*"))
        self.dry_run("-j", "2")
        after = sorted(p.relative_to(self.project) for p in self.project.rglob("*"))
        self.assertEqual(before, after)

    def test_help_documents_the_options(self):
        build_help = self.cli("build", "--help").stdout
        self.assertIn("-j, --jobs <N>", build_help)
        self.assertIn("--dry-run", build_help)
        for command in ("run", "upgrade"):
            with self.subTest(command=command):
                self.assertIn("-j, --jobs <N>", self.cli(command, "--help").stdout)


if __name__ == "__main__":
    unittest.main()
