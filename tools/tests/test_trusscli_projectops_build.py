"""Offline build operations and cancellation of owned process groups.

Run through tcbuild.sh. Conditions have bounded waits; no elapsed-time verdicts.
"""
import json
import errno
import os
from pathlib import Path
import select
import shutil
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()


def host_uses_deb():
    if sys.platform != "linux":
        return False
    release = Path("/etc/os-release").read_text(encoding="utf-8")
    fields = dict(line.split("=", 1) for line in release.splitlines() if "=" in line)
    ids = (fields.get("ID", "").strip('"') + " " + fields.get("ID_LIKE", "").strip('"')).split()
    for value in ids:
        if value in ("arch", "debian", "ubuntu"):
            return value != "arch"
    return False


def wait_for(condition, timeout=20):
    deadline = time.monotonic() + timeout
    while not condition():
        if time.monotonic() >= deadline:
            raise AssertionError("condition was not reached")
        time.sleep(0.01)


@unittest.skipUnless(os.name == "posix", "requires POSIX process groups, signals, and PTYs")
class BuildOperationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory(prefix="projectops-build-driver-")
        cls.addClassCleanup(cls.build.cleanup)
        scratch = Path(cls.build.name)
        (scratch / "TrussC.h").write_text(
            '#pragma once\n#include <nlohmann/json.hpp>\n'
            '#if defined(__GNUC__) && !defined(__clang__) && __GNUC__ >= 13\n'
            '#pragma GCC diagnostic ignored "-Wdangling-reference"\n#endif\n'
            'namespace tc { using Json = nlohmann::json; }\n', encoding="utf-8")
        cls.driver = scratch / "driver"
        sources = ["ProjectDist", "Dist", "ProjectRun", "ProjectBuild", "ProjectOps", "ProjectGenerator", "ProjectState",
                   "BuildSetup", "BuildOutputFilter", "IdeHelper", "VsDetector",
                   "AddonVersions", "TrussCVersion", "ProcessCapture"]
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
                        "-Wno-missing-field-initializers",  # Dist.cpp uses defaulted CopyItem fields.
                        "-include", str(scratch / "TrussC.h"), "-I", str(scratch),
                        "-I", str(ROOT / "tools/src"), "-I", str(ROOT / "core/include"),
                        str(ROOT / "tools/tests/projectops_build_driver.cpp"),
                        *[str(ROOT / "tools/src" / (name + ".cpp")) for name in sources],
                        *(["-Wl,--wrap=access"] if sys.platform == "linux" else []),
                        "-o", str(cls.driver)], check=True)

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="projectops-build-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.preset = "macos" if sys.platform == "darwin" else "linux"
        self.project = self.base / "sample"
        self.build_dir = self.project / ("build-" + self.preset)
        self.app = self.project / ("bin/sample.app/Contents/MacOS/sample"
                                   if sys.platform == "darwin" else "bin/sample")
        (self.project / "src").mkdir(parents=True)
        (self.project / "addons.make").touch()
        (self.project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(sample LANGUAGES CXX)\n'
            'add_executable(sample src/main.cpp)\n'
            f'set_target_properties(sample PROPERTIES RUNTIME_OUTPUT_DIRECTORY "{self.app.parent}")\n',
            encoding="utf-8")
        (self.project / "src/main.cpp").write_text('int main() { return 0; }\n', encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(json.dumps({
            "version": 3, "configurePresets": [{"name": self.preset, "generator": "Unix Makefiles",
            "binaryDir": "${sourceDir}/build-" + self.preset, "cacheVariables": {"FETCHCONTENT_FULLY_DISCONNECTED": "ON"}}],
            "buildPresets": [{"name": self.preset, "configurePreset": self.preset}]}), encoding="utf-8")
        self.env = dict(os.environ)
        self.x11_directory = self.base / "fake-x11"
        self.x11_directory.mkdir()
        self.env["TEST_X11_DIRECTORY"] = str(self.x11_directory)

    def test_empty_fetch_callback_keeps_missing_version_offline(self):
        (self.project / ".trussc-version").write_text("vMissing\n", encoding="utf-8")
        cache = self.base / "empty-cache"
        self.env["TRUSSC_CACHE_DIR"] = str(cache)
        report = self.operate()
        self.assertFalse(report["success"])
        self.assertIn("version is unavailable; run `trusscli update`", report["error"])
        self.assertNotIn("Download now?", report["output"])
        self.assertFalse((cache / "versions/TrussC-org/TrussC/vMissing").exists())

    def operate(self, mode="build", *args):
        result = subprocess.run([str(self.driver), mode, str(self.project), *args],
                                env=self.env, capture_output=True, text=True, encoding="utf-8", timeout=40)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertTrue(report["cwdRestored"])
        self.assertTrue(report["cwdUnchangedDuringProcess"])
        if mode not in ("noisy", "run-noisy"):
            self.assertEqual(report["logs"], [event["message"] for event in report["events"]])
        return report

    def test_configure_build_type_and_compile_failure(self):
        report = self.operate("build", "Release")
        self.assertTrue(report["success"], report)
        self.assertIn("Built target sample", report["output"])
        self.assertTrue(self.app.is_file())
        report = self.operate("build", "Debug")
        self.assertTrue(report["success"], report)
        self.assertIn("CMAKE_BUILD_TYPE:STRING=Debug", (self.build_dir / "CMakeCache.txt").read_text(encoding="utf-8"))
        (self.project / "src/main.cpp").write_text("compile_error\n", encoding="utf-8")
        # Make 3.81 compares whole seconds; force this rewrite past the object.
        modified = time.time() + 2
        os.utime(self.project / "src/main.cpp", (modified, modified))
        report = self.operate()
        self.assertFalse(report["success"])
        self.assertFalse(report["stopped"])
        self.assertNotEqual(report["exitCode"], 0)
        self.assertIn("error:", (self.build_dir / "build.log").read_text(encoding="utf-8"))

    @unittest.skipUnless(sys.platform == "linux", "fake tools on PATH")
    def test_update_build_and_run_in_special_character_path(self):
        folder = self.base / "a&b space 日本語 $(touch MARKER) `touch MARKER`"
        folder.mkdir()
        self.project = self.project.rename(folder / "sample")
        (self.project / "CMakeLists.txt").write_text("# TRUSSC_DIR\n", encoding="utf-8")
        self.build_dir = self.project / "build-linux"
        self.app = self.project / "bin/sample"
        self.app.parent.mkdir()
        self.app.write_text('#!/bin/sh\nprintf "app ran\\n"\n', encoding="utf-8")
        self.app.chmod(0o755)
        framework = self.base / "framework"
        (framework / "core/cmake").mkdir(parents=True)
        (framework / "core/cmake/trussc_app.cmake").touch()
        fake_bin = self.base / "fake-bin"
        fake_bin.mkdir()
        cmake = fake_bin / "cmake"
        cmake.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
from pathlib import Path
with open(os.environ["CMAKE_CALLS"], "a", encoding="utf-8") as log:
    log.write(json.dumps([os.getcwd(), sys.argv[1:]]) + "\\n")
build = Path("build-linux")
if "--preset" in sys.argv:
    build.mkdir(exist_ok=True)
    (build / "Makefile").touch()
    (build / "CMakeCache.txt").write_text("CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\\n")
elif "--build" not in sys.argv:
    sys.exit(2)
''', encoding="utf-8")
        cmake.chmod(0o755)
        ninja = fake_bin / "ninja"
        ninja.write_text('#!/bin/sh\necho 1.11.1\n', encoding="utf-8")
        ninja.chmod(0o755)
        calls_path = self.base / "cmake-calls.jsonl"
        self.env.update(PATH=str(fake_bin), TRUSSC_DIR=str(framework),
                        CMAKE_CALLS=str(calls_path))
        # Direct update uses the generator executor. Missing metadata on build
        # uses ProjectBuild's command runner for the same configure operation.
        for mode in ("update", "build", "run"):
            if mode == "build":
                (self.project / "CMakeLists.txt").unlink()
                (self.project / "CMakePresets.json").unlink()
            report = self.operate(mode)
            self.assertTrue(report["success"], report)
            if mode == "run":
                self.assertIn("app ran", report["output"])
        self.assertFalse((self.base / "MARKER").exists())
        self.assertFalse((self.project / "MARKER").exists())
        calls = [json.loads(line) for line in calls_path.read_text(encoding="utf-8").splitlines()]
        self.assertEqual(len(calls), 4, calls)
        self.assertTrue(all(cwd == str(self.project) for cwd, _ in calls), calls)
        self.assertEqual([args for _, args in calls[:2]],
                         [["--preset", "linux"], ["--preset", "linux"]])
        self.assertTrue(all(args[:3] == ["--build", "--preset", "linux"]
                            for _, args in calls[2:]), calls)

    def child_fixture(self, ignore=False, parent_exits=False):
        fake = self.base / "fake-bin"
        fake.mkdir()
        script = fake / "build-tool"
        script.write_text(f'''#!{sys.executable}
import os, signal, sys, time
# The build call (-f Makefile) and execute_process (no arguments) act;
# any other call is a CMake probe of the make program.
with open(os.environ["CHILD_PIDS"] + ".calls", "a") as calls:
    calls.write(repr(sys.argv[1:]) + "\\n")
if sys.argv[1:] and "-f" not in sys.argv[1:]:
    print("GNU Make 4.3")
    sys.exit(0)
ignore = {ignore!r}
parent_exits = {parent_exits!r}
child = 0
def finish(number, frame):
    open(os.environ["STOP_MARKERS"] + str(os.getpid()), "w").close()
    if child and not parent_exits:
        os.waitpid(child, 0)
    sys.exit(0)
for number in (signal.SIGINT, signal.SIGTERM):
    signal.signal(number, signal.SIG_IGN if ignore else finish)
read_end, write_end = os.pipe()
child = os.fork()
if child == 0:
    os.close(read_end)
    if parent_exits:
        signal.signal(signal.SIGTERM, signal.SIG_IGN)
    os.write(write_end, b"ready")
    os.close(write_end)
    while True: signal.pause()
os.close(write_end)
os.read(read_end, 5)
os.close(read_end)
open(os.environ["CHILD_PIDS"], "w").write(str(os.getpid()) + " " + str(child))
print("READY", flush=True)
open(os.environ["CHILD_READY"], "w").close()
while True: signal.pause()
''', encoding="utf-8")
        script.chmod(0o755)
        build = self.build_dir
        build.mkdir()
        (build / "Makefile").touch()
        # Let the real CMake launch the fixture, including when macOS selects
        # CMake by absolute path instead of searching PATH.
        (build / "CMakeCache.txt").write_text(
            "CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n"
            "CMAKE_GENERATOR:INTERNAL=Unix Makefiles\n"
            "CMAKE_PROJECT_NAME:STATIC=sample\n"
            f"CMAKE_MAKE_PROGRAM:FILEPATH={script}\n", encoding="utf-8")
        self.pids = self.base / "pids"
        self.ready = self.base / "ready"
        self.env.update(CHILD_PIDS=str(self.pids), CHILD_READY=str(self.ready),
                        STOP_MARKERS=str(self.base / "stopped-"))

    def fixture_calls(self):
        calls = Path(str(self.pids) + ".calls")
        return "build-tool calls:\n" + (calls.read_text(encoding="utf-8") if calls.exists() else "(none)\n")

    def assert_children_exited(self):
        for pid in map(int, self.pids.read_text(encoding="utf-8").split()):
            def ended():
                # One snapshot: both a vanished PID and a zombie have exited.
                result = subprocess.run(["ps", "-o", "stat=", "-p", str(pid)],
                                        capture_output=True, text=True, encoding="utf-8", timeout=5)
                self.assertIn(result.returncode, (0, 1), result.stderr)
                state = result.stdout.strip()
                return not state or state.startswith("Z")
            wait_for(ended)

    def test_request_stop_returns_stopped_result(self):
        self.child_fixture()
        report = self.operate("stop")
        self.assertFalse(report["success"])
        self.assertTrue(report["stopped"])
        self.assertEqual(report["exitCode"], -1)
        self.assert_children_exited()
        for pid in self.pids.read_text(encoding="utf-8").split():
            self.assertTrue((self.base / ("stopped-" + pid)).exists())

    def test_still_running_needs_explicit_force_stop(self):
        self.child_fixture(ignore=True)
        with subprocess.Popen([sys.executable, "-c", "import signal; signal.pause()"],
                              start_new_session=True) as unrelated:
            try:
                report = self.operate("force")
                self.assertIsNone(unrelated.poll())
            finally:
                unrelated.terminate()
                unrelated.wait()
        self.assertTrue(report["stillRunning"])
        self.assertTrue(report["stopped"])
        self.assertEqual(report["exitCode"], -1)
        self.assert_children_exited()

    def test_automatic_update_uses_the_build_cancel_handle(self):
        self.child_fixture()
        framework = self.base / "framework"
        (framework / "core/cmake").mkdir(parents=True)
        (framework / "core/cmake/trussc_app.cmake").write_text(
            'execute_process(COMMAND "' + str(self.base / "fake-bin/build-tool") + '")\n'
            'function(trussc_app)\nendfunction()\n', encoding="utf-8")
        self.env["TRUSSC_DIR"] = str(framework)
        (self.project / "CMakeLists.txt").unlink()
        (self.project / "CMakePresets.json").unlink()
        report = self.operate("stop")
        self.assertTrue(report["stopped"], report)
        self.assertFalse(report["success"])
        self.assertIn("Build files not found", report["output"])
        self.assert_children_exited()

    def test_stop_tracks_survivor_after_parent_exits_without_waiting(self):
        self.child_fixture(parent_exits=True)
        report = self.operate("force-orphan")
        self.assertTrue(report["stillRunning"], report)
        self.assertTrue(report["parentExitedAfterStop"], report)
        self.assertTrue(report["survivorBeforeForce"], report)
        self.assertTrue(report["stopped"])
        self.assertFalse(report["success"])
        self.assert_children_exited()

    def test_cli_terminal_group_sigint_and_sigterm_reach_build_grandchild(self):
        self.child_fixture()
        for number in (signal.SIGINT, signal.SIGTERM):
            with self.subTest(signal=number):
                self.pids.unlink(missing_ok=True)
                self.ready.unlink(missing_ok=True)
                with subprocess.Popen([str(CLI), "build", "-p", str(self.project), "--verbose", "-j1"],
                                      env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                      start_new_session=True, text=True, encoding="utf-8") as process:
                    try:
                        # The file can appear before CMake/trusscli forward READY.
                        # Observe the output before terminating that forwarding path.
                        output = b""
                        def received_ready():
                            nonlocal output
                            if select.select([process.stdout], [], [], 0)[0]:
                                data = os.read(process.stdout.fileno(), 4096)
                                self.assertTrue(data, output)
                                output += data
                            return b"READY\n" in output
                        try:
                            wait_for(received_ready)
                        except AssertionError:
                            self.fail("READY was not forwarded: " + repr(output) + "\n" + self.fixture_calls())
                        os.killpg(process.pid, number)
                        remaining, _ = process.communicate(timeout=20)
                        output = output.decode("utf-8") + remaining
                        self.assertEqual(process.returncode, -number, output)
                        self.assertIn("READY", output)
                        self.assert_children_exited()
                    finally:
                        if process.poll() is None:
                            os.killpg(process.pid, signal.SIGKILL)
                            process.wait()

    def test_cli_build_and_regeneration_can_read_terminal(self):
        import pty
        self.child_fixture()
        script = self.base / "fake-bin/build-tool"
        script.write_text(f'''#!{sys.executable}
import os, sys
from pathlib import Path
# The build call (-f Makefile) and execute_process (no arguments) act;
# any other call is a CMake probe of the make program.
with open(os.environ["CHILD_PIDS"] + ".calls", "a") as calls:
    calls.write(repr(sys.argv[1:]) + "\\n")
if sys.argv[1:] and "-f" not in sys.argv[1:]:
    print("GNU Make 4.3")
    sys.exit(0)
marker = Path(os.environ["CHILD_READY"])
if not marker.exists():
    Path(str(marker) + ".prompt").touch()
    print("READ_READY", flush=True)
    assert sys.stdin.readline() == "terminal input\\n"
    print("READ_DONE", flush=True)
    marker.touch()
''', encoding="utf-8")
        for regenerate in (False, True):
            with self.subTest(regeneration=regenerate):
                self.ready.unlink(missing_ok=True)
                prompt = Path(str(self.ready) + ".prompt")
                prompt.unlink(missing_ok=True)
                if regenerate:
                    framework = self.base / "framework"
                    (framework / "core/cmake").mkdir(parents=True)
                    (framework / "core/cmake/trussc_app.cmake").write_text(
                        'execute_process(COMMAND "' + str(script) + '")\n'
                        'function(trussc_app)\nendfunction()\n', encoding="utf-8")
                    self.env["TRUSSC_DIR"] = str(framework)
                    (self.project / "CMakeLists.txt").unlink()
                    (self.project / "CMakePresets.json").unlink()
                pid, terminal = pty.fork()
                if pid == 0:
                    os.execve(str(CLI), [str(CLI), "build", "-p", str(self.project), "--verbose", "-j1"], self.env)
                output = b""
                sent = False
                status = None
                ended = 0
                terminal_closed = False
                try:
                    deadline = time.monotonic() + 20
                    while time.monotonic() < deadline:
                        if not terminal_closed and select.select([terminal], [], [], 0.05)[0]:
                            try:
                                data = os.read(terminal, 4096)
                                output += data
                                terminal_closed = not data
                            except OSError as error:
                                if error.errno != errno.EIO:
                                    raise
                                terminal_closed = True  # Linux PTY EOF; macOS returns b"".
                        if prompt.exists() and not sent:
                            os.write(terminal, b"terminal input\n")
                            sent = True
                        if not ended:
                            ended, status = os.waitpid(pid, os.WNOHANG)
                        # Child exit can precede reading its final PTY output.
                        if ended and terminal_closed:
                            break
                    else:
                        self.fail("terminal read did not complete: " + repr(output))
                    self.assertEqual(os.waitstatus_to_exitcode(status), 0, output)
                    self.assertIn(b"READ_DONE", output, self.fixture_calls())
                finally:
                    os.close(terminal)
                    try:
                        os.killpg(pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
                    if status is None or not ended:
                        os.waitpid(pid, 0)

    def test_retained_log_is_bounded_while_callback_and_file_are_complete(self):
        self.child_fixture()
        script = self.base / "fake-bin/build-tool"
        for lines, width in ((20000, 256), (40000, 8)):
            with self.subTest(lines=lines, width=width):
                script.write_text(f'''#!{sys.executable}
import sys
sys.stdout.write("FIRST_OUTPUT\\n" + ("x" * {width} + "\\n") * {lines} + "LAST_OUTPUT\\n")
''', encoding="utf-8")
                report = self.operate("noisy")
                self.assertTrue(report["success"], report["error"])
                self.assertLessEqual(sum(len(line) for line in report["logs"]), 2 * 1024 * 1024)
                self.assertLessEqual(len(report["logs"]), 32768)
                self.assertEqual(report["logs"][0], "[Earlier build output truncated]\n")
                self.assertNotIn("FIRST_OUTPUT", "".join(report["logs"]))
                self.assertIn("LAST_OUTPUT", "".join(report["logs"]))
                full = "FIRST_OUTPUT\n" + ("x" * width + "\n") * lines + "LAST_OUTPUT\n"
                self.assertIn(full, report["output"])
                self.assertEqual(report["output"], "".join(event["message"] for event in report["events"]))
                self.assertEqual((self.build_dir / "build.log").read_text(encoding="utf-8"), full)

    def run_fixture(self, ignore=False):
        self.child_fixture(ignore=ignore)
        app = self.app
        app.parent.mkdir(parents=True)
        app.write_bytes((self.base / "fake-bin/build-tool").read_bytes())
        app.chmod(0o755)
        (self.base / "fake-bin/build-tool").write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")

    def test_run_streams_app_output_and_returns_exit_status(self):
        for code in (0, 7):
            with self.subTest(code=code):
                (self.project / "src/main.cpp").write_text(
                    '#include <cstdio>\nint main() {\n'
                    'puts("APP_STDOUT"); fflush(stdout);\n'
                    'fputs("APP_STDERR\\n", stderr);\n'
                    f'return {code}; }}\n', encoding="utf-8")
                modified = time.time() + 2
                os.utime(self.project / "src/main.cpp", (modified, modified))
                report = self.operate("run")
                self.assertEqual(report["success"], code == 0, report)
                self.assertEqual(report["exitCode"], code)
                self.assertFalse(report["stopped"])
                self.assertIn("Launching sample ...\nAPP_STDOUT\nAPP_STDERR\n", report["output"])
        (self.project / "src/main.cpp").write_text("compile_error\n", encoding="utf-8")
        modified = time.time() + 2
        os.utime(self.project / "src/main.cpp", (modified, modified))
        report = self.operate("run")
        self.assertFalse(report["success"])
        self.assertNotIn("Launching sample", report["output"])

    def test_run_retained_output_is_bounded_but_callback_is_complete(self):
        self.run_fixture()
        app = self.app
        app.write_text(f'#!{sys.executable}\n'
                       'print("FIRST_APP_OUTPUT")\n'
                       'print("x" * (3 * 1024 * 1024))\n'
                       'print("LAST_APP_OUTPUT")\n', encoding="utf-8")
        report = self.operate("run-noisy")
        self.assertTrue(report["success"], report["error"])
        self.assertLessEqual(sum(len(line) for line in report["logs"]), 2 * 1024 * 1024)
        self.assertLessEqual(len(report["logs"]), 32768)
        self.assertEqual(report["logs"][0], "[Earlier run output truncated]\n")
        self.assertNotIn("FIRST_APP_OUTPUT", "".join(report["logs"]))
        self.assertIn("LAST_APP_OUTPUT", "".join(report["logs"]))
        self.assertIn("FIRST_APP_OUTPUT", report["output"])
        self.assertEqual(report["output"], "".join(event["message"] for event in report["events"]))

    def test_run_reports_missing_binary(self):
        self.child_fixture()
        (self.base / "fake-bin/build-tool").write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        report = self.operate("run")
        self.assertFalse(report["success"])
        self.assertEqual(report["exitCode"], 1)
        self.assertIn("built binary not found", report["error"])

    @unittest.skipUnless(sys.platform == "linux", "X11/labwc session launch is Linux-only")
    def test_run_display_session_launcher_and_spawn_failure(self):
        self.run_fixture()
        app = self.app
        app.write_text("#!/bin/sh\nprintf 'SESSION_APP\\n'\n", encoding="utf-8")
        arguments = self.base / "session-arguments"
        self.env["SESSION_ARGS"] = str(arguments)
        (self.base / "fake-bin/cmake").symlink_to(shutil.which("cmake"))
        labwc = self.base / "fake-bin/labwc"
        labwc.write_text(f'''#!{sys.executable}
import json, os, sys
from pathlib import Path
script = Path(sys.argv[2])
Path(os.environ["SESSION_ARGS"]).write_text(json.dumps(
    [sys.argv[1], str(script), script.parent.stat().st_mode & 0o777]))
os.execv(str(script), [str(script)])
''', encoding="utf-8")
        labwc.chmod(0o755)
        self.env["PATH"] = str(self.base / "fake-bin") + os.pathsep + self.env["PATH"]
        report = self.operate("run", "", "labwc")
        self.assertTrue(report["success"], report)
        self.assertIn("Launching via labwc Wayland session ...\nSESSION_APP\n", report["output"])
        self.assert_labwc_temp_files_removed(arguments)
        # No X server or display socket is created by this spawn-failure fixture.
        self.env["PATH"] = str(self.base / "fake-bin")
        report = self.operate("run", "", "x11")
        self.assertFalse(report["success"])
        self.assertEqual(report["exitCode"], -1)
        self.assertIn("failed to spawn xinit", report["error"])

    def assert_session_temp_removed(self, script, mode):
        script = Path(script)
        self.assertEqual(script.parent.parent, Path("/tmp"))
        self.assertRegex(script.parent.name, r"^trusscli-session-[A-Za-z0-9]{6}$")
        self.assertEqual(mode, 0o700)
        self.assertFalse(script.exists())
        self.assertFalse(script.parent.exists())

    def assert_labwc_temp_files_removed(self, arguments):
        option, script, mode = json.loads(arguments.read_text(encoding="utf-8"))
        self.assertEqual(option, "-S")
        self.assert_session_temp_removed(script, mode)

    @unittest.skipUnless(sys.platform == "linux", "labwc session launch is Linux-only")
    def test_run_labwc_prefers_app_status_and_falls_back_without_status(self):
        # Exercise spaces, a single quote, and CR in the app path.
        parent = self.base / "owner's projects\r"
        parent.mkdir()
        self.project = self.project.rename(parent / "sample")
        self.build_dir = self.project / ("build-" + self.preset)
        self.app = self.project / "bin/sample"
        self.run_fixture()
        arguments = self.base / "session-arguments"
        self.env["SESSION_ARGS"] = str(arguments)
        labwc = self.base / "fake-bin/labwc"
        labwc.write_text(f'''#!{sys.executable}
import json, os, resource, signal, subprocess, sys
from pathlib import Path
resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
script = Path(sys.argv[2])
Path(os.environ["SESSION_ARGS"]).write_text(json.dumps(
    [sys.argv[1], str(script), script.parent.stat().st_mode & 0o777]))
mode = os.environ["LABWC_MODE"]
if mode == "crash":
    subprocess.run([sys.argv[2]], check=False)
    os.kill(os.getpid(), signal.SIGSEGV)
if mode == "invalid":
    Path(sys.argv[2]).with_suffix(".rc").write_text("invalid\\n")
if mode in ("boundary", "long"):
    script.with_suffix(".rc").write_text("3" + " " * (15 if mode == "boundary" else 16))
sys.exit(7)
''', encoding="utf-8")
        labwc.chmod(0o755)
        self.env["PATH"] = str(labwc.parent) + os.pathsep + self.env["PATH"]
        for mode, app_code in (("crash", 0), ("crash", 3), ("skip", 0), ("invalid", 0),
                               ("boundary", 3), ("long", 0)):
            with self.subTest(mode=mode, app_code=app_code):
                self.env["LABWC_MODE"] = mode
                self.app.write_text("#!/bin/sh\nprintf 'APP_STDOUT\\n'\n"
                                    "printf 'APP_STDERR\\n' >&2\n"
                                    f"exit {app_code}\n", encoding="utf-8")
                report = self.operate("run", "", "labwc")
                expected = app_code if mode in ("crash", "boundary") else 7
                self.assertEqual(report["exitCode"], expected, report)
                self.assertEqual(report["success"], expected == 0, report)
                self.assertFalse(report["stopped"])
                for marker in ("APP_STDOUT", "APP_STDERR"):
                    if mode == "crash":
                        self.assertIn(marker, report["output"])
                    else:
                        self.assertNotIn(marker, report["output"])
                self.assert_labwc_temp_files_removed(arguments)

    @unittest.skipUnless(sys.platform == "linux", "X11 session launch is Linux-only")
    def test_run_x11_busy_display_rejects_socket_or_lock_before_launch(self):
        self.run_fixture()
        marker = self.base / "unexpected-launch"
        self.env["UNEXPECTED_LAUNCH"] = str(marker)
        for binary in (self.app, self.base / "fake-bin/xinit", self.base / "fake-bin/xhost"):
            binary.write_text('#!/bin/sh\nprintf launched >> "$UNEXPECTED_LAUNCH"\n', encoding="utf-8")
            binary.chmod(0o755)
        self.env["PATH"] = str(self.base / "fake-bin") + os.pathsep + self.env["PATH"]
        for entries in (("X0",), (".X0-lock",), ("X0", ".X0-lock")):
            with self.subTest(entries=entries):
                for entry in entries:
                    (self.x11_directory / entry).touch()
                try:
                    report = self.operate("run", "", "x11")
                    self.assertFalse(report["success"], report)
                    self.assertNotEqual(report["exitCode"], 0)
                    self.assertFalse(report["stopped"])
                    self.assertIn("display :0 is already in use (is a desktop running?)", report["error"])
                    self.assertIn("--session x11 is for a machine without a desktop session.", report["error"])
                    self.assertNotIn("Launching via xinit", report["output"])
                    self.assertFalse(marker.exists(), report)
                finally:
                    for entry in entries:
                        (self.x11_directory / entry).unlink()

    @unittest.skipUnless(sys.platform == "linux", "X11 session launch is Linux-only")
    def test_run_xinit_timeout_reaps_cooperative_and_stubborn_children(self):
        self.run_fixture()
        child_info = self.base / "xinit-child.json"
        self.env["XINIT_CHILD_INFO"] = str(child_info)
        xinit = self.base / "fake-bin/xinit"
        self.env["PATH"] = str(xinit.parent) + os.pathsep + self.env["PATH"]
        for ignore_term in (False, True):
            with self.subTest(ignore_term=ignore_term):
                xinit.write_text(f'''#!{sys.executable}
import json, os, signal, sys
from pathlib import Path
def on_term(*_):
    with open(os.environ["XINIT_CHILD_INFO"] + ".term", "w") as marker:
        marker.write("term")
    if not {ignore_term!r}:
        sys.exit(0)
signal.signal(signal.SIGTERM, on_term)
with open(os.environ["XINIT_CHILD_INFO"], "w") as info:
    json.dump([os.getpid(), sys.argv[1], Path(sys.argv[1]).parent.stat().st_mode & 0o777], info)
while True:
    signal.pause()
''', encoding="utf-8")
                xinit.chmod(0o755)
                term_marker = Path(str(child_info) + ".term")
                try:
                    # run returns only after trusscli has reaped xinit, so the
                    # child's PID is never signaled from here.
                    report = self.operate("run", "", "x11")
                    self.assertFalse(report["success"])
                    self.assertIn("X server did not start within 6 seconds", report["error"])
                    _, xinitrc, mode = json.loads(child_info.read_text(encoding="utf-8"))
                    # Both children got SIGTERM first; the stubborn one was then killed.
                    self.assertEqual(term_marker.read_text(encoding="utf-8"), "term")
                    self.assert_session_temp_removed(xinitrc, mode)
                finally:
                    child_info.unlink(missing_ok=True)
                    term_marker.unlink(missing_ok=True)

    def dist_fixture(self, ignore=False, step="cmake"):
        self.child_fixture(ignore=ignore)
        self.env["TRUSSC_DIR"] = str(ROOT)
        self.app.parent.mkdir(parents=True)
        shutil.copy2(sys.executable, self.app)
        # Packaging and build-stop commands use fake tools found through PATH.
        self.env["PATH"] = str(self.base / "fake-bin") + os.pathsep + self.env["PATH"]
        self.env["DIST_STEP"] = step
        for tool in ("cmake", "strip", "dpkg-shlibdeps", "dpkg-deb"):
            script = (self.base / "fake-bin/build-tool").read_text(encoding="utf-8")
            script = script.replace('if sys.argv[1:] and "-f" not in sys.argv[1:]:', 'if False:')
            first, rest = script.split("\n", 1)
            (self.base / "fake-bin" / tool).write_text(first + "\n" +
                'import os, sys\nfrom pathlib import Path\n'
                'if Path(sys.argv[0]).name != os.environ["DIST_STEP"]:\n'
                '    sys.exit(0)\n'
                'if Path(sys.argv[0]).name == "cmake" and "-E" in sys.argv[1:]:\n'
                '    (Path(sys.argv[3]) / sys.argv[-2]).write_text("partial archive")\n'
                + rest, encoding="utf-8")
            (self.base / "fake-bin" / tool).chmod(0o755)

    @unittest.skipUnless(sys.platform == "linux", "Linux packaging fixtures")
    def test_dist_archive_and_installer_stop_cooperatively(self):
        for step in ("cmake", "strip", "dpkg-shlibdeps", "dpkg-deb"):
            mode = "dist-stop" if step == "cmake" else "dist-installer-stop"
            with self.subTest(step=step):
                if step != "cmake" and not host_uses_deb():
                    self.skipTest("installer cancellation requires Debian packaging")
                # Each subcase gets a new project and child marker set.
                self.dist_fixture(step=step)
                report = self.operate(mode)
                self.assertFalse(report["success"], report)
                self.assertTrue(report["stopped"], report)
                self.assert_children_exited()
                self.assertFalse(list((self.project / "dist").glob("*.tar.gz")))
                shutil.rmtree(self.build_dir)
                shutil.rmtree(self.base / "fake-bin")
                shutil.rmtree(self.app.parent)
                self.pids.unlink()
                self.ready.unlink()

    @unittest.skipUnless(sys.platform == "linux", "Linux packaging fixtures")
    def test_dist_stopped_preserves_previous_package_and_archive(self):
        self.dist_fixture()
        report = self.operate("dist")
        self.assertTrue(report["success"], report)
        package = next((self.project / "dist").glob("*/sample")).parent
        sentinel = package / "previous-package"
        sentinel.write_text("previous complete package", encoding="utf-8")
        archive = package.with_name(package.name + ".tar.gz")
        archive.write_text("previous complete archive", encoding="utf-8")
        before = {str(path.relative_to(self.project / "dist")): path.read_bytes()
                  for path in (self.project / "dist").rglob("*") if path.is_file()}
        for mode in ("dist-pre-stopped", "dist-pre-stopped-archive", "dist-packaging-stop"):
            with self.subTest(mode=mode):
                report = self.operate(mode)
                self.assertTrue(report["stopped"], report)
                self.assertFalse(report["success"], report)
                self.assertEqual(before, {
                    str(path.relative_to(self.project / "dist")): path.read_bytes()
                    for path in (self.project / "dist").rglob("*") if path.is_file()})
        self.assertFalse(self.ready.exists())

    @unittest.skipUnless(sys.platform == "linux", "Linux packaging fixtures")
    def test_dist_still_running_requires_force(self):
        self.dist_fixture(ignore=True)
        report = self.operate("dist-force")
        self.assertTrue(report["stillRunning"], report)
        self.assertTrue(report["appAliveBeforeForce"], report)
        self.assertTrue(report["stopped"], report)
        self.assertFalse(report["success"], report)
        self.assert_children_exited()

    @unittest.skipUnless(sys.platform == "linux", "Linux packaging fixtures")
    def test_dist_no_build_preserves_streaming_addons_and_warnings(self):
        self.dist_fixture()
        # Existing-build packaging keeps the original streaming addon reader.
        (self.project / "addons.make").write_text(
            "#" + "x" * (17 * 1024 * 1024) + "\n", encoding="utf-8")
        (self.project / "dist.json").write_text('{"unknownOption":true}', encoding="utf-8")
        report = self.operate("dist")
        self.assertTrue(report["success"], report)
        self.assertEqual(report["error"], "")
        self.assertTrue(any(event["kind"] == 1 and "unknownOption" in event["message"]
                            for event in report["events"]), report)
        self.assertIn("Packaged sample 0.1.0", report["output"])
        self.assertTrue(list((self.project / "dist").glob("*/sample")))

    @unittest.skipUnless(sys.platform == "linux", "Linux packaging fixtures")
    def test_dist_declined_fetch_stays_offline(self):
        self.env["TRUSSC_DIR"] = str(ROOT)
        self.env["TRUSSC_CACHE_DIR"] = str(self.base / "empty-cache")
        (self.project / ".trussc-version").write_text("vMissing\n", encoding="utf-8")
        report = self.operate("dist-fetch")
        self.assertTrue(report["fetchAsked"], report)
        self.assertFalse(report["success"], report)
        self.assertIn("version is unavailable", report["error"])
        self.assertFalse((self.base / "empty-cache/versions/TrussC-org/TrussC/vMissing").exists())

    @unittest.skipUnless(sys.platform == "linux", "Linux packaging fixtures")
    def test_dist_build_uses_cancel_handle(self):
        self.dist_fixture()
        report = self.operate("dist-build-stop")
        self.assertTrue(report["stopped"], report)
        self.assertFalse(report["success"], report)
        self.assert_children_exited()

    def test_run_request_stop_ends_cooperative_app(self):
        self.run_fixture()
        report = self.operate("run-stop")
        self.assertFalse(report["success"])
        self.assertTrue(report["stopped"])
        self.assertEqual(report["exitCode"], 0)
        self.assert_children_exited()
        for pid in self.pids.read_text(encoding="utf-8").split():
            self.assertTrue((self.base / ("stopped-" + pid)).exists())

    def test_run_still_running_waits_for_explicit_force(self):
        self.run_fixture(ignore=True)
        with subprocess.Popen([sys.executable, "-c", "import signal; signal.pause()"],
                              start_new_session=True) as unrelated:
            try:
                report = self.operate("run-force")
                self.assertIsNone(unrelated.poll())
            finally:
                unrelated.terminate()
                unrelated.wait()
        self.assertTrue(report["stillRunning"], report)
        self.assertTrue(report["appAliveBeforeForce"], report)
        self.assertTrue(report["stopped"])
        self.assertFalse(report["success"])
        self.assertEqual(report["exitCode"], -1)
        self.assert_children_exited()

    def test_run_retains_terminal_group_ctrl_c(self):
        self.child_fixture()
        app = self.app
        app.parent.mkdir(parents=True)
        app.write_bytes((self.base / "fake-bin/build-tool").read_bytes())
        app.chmod(0o755)
        (self.base / "fake-bin/build-tool").write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
        with subprocess.Popen([str(CLI), "run", "-p", str(self.project), "-j1"],
                              env=self.env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              start_new_session=True, text=True, encoding="utf-8") as process:
            try:
                wait_for(self.ready.exists)
                os.killpg(process.pid, signal.SIGINT)
                output, _ = process.communicate(timeout=20)
                self.assertEqual(process.returncode, -signal.SIGINT, output)
                self.assertIn("Launching sample", output)
                self.assert_children_exited()
            finally:
                if process.poll() is None:
                    os.killpg(process.pid, signal.SIGKILL)
                    process.wait()


if __name__ == "__main__":
    unittest.main()
