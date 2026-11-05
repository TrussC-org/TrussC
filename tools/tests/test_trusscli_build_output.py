"""Short build output (#1240) through the real trusscli, with a fake cmake.

Set TRUSSCLI to the binary to test. A fake `cmake` first in PATH prints
scripted output on stdout and stderr, so these checks need no compiler and
no real build, and they exercise what only the binary does: reading both
streams in order, the log file, --verbose, and the exit codes. The filter's own
formats are covered by test_trusscli_output_filter.py.
"""

import os
from pathlib import Path
import stat
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()

FAKE_CMAKE = r"""#!/bin/sh
# Fake cmake: `--build` prints a scripted build, anything else a configure.
case " $* " in
*" --build "*)
    echo "[ 25%] Building CXX object CMakeFiles/app.dir/main.cpp.o"
    echo "main.cpp:3:5: warning: unused variable 'a'" >&2
    echo "[ 50%] Building CXX object CMakeFiles/app.dir/tcApp.cpp.o"
    echo "tcApp.cpp:9:1: error: expected ';'" >&2
    echo "[100%] Linking CXX executable bin/app"
    echo "Built target app"
    exit "${FAKE_BUILD_RC:-0}"
    ;;
*)
    echo "-- Configuring done"
    mkdir -p build-linux
    echo "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo" > build-linux/CMakeCache.txt
    touch build-linux/Makefile
    echo '[{"file": "main.cpp"}, {"file": "tcApp.cpp"}, {"file": "other.cpp"}]' > build-linux/compile_commands.json
    exit "${FAKE_CONFIGURE_RC:-0}"
    ;;
esac
"""


