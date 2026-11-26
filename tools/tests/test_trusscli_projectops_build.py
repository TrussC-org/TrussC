"""Offline build operations and cancellation of owned process groups.

Run through tcbuild.sh. Conditions have bounded waits; no elapsed-time verdicts.
"""
import json
import errno
import os
from pathlib import Path
import select
import signal
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get("TRUSSCLI", ROOT / "tools/bin/trusscli")).resolve()


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
        sources = ["ProjectBuild", "ProjectOps", "ProjectGenerator", "ProjectState",
                   "BuildSetup", "BuildOutputFilter", "IdeHelper", "VsDetector",
                   "AddonVersions", "TrussCVersion", "ProcessCapture"]
        subprocess.run([os.environ.get("CXX", "c++"), "-std=c++20", "-pthread", "-Wall", "-Wextra", "-Werror",
                        "-include", str(scratch / "TrussC.h"), "-I", str(scratch),
                        "-I", str(ROOT / "tools/src"), "-I", str(ROOT / "core/include"),
                        str(ROOT / "tools/tests/projectops_build_driver.cpp"),
                        *[str(ROOT / "tools/src" / (name + ".cpp")) for name in sources],
                        "-o", str(cls.driver)], check=True)

    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix="projectops-build-")
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.preset = "macos" if sys.platform == "darwin" else "linux"
        self.project = self.base / "sample"
        self.build_dir = self.project / ("build-" + self.preset)
        (self.project / "src").mkdir(parents=True)
        (self.project / "addons.make").touch()
        (self.project / "CMakeLists.txt").write_text(
            'cmake_minimum_required(VERSION 3.20)\nproject(sample LANGUAGES CXX)\n'
            'add_executable(sample src/main.cpp)\n'
            'set_target_properties(sample PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin")\n',
            encoding="utf-8")
        (self.project / "src/main.cpp").write_text('int main() { return 0; }\n', encoding="utf-8")
        (self.project / "CMakePresets.json").write_text(json.dumps({
            "version": 3, "configurePresets": [{"name": self.preset, "generator": "Unix Makefiles",
            "binaryDir": "${sourceDir}/build-" + self.preset, "cacheVariables": {"FETCHCONTENT_FULLY_DISCONNECTED": "ON"}}],
            "buildPresets": [{"name": self.preset, "configurePreset": self.preset}]}), encoding="utf-8")
        self.env = dict(os.environ)

    def operate(self, mode="build", *args):
        result = subprocess.run([str(self.driver), mode, str(self.project), *args],
                                env=self.env, capture_output=True, text=True, encoding="utf-8", timeout=40)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stderr, "")
        report = json.loads(result.stdout)
        self.assertTrue(report["cwdRestored"])
        self.assertTrue(report["cwdUnchangedDuringProcess"])
        if mode != "noisy":
            self.assertEqual(report["logs"], [event["message"] for event in report["events"]])
        return report

    def test_configure_build_type_and_compile_failure(self):
        report = self.operate("build", "Release")
        self.assertTrue(report["success"], report)
        self.assertIn("Built target sample", report["output"])
        self.assertTrue((self.project / "bin/sample").is_file())
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

    def test_run_retains_terminal_group_ctrl_c(self):
        self.child_fixture()
        app = self.project / ("bin/sample.app/Contents/MacOS/sample"
                              if sys.platform == "darwin" else "bin/sample")
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