@unittest.skipUnless(sys.platform == "linux", "fake cmake is a POSIX shell script")
class BuildOutputTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="build-output-test-")
        self.addCleanup(self.scratch.cleanup)
        base = Path(self.scratch.name)
        (base / "bin").mkdir()
        fake = base / "bin" / "cmake"
        fake.write_text(FAKE_CMAKE, encoding="utf-8")
        fake.chmod(fake.stat().st_mode | stat.S_IXUSR)
        self.path = f"{base / 'bin'}{os.pathsep}{os.environ['PATH']}"
        self.project = base / "sampleApp"
        (self.project / "src").mkdir(parents=True)
        (self.project / "bin").mkdir()
        (self.project / "CMakeLists.txt").write_text(
            "cmake_minimum_required(VERSION 3.20)\nproject(sample NONE)\n", encoding="utf-8")
        (self.project / "addons.make").write_text("", encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(
            '{"version": 3, "configurePresets": [{"name": "linux", '
            '"binaryDir": "${sourceDir}/build-linux"}], '
            '"buildPresets": [{"name": "linux", "configurePreset": "linux"}]}\n',
            encoding="utf-8")
        self.log = self.project / "build-linux" / "build.log"

    def cli(self, *args, env=None):
        environment = dict(os.environ, TRUSSC_DIR=str(ROOT), PATH=self.path)
        environment.pop("CMAKE_BUILD_PARALLEL_LEVEL", None)
        environment.update(env or {})
        return subprocess.run([str(CLI), *map(str, args)], cwd=self.project, env=environment,
                              capture_output=True, encoding="utf-8", errors="replace",
                              check=False)

    def test_short_output_with_streams_in_order(self):
        result = self.cli("build", "-p", self.project, env={"FAKE_BUILD_RC": "0"})
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        out = result.stdout
        # The warning (stderr) lands between the dots (stdout), on its own line.
        self.assertIn("Building sampleApp (3 files) .....\n"
                      "main.cpp:3:5: warning: unused variable 'a'\n", out)
        self.assertIn("tcApp.cpp:9:1: error: expected ';'\n", out)
        # Noise is hidden, the configure line too
        for hidden in ("Building CXX object", "Linking CXX", "Built target", "-- Configuring"):
            self.assertNotIn(hidden, out)
        self.assertIn("Done: 2 files, 1 up to date (", out)
        self.assertIn("Full log: build-linux/build.log\n", out)
        self.assertIn("ERROR: 1 (tcApp.cpp)\nWARNING: 1 (main.cpp)\n", out)

    def test_exit_code_is_the_build_tools(self):
        for rc in (2, 7):
            with self.subTest(rc=rc):
                result = self.cli("build", "-p", self.project, env={"FAKE_BUILD_RC": str(rc)})
                self.assertEqual(result.returncode, rc, result.stdout + result.stderr)
                self.assertIn("Failed: stopped after 2 files (", result.stdout)

    def test_log_has_the_full_ordered_output_and_is_overwritten(self):
        self.cli("build", "-p", self.project)
        first = self.log.read_text(encoding="utf-8")
        self.assertEqual(first,
            "-- Configuring done\n"
            "[ 25%] Building CXX object CMakeFiles/app.dir/main.cpp.o\n"
            "main.cpp:3:5: warning: unused variable 'a'\n"
            "[ 50%] Building CXX object CMakeFiles/app.dir/tcApp.cpp.o\n"
            "tcApp.cpp:9:1: error: expected ';'\n"
            "[100%] Linking CXX executable bin/app\n"
            "Built target app\n")
        self.cli("build", "-p", self.project)   # the cache exists now: no configure
        second = self.log.read_text(encoding="utf-8")
        self.assertNotIn("-- Configuring done", second)
        self.assertTrue(second.startswith("[ 25%] Building"), second)

    def test_verbose_shows_everything(self):
        result = self.cli("build", "-p", self.project, "--verbose")
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        for shown in ("-- Configuring done\n", "[ 25%] Building CXX object", "Built target app\n",
                      "main.cpp:3:5: warning: unused variable 'a'\n"):
            self.assertIn(shown, result.stdout)
        self.assertNotIn("Done:", result.stdout)
        self.assertTrue(self.log.is_file())
        self.assertIn("-- Configuring done", self.log.read_text(encoding="utf-8"))

    def test_failed_configure_names_the_log(self):
        result = self.cli("build", "-p", self.project, env={"FAKE_CONFIGURE_RC": "1"})
        self.assertEqual(result.returncode, 1, result.stdout + result.stderr)
        self.assertIn("Failed: configure stopped (", result.stdout)
        self.assertIn("Full log: build-linux/build.log", result.stdout)
        self.assertNotIn("Building sampleApp", result.stdout)

    def test_run_shortens_only_the_build(self):
        app = self.project / "bin" / "sampleApp"
        app.write_text("#!/bin/sh\n"
                       "echo '-- app output stays'\n"
                       "echo '[ 50%] Building like a build line'\n"
                       "echo 'Built target app'\n", encoding="utf-8")
        app.chmod(app.stat().st_mode | stat.S_IXUSR)
        result = self.cli("run", "-p", self.project)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("Done: 2 files", result.stdout)
        self.assertNotIn("Building CXX object", result.stdout)
        # trusscli's own text comes before the app's output, also through a pipe
        self.assertLess(result.stdout.index("Launching sampleApp"),
                        result.stdout.index("-- app output stays"), result.stdout)
        # Everything the app prints is shown as it is
        self.assertIn("-- app output stays\n[ 50%] Building like a build line\n"
                      "Built target app\n", result.stdout)

    def test_run_verbose_forwards_to_the_build(self):
        app = self.project / "bin" / "sampleApp"
        app.write_text("#!/bin/sh\necho started\n", encoding="utf-8")
        app.chmod(app.stat().st_mode | stat.S_IXUSR)
        result = self.cli("run", "-p", self.project, "--verbose")
        self.assertIn("[ 25%] Building CXX object", result.stdout)
        self.assertNotIn("Done:", result.stdout)

    def test_help_mentions_verbose_and_the_log(self):
        for command in ("build", "run"):
            with self.subTest(command=command):
                text = self.cli(command, "--help").stdout
                self.assertIn("--verbose", text)
        self.assertIn("build.log", self.cli("build", "--help").stdout)


if __name__ == "__main__":
    unittest.main()
