"""Offline warning formats, precise exceptions, capture and CI wiring."""
import importlib.util
import os
import ast
import shutil
import runpy
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch
from types import SimpleNamespace

import yaml

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / 'tools/check_build_warnings.py'
spec = importlib.util.spec_from_file_location('check_build_warnings', SCRIPT)
gate = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gate)
cache_spec = importlib.util.spec_from_file_location('verify_warning_cache', ROOT / 'tools/verify_warning_cache.py')
cache = importlib.util.module_from_spec(cache_spec)
cache_spec.loader.exec_module(cache)


class WarningTests(unittest.TestCase):
    def test_diagnostic_formats(self):
        for line in [
            'file.cpp:12:3: warning: unused result [-Wunused-result]',
            'file.cpp(42): warning C4244: conversion loses data',
            'CMake Warning (dev) at CMakeLists.txt:3 (FetchContent):',
            'CMake Deprecation Warning at CMakeLists.txt:1 (cmake_minimum_required):',
            "libtool: warning: 'tcKeepScreenOn.o' has no symbols",
            'ld: warning: duplicate libraries: -lc++',
            'ld: warning duplicate library -lc++',
            'ld: fatal warning(s) induced error (-fatal_warnings)',
            'LINK : warning LNK4099: missing PDB',
            'LINK : warning LNK4221: no public symbols found',
            'lld-link: warning: undefined symbol: optional_function',
            'ld.lld: warning: cannot find entry symbol _start',
            'ld64.lld: warning: directory not found for option -Lmissing',
            'ld: warning: ignoring duplicate libraries: -lc++',
            'libtool: file: empty.o has no symbols',
            'libtool: warning same member name (same.o) in output file used for input files: a.o and: b.o',
            'libtool: warning for architecture: arm64 same member name (same.o) in output file used for input files: a.o and: b.o',
            'ranlib: warning for architecture: x86_64 file: empty.o has no symbols',
            'libtool: for architecture: x86_64 warning same member name (same.o)',

            '/Applications/Xcode.app/Toolchains/usr/bin/libtool: file: lib.a(empty.o) has no symbols',
            'ranlib: file: lib.a(empty.o) has no symbols',
            'libtool: for architecture: arm64 file: empty.o has no symbols',
            "libtool: duplicate member name 'same.o' from 'a.a(same.o)' and 'b.a(same.o)'",
            "libtool: for architecture: x86_64 duplicate member name 'same.o' from 'a.o' and 'b.o'",
            "ranlib: duplicate member name 'same.o' from 'a.o' and 'b.o'",
            'emcc: warning: ignored option [-Wunused-command-line-argument]',
            '\x1b[33mfile.cpp:1: warning: colored\x1b[0m',
        ]:
            with self.subTest(line=line):
                self.assertEqual(len(gate.scan([line], [])), 1)
        self.assertFalse(gate.scan(['0 warnings generated.', '[100%] Built target app',
                                    'old connection queues its warning: PASS',
                                    'callback rejection has first-loss Warning: PASS'], []))

    def test_android_stubs_are_not_exempt(self):
        entries = gate.load_allowlist(gate.ALLOWLIST)
        self.assertEqual(entries, [])
        for filename, message in [
            ('tcFileDialog', 'JNI Intent implementation'),
            ('tcSound', 'MediaCodec or FFmpeg AAC decoding'),
            ('tcVideoPlayer', 'MediaCodec NDK implementation'),
            ('tcVideoGrabber', 'Camera2 NDK implementation'),
        ]:
            source = filename + '_android.cpp'
            line = (f'/checkout/core/platform/android/{source}:11:9: warning: '
                    f'STUB: {source} — needs {message} [-W#pragma-messages]')
            with self.subTest(source=source):
                self.assertTrue(gate.scan([line], entries))
                self.assertTrue(gate.scan([line.replace('/', '\\')], entries))
        self.assertTrue(gate.scan(['other.cpp:2: warning: new diagnostic'], entries))

    def test_capture_preserves_output_status_and_gate_fails_closed(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            log = Path(directory) / 'build.log'
            result = subprocess.run([sys.executable, str(SCRIPT), '--capture', str(log), '--',
                                     sys.executable, '-c',
                                     'import sys; print("out"); print("x.cpp:1: warning: injected", file=sys.stderr); sys.exit(7)'],
                                    capture_output=True)
            self.assertEqual(result.returncode, 7)
            self.assertEqual(result.stdout, log.read_bytes())
            result = subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True)
            self.assertEqual(result.returncode, 1)
            self.assertIn(b'warning: injected', result.stdout)
            log.write_text('Built target app\n', encoding='utf-8')
            self.assertEqual(subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True).returncode, 0)
            log.unlink()
            self.assertEqual(subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True).returncode, 1)

    def test_capture_bounds_inherited_pipe_wait(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as directory:
            log = Path(directory) / 'build.log'
            pidfile = Path(directory) / 'grandchild.pid'
            grandchild = ('import time; time.sleep(.2); '
                          'print("late.cpp:2: warning: inherited pipe", flush=True); '
                          'time.sleep(60)')
            child = (
                'import subprocess,sys; from pathlib import Path; '
                f'p=subprocess.Popen([sys.executable,"-c",{grandchild!r}], '
                'stdin=subprocess.DEVNULL); '
                'Path(sys.argv[1]).write_text(str(p.pid)); '
                'sys.stdout.write("x.cpp:1: warning: before exit\\npartial output"); '
                'sys.stdout.flush(); sys.exit(7)')
            started = time.monotonic()
            try:
                result = subprocess.run(
                    [sys.executable, str(SCRIPT), '--capture', str(log), '--',
                     sys.executable, '-c', child, str(pidfile)],
                    capture_output=True, timeout=10)
                self.assertLess(time.monotonic() - started, 8)
                self.assertEqual(result.returncode, 7)
                self.assertIn(b'build output capture incomplete: a background process kept the output open',
                              result.stderr)
                self.assertIn(b'partial output', log.read_bytes())
                self.assertIn(b'warning: before exit', result.stdout)
                self.assertIn(b'warning: inherited pipe', result.stdout)
                checked = subprocess.run([sys.executable, str(SCRIPT), str(log)],
                                         capture_output=True, timeout=10)
                self.assertEqual(checked.returncode, 1)
                self.assertIn(b'warning: before exit', checked.stdout)
            finally:
                if pidfile.exists():
                    pid = int(pidfile.read_text(encoding='utf-8'))
                    if os.name == 'nt':
                        subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
                    else:
                        import signal
                        os.kill(pid, signal.SIGTERM)

    def test_capture_incomplete_log_fails_without_any_warning(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'build.log'
            pidfile = Path(directory) / 'grandchild.pid'
            grandchild = ('import time; time.sleep(1.5); '
                          'print("late.cpp:1: warning: hidden", flush=True); time.sleep(60)')
            child = ('import subprocess,sys; from pathlib import Path; '
                     f'p=subprocess.Popen([sys.executable,"-c",{grandchild!r}]); '
                     'Path(sys.argv[1]).write_text(str(p.pid), encoding="utf-8")')
            try:
                result = subprocess.run([sys.executable, str(SCRIPT), '--capture', str(log), '--',
                                         sys.executable, '-c', child, str(pidfile)],
                                        capture_output=True, timeout=10)
                self.assertNotEqual(result.returncode, 0)
                notice = b'build output capture incomplete: a background process kept the output open'
                self.assertIn(notice, result.stderr)
                self.assertIn(notice, log.read_bytes())
                self.assertNotIn(b'warning:', log.read_bytes())
                checked = subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True)
                self.assertEqual(checked.returncode, 1)
                self.assertIn(notice, checked.stdout)
            finally:
                if pidfile.exists():
                    pid = int(pidfile.read_text(encoding='utf-8'))
                    if os.name == 'nt':
                        subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
                    else:
                        import signal
                        os.kill(pid, signal.SIGTERM)

    def test_capture_bounds_continuously_writing_descendant(self):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'build.log'
            pidfile = Path(directory) / 'grandchild.pid'
            grandchild = ('import time; '
                          '\nwhile True: print("background output", flush=True); time.sleep(.01)')
            child = ('import subprocess,sys; from pathlib import Path; '
                     f'p=subprocess.Popen([sys.executable,"-c",{grandchild!r}]); '
                     'Path(sys.argv[1]).write_text(str(p.pid), encoding="utf-8"); '
                     'print("direct child final output", flush=True)')
            started = time.monotonic()
            try:
                result = subprocess.run([sys.executable, str(SCRIPT), '--capture', str(log), '--',
                                         sys.executable, '-c', child, str(pidfile)],
                                        capture_output=True, timeout=10)
                self.assertLess(time.monotonic() - started, 8)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(b'direct child final output', log.read_bytes())
                self.assertIn(b'build output capture incomplete:', log.read_bytes())
                checked = subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True)
                self.assertEqual(checked.returncode, 1)
            finally:
                if pidfile.exists():
                    pid = int(pidfile.read_text(encoding='utf-8'))
                    if os.name == 'nt':
                        subprocess.run(['taskkill', '/PID', str(pid), '/F'], capture_output=True)
                    else:
                        import signal
                        try:
                            os.kill(pid, signal.SIGTERM)
                        except ProcessLookupError:
                            pass

    def test_capture_delayed_read_and_exit_burst_are_complete(self):
        # Delay a chunk after the kernel read, reproducing reader descheduling.
        # The capture must hand it over and drain the rest through EOF.
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'build.log'
            original = os.read
            for blocks, diagnostic in ((1, 'Built target app'),
                                       (1, 'burst.cpp:1: warning: at exit'),
                                       (256, 'Built target app'),
                                       (256, 'burst.cpp:1: warning: at exit')):
                delayed = False

                def read(fd, size):
                    nonlocal delayed
                    in_reader = gate.threading.current_thread() is not gate.threading.main_thread()
                    chunk = original(fd, min(size, 512) if in_reader else size)
                    if (chunk and not delayed and
                            gate.threading.current_thread() is not gate.threading.main_thread()):
                        delayed = True
                        time.sleep(1.5)
                    return chunk

                expected = (('x' * 1024 + '\n') * blocks + diagnostic + '\n').encode()
                command = [sys.executable, '-c',
                           f'import sys; sys.stdout.write(("x" * 1024 + "\\n") * {blocks} + '
                           f'{diagnostic!r} + "\\n"); sys.stdout.flush()']
                # Redirect the echo to a file to keep the test's own log small.
                with (Path(directory) / 'echo.log').open('wb') as echo:
                    with patch.object(gate.os, 'read', read), patch.object(gate.sys, 'stdout',
                                                                          SimpleNamespace(buffer=echo)):
                        self.assertEqual(gate.capture(log, command), 0)
                self.assertTrue(delayed)
                self.assertEqual(log.read_bytes(), expected)
                checked = subprocess.run([sys.executable, str(SCRIPT), str(log)], capture_output=True)
                self.assertEqual(checked.returncode, int('warning:' in diagnostic))

    def test_windows_ci_disables_gui_launch_without_rewriting_batch(self):
        for name in ('build', 'daily'):
            workflow = yaml.safe_load((ROOT / f'.github/workflows/{name}.yml').read_text(encoding='utf-8'))
            invocations = 0
            for job in workflow['jobs'].values():
                for step in job.get('steps', []):
                    script = step.get('run', '')
                    for line in script.splitlines():
                        if 'build_win.bat' in line:
                            # The only allowed reference is an invocation of
                            # the original script with the supported switch.
                            self.assertEqual(line.strip(), 'TC_NO_LAUNCH=1 ./tools/build_win.bat')
                            invocations += 1
            self.assertEqual(invocations, 1, name)
        batch = (ROOT / 'tools/build_win.bat').read_bytes()
        self.assertEqual(batch.count(b'\n'), batch.count(b'\r\n'))
        self.assertIn(b'if not defined TC_NO_LAUNCH (\r\n', batch)
        self.assertIn(b'    start "" "%SCRIPT_DIR%\\trusscli.exe"\r\n)', batch)
        self.assertIn(b') else (\r\n    REM The unattended PATH prompt may have reached EOF after a successful build.\r\n    exit /b 0\r\n)', batch)
        attributes = subprocess.check_output(['git', 'check-attr', 'eol', '--', 'tools/build_win.bat'])
        self.assertIn(b'eol: crlf', attributes)

    def test_runtime_output_never_enters_build_log(self):
        # Exercise the actual mixed build/run boundary. Even marker-like text
        # from a test cannot change which bytes enter the build-only file.
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            log = Path(temporary) / 'build.log'
            for warning_in_build in (False, True):
                source = (
                    f'import sys; sys.path.insert(0, {str(ROOT / "tools")!r}); '
                    'from check_build_warnings import run_build; '
                    'print("BEGIN TEST RUN\\nBEGIN BUILD\\nfile:1: warning: diagnostic\\nEND BUILD\\nEND TEST RUN", flush=True); '
                    'run_build([sys.executable, "-c", '
                    + repr('print("file:1: warning: diagnostic")' if warning_in_build
                           else 'print("Built target app")') + '])')
                result = subprocess.run([sys.executable, str(SCRIPT), '--capture-builds',
                                         str(log), '--', sys.executable, '-c', source],
                                        capture_output=True, text=True, encoding='utf-8')
                self.assertIn('BEGIN TEST RUN', result.stdout)
                self.assertIn('file:1: warning: diagnostic', result.stdout)
                self.assertNotIn('BEGIN BUILD', log.read_text(encoding='utf-8'))
                self.assertEqual('file:1: warning: diagnostic' in log.read_text(encoding='utf-8'),
                                 warning_in_build)
                verdict = subprocess.run([sys.executable, str(SCRIPT), str(log)],
                                         capture_output=True)
                self.assertEqual(verdict.returncode, int(warning_in_build))
                self.assertEqual(bool(result.returncode), warning_in_build)

    def test_build_helper_preserves_failure_and_checks_captured_output(self):
        for output in ('stdout', 'stderr'):
            with self.subTest(output=output):
                with self.assertRaisesRegex(RuntimeError, 'unexpected warning'):
                    gate.run_build([sys.executable, '-c',
                                    f'import sys; print("file:1: warning: injected", file=sys.{output})'],
                                   echo=False, capture_output=True)
        with self.assertRaisesRegex(RuntimeError, 'unexpected warning'):
            gate.run_build([sys.executable, '-c',
                            'import sys; sys.stdout.write("progress"); sys.stderr.write("warning: partial line")'],
                           echo=False, capture_output=True)
        with self.assertRaises(subprocess.CalledProcessError) as error:
            gate.run_build([sys.executable, '-c', 'raise SystemExit(7)'], check=True)
        self.assertEqual(error.exception.returncode, 7)

    @unittest.skipUnless(sys.platform == 'linux', 'requires Linux address-space resource limit')
    def test_large_pipe_output_has_bounded_memory_and_catches_middle_warning(self):
        # Each pass emits 200 MiB; consumer and producer both have an 80 MiB limit.
        with tempfile.TemporaryDirectory() as temporary:
            log = Path(temporary) / 'build.log'
            source = r"""
import os, resource, sys
resource.setrlimit(resource.RLIMIT_AS, (80 * 1024**2, 80 * 1024**2))
sys.path.insert(0, sys.argv[1])
from check_build_warnings import run_build, OUTPUT_LIMIT
producer = r'''import os
line = b'x' * 4095 + b'\n'
for i in range(51200):
    if i == 25600 and {warning}:
        os.write(1, b'file:1: warning: middle diagnostic\n')
    os.write(1, line)
'''
for warning in (False, True):
    try:
        result = run_build([sys.executable, '-c', producer.format(warning=warning)], echo=False)
    except RuntimeError as error:
        assert warning and 'middle diagnostic' in str(error), str(error)
    else:
        assert not warning and result.returncode == 0
        assert len(result.stdout) <= OUTPUT_LIMIT
    assert os.stat(os.environ['TC_BUILD_WARNING_LOG']).st_size >= 200 * 1024**2
    os.unlink(os.environ['TC_BUILD_WARNING_LOG'])
print('200 MiB clean and middle-warning passes under 80 MiB RLIMIT_AS')
"""
            result = subprocess.run([sys.executable, '-c', source, str(ROOT / 'tools')],
                                    env=dict(os.environ, TC_BUILD_WARNING_LOG=str(log)),
                                    capture_output=True, text=True, encoding='utf-8', timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_streams_and_flushes_before_child_exit(self):
        with tempfile.TemporaryDirectory() as temporary:
            marker = Path(temporary) / 'continue'
            log = Path(temporary) / 'build.log'
            child = (f'import pathlib,time; print("first line", flush=True); '
                     f'p=pathlib.Path({str(marker)!r}); '
                     '\nwhile not p.exists(): time.sleep(.01)\nprint("last line")')
            source = (f'import sys; sys.path.insert(0, {str(ROOT / "tools")!r}); '
                      'from check_build_warnings import run_build; '
                      f'run_build([sys.executable, "-c", {child!r}], timeout=10)')
            with subprocess.Popen([sys.executable, '-c', source], stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE, text=True, encoding='utf-8',
                                  env=dict(os.environ, TC_BUILD_WARNING_LOG=str(log))) as process:
                try:
                    self.assertEqual(process.stdout.readline(), 'first line\n')
                    self.assertIn('first line', log.read_text(encoding='utf-8'))
                    marker.touch()
                    output, error = process.communicate(timeout=15)
                    self.assertEqual(process.returncode, 0, error)
                    self.assertIn('last line', output)
                finally:
                    if process.poll() is None:
                        process.kill()

    def test_oversized_line_fails_closed_and_timeout_is_preserved(self):
        with self.assertRaisesRegex(RuntimeError, 'cannot certify'):
            gate.run_build([sys.executable, '-c', 'print("x" * 200000)'], echo=False)
        with self.assertRaises(subprocess.TimeoutExpired):
            gate.run_build([sys.executable, '-c', 'import time; time.sleep(10)'],
                           echo=False, timeout=.1)

    def test_guest_rebuild_and_ci_fixture_builds_use_shared_scanner(self):
        # Include the daily display path explicitly: a build inventory based
        # only on workflow step names missed this subprocess chain before.
        daily = (ROOT / '.github/workflows/daily.yml').read_text(encoding='utf-8')
        self.assertIn('python3 tools/run_core_display_tests.py', daily)
        workflow = yaml.load(daily, Loader=yaml.BaseLoader)
        steps = workflow['jobs']['sweep']['steps']
        display_index = next(i for i, step in enumerate(steps) if step.get('id') == 'display_tests')
        self.assertIn('--capture-builds "$RUNNER_TEMP/daily_display_builds.log"', steps[display_index]['run'])
        check = steps[display_index + 1]
        self.assertEqual(check['if'], "always() && steps.display_tests.outcome != 'skipped'")
        self.assertIn('check_build_warnings.py "$RUNNER_TEMP/daily_display_builds.log"', check['run'])
        host = (ROOT / 'core/include/tc/app/tcHotReloadHost.h').read_text(encoding='utf-8')
        self.assertIn('std::getenv("TC_BUILD_WARNING_LOG")', host)
        self.assertIn('<< output', host)
        self.assertIn('test_shader_rebuild.py {test}',
                      (ROOT / 'core/tests/hotReloadLifecycle/display-test').read_text(encoding='utf-8'))
        inventory = {
            'core/tests/hotReloadLifecycle/test_shader_rebuild.py',
            'examples/build_all.py', 'tools/render_regression.py',
            'tools/tests/test_windows_header_dependencies.py', 'tools/tests/test_trusscli_writes.py',
            'tools/tests/test_fetchcontent_cache.py', 'tools/tests/test_shared_build_regen.py',
            'tools/tests/test_shader_targets.py', 'tools/tests/test_core_test_build_steps.py',
            'tools/tests/test_release_workflow.py', 'tools/tests/test_reload_check_exit.py',
            'core/tests/metalUniformGrowth/test_backend.py',
            'core/tests/mouseReleaseLost/test_locked_replay.py',
            'core/tests/serialRxBuffer/probes/run.py', 'core/tests/webgpuUniformGrowth/run.py',
        }
        for filename in sorted(inventory):
            with self.subTest(script=filename):
                tree = ast.parse((ROOT / filename).read_text(encoding='utf-8'))
                self.assertTrue(any(isinstance(node, ast.ImportFrom) and
                                    node.module == 'check_build_warnings' and
                                    any(name.name == 'run_build' for name in node.names)
                                    for node in ast.walk(tree)))
                if filename == 'core/tests/serialRxBuffer/probes/run.py':
                    result = subprocess.run([sys.executable, str(ROOT / filename), '--help'],
                                            capture_output=True)
                    self.assertEqual(result.returncode, 0, result.stderr)
                else:
                    runpy.run_path(str(ROOT / filename))
        # Check the real guest call site, including the restoration build.
        path = ROOT / 'core/tests/hotReloadLifecycle/test_shader_rebuild.py'
        tree = ast.parse(path.read_text(encoding='utf-8'))
        build = next(node for node in ast.walk(tree)
                     if isinstance(node, ast.FunctionDef) and node.name == 'build_guest')
        calls = [node for node in ast.walk(build) if isinstance(node, ast.Call)]
        self.assertTrue(any(isinstance(node.func, ast.Name) and node.func.id == 'run_build'
                            for node in calls))
        self.assertFalse(any(isinstance(node.func, ast.Attribute) and
                             isinstance(node.func.value, ast.Name) and
                             node.func.value.id == 'subprocess' for node in calls))
        regen = (ROOT / 'docs/reference/regenerate.sh').read_text(encoding='utf-8')
        self.assertEqual(regen.count('check_build_warnings.py" --run-build -- cmake'), 2)

    @unittest.skipUnless(sys.platform != 'win32' and shutil.which('c++'), 'requires a host C++ compiler')
    def test_runtime_host_build_capture_uses_build_bytes_and_preserves_status(self):
        # Compile the actual host build function with only its process/Logger
        # dependencies stubbed. This checks the C++ -> Python log boundary.
        source = (ROOT / 'core/include/tc/app/tcHotReloadHost.h').read_text(encoding='utf-8')
        function = source[source.index('inline int runBuildCommand('):
                          source.index('// Hot reload host — manages')]
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            directory = Path(temporary)
            cpp = directory / 'capture.cpp'
            cpp.write_text(r'''#include <string>
#include <vector>
#include <fstream>
#include <cstdlib>
#include <iostream>
#include <cstdint>
#include <new>
bool failAllocation = false, failDiagnostic = false;
void* operator new(std::size_t size) {
    if (failAllocation) { failAllocation = false; throw std::bad_alloc(); }
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
using std::string;
using std::vector;
struct Loss { uint64_t truncatedBuilds = 0, unavailableBuilds = 0; };
namespace internal {
Loss& buildOutputLoss() { static Loss loss; return loss; }
int runPosixBuildWithOutput(const vector<string>& argv, string& output) {
    output = "file:1: warning: guest build\n";
    if (argv[0] == "truncate") ++buildOutputLoss().truncatedBuilds;
    if (argv[0] == "throw" || argv[0] == "throw-report") failAllocation = true;
    failDiagnostic = argv[0] == "throw-report";
    return argv[0] == "fail" ? 7 : 0;
}
}
std::ostream& logError(const char*) {
    if (failDiagnostic) throw std::bad_alloc();
    return std::cerr;
}
''' + function + r'''
int main(int argc, char** argv) {
    if (argc != 2) return 99;
    std::cout << "runtime:2: warning: application message\n";
    string output;
    return runBuildCommand({argv[1]}, output);
}
''', encoding='utf-8')
            binary = directory / 'capture'
            compiler = (['ccache'] if shutil.which('ccache') else []) + ['c++']
            gate.run_build(compiler + ['-std=c++20', '-Wall', '-Wextra', str(cpp), '-o', str(binary)],
                           check=True, echo=False, capture_output=True)
            for mode, status in [('ok', 0), ('fail', 7), ('truncate', 255)]:
                log = directory / (mode + '.log')
                result = subprocess.run([str(binary), mode],
                                        env=dict(os.environ, TC_BUILD_WARNING_LOG=str(log)),
                                        capture_output=True)
                self.assertEqual(result.returncode, status)
                output = log.read_text(encoding='utf-8')
                self.assertNotIn('application message', output)
                self.assertTrue(gate.scan(output.splitlines(), []))
            for mode in ('throw', 'throw-report'):
                result = subprocess.run([str(binary), mode],
                                        env=dict(os.environ, TC_BUILD_WARNING_LOG=str(directory / 'throw.log')),
                                        capture_output=True)
                self.assertEqual(result.returncode, 255, result.stderr)
                if mode == 'throw':
                    self.assertIn(b'Cannot write build log', result.stderr)
                env = dict(os.environ)
                env.pop('TC_BUILD_WARNING_LOG', None)
                result = subprocess.run([str(binary), mode], env=env, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
            result = subprocess.run([str(binary), 'ok'],
                                    env=dict(os.environ, TC_BUILD_WARNING_LOG=str(directory / 'missing/log')),
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)

    @unittest.skipUnless(sys.platform != 'win32' and shutil.which('c++'), 'requires a host C++ compiler')
    def test_bounded_capture_keeps_head_and_tail_across_chunk_sizes(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temporary:
            directory = Path(temporary)
            cpp = directory / 'ring.cpp'
            cpp.write_text(r'''#include "tc/app/internal/tcBuildOutputCapture.h"
#include <cassert>
int main() {
    using trussc::internal::BuildOutputCapture;
    const size_t edge = BuildOutputCapture::endBytes;
    std::string input(edge * 7 + 53, ' ');
    for (size_t i = 0; i < input.size(); ++i) input[i] = static_cast<char>('!' + i % 89);
    const auto expected = input.substr(0, edge) + "\n… " +
        std::to_string(input.size() - 2 * edge) + " bytes omitted …\n" +
        input.substr(input.size() - edge);
    for (size_t chunk : {size_t{1}, size_t{4096}, edge, edge + 1, edge * 3}) {
        BuildOutputCapture capture;
        for (size_t offset = 0; offset < input.size();) {
            const size_t count = (std::min)(chunk, input.size() - offset);
            capture.append(input.data() + offset, count);
            offset += count;
        }
        assert(capture.text() == expected);
        assert(capture.omittedBytes() == input.size() - edge * 2);
    }
}
''', encoding='utf-8')
            binary = directory / 'ring'
            compiler = (['ccache'] if shutil.which('ccache') else []) + ['c++']
            gate.run_build(compiler + ['-std=c++20', '-O3', '-D_FORTIFY_SOURCE=2',
                                      '-Wall', '-Wextra', '-I' + str(ROOT / 'core/include'),
                                      str(cpp), '-o', str(binary)],
                           check=True, echo=False, capture_output=True)
            subprocess.run([str(binary)], check=True)

    def test_cache_probe_requires_warning_and_actual_hit(self):
        def compile_result(command, **kwargs):
            output_arg = command[-1]
            if output_arg.startswith('/Fo'):
                output_arg = output_arg[3:]
            Path(output_arg).write_bytes(b'object')
            return SimpleNamespace(returncode=0, stdout=cache.MARKER)
        with patch.object(cache.shutil, 'which', return_value='compiler'), \
             patch.object(cache.subprocess, 'run', side_effect=compile_result), \
             patch.object(cache, 'hits', side_effect=[0, 0, 0, 1]):
            self.assertEqual(cache.verify('ccache'), 0)
        with patch.object(cache.shutil, 'which', return_value='compiler'), \
             patch.object(cache.subprocess, 'run', side_effect=compile_result), \
             patch.object(cache, 'hits', return_value=0):
            with self.assertRaisesRegex(RuntimeError, 'not a cache hit'):
                cache.verify('ccache')
        with patch.object(cache.shutil, 'which', return_value='compiler'), \
             patch.object(cache.subprocess, 'run', return_value=SimpleNamespace(returncode=0, stdout='')), \
             patch.object(cache, 'hits', return_value=1):
            with self.assertRaisesRegex(RuntimeError, 'did not replay'):
                cache.verify('sccache')

    def test_cache_stats_formats(self):
        with patch.object(cache.subprocess, 'check_output', return_value='direct_cache_hit 12\npreprocessed_cache_hit 3\ncache_miss 8\n'):
            self.assertEqual(cache.hits('ccache'), 15)
        with patch.object(cache.subprocess, 'check_output', return_value='{"stats":{"cache_hits":{"C/C++":5,"Rust":0}}}'):
            self.assertEqual(cache.hits('sccache'), 5)

    def test_workflow_checks_each_build_even_on_failure(self):
        for filename, expected in [('build.yml', 13), ('daily.yml', 7)]:
            workflow = yaml.load((ROOT / '.github/workflows' / filename).read_text(encoding='utf-8'),
                                 Loader=yaml.BaseLoader)
            count = 0
            for name, job in workflow['jobs'].items():
                steps = job.get('steps', [])
                for index, step in enumerate(steps):
                    if not step.get('name', '').startswith(('Build ', 'Compare merge-base')):
                        continue
                    count += 1
                    with self.subTest(workflow=filename, job=name, step=step['name']):
                        mixed = any(script in step['run'] for script in
                                    ('examples/build_all.py', 'tools/render_regression.py'))
                        option = '--capture-builds' if mixed else '--capture'
                        self.assertIn(option + ' "$RUNNER_TEMP/', step['run'])
                        self.assertNotIn('--warnings', step['run'])
                        self.assertNotIn('continue-on-error', step)
                        check = steps[index + 1]
                        self.assertIn(f"steps.{step['id']}.outcome != 'skipped'", check['if'])
                        self.assertTrue(check['if'].startswith('always() && '))
                        self.assertIn('check_build_warnings.py "$RUNNER_TEMP/', check['run'])
                        log = step['run'].split(option + ' "', 1)[1].split('"', 1)[0]
                        self.assertIn(f'"{log}"', check['run'])
                        self.assertNotIn('continue-on-error', check)
                        # The wrapper must preserve heredocs and shell syntax.
                        shell = subprocess.run(['bash', '-n'], input=step['run'], text=True,
                                               encoding='utf-8', capture_output=True)
                        self.assertEqual(shell.returncode, 0, shell.stderr)
            self.assertEqual(count, expected, filename)
            if filename == 'daily.yml':
                platforms = workflow['jobs']['sweep']['strategy']['matrix']['include']
                self.assertEqual({p['preset'] for p in platforms}, {'linux', 'macos', 'windows'})
                self.assertIn('sweep-web', workflow['jobs'])

    @unittest.skipUnless(sys.platform != 'win32' and shutil.which('c++'), 'requires POSIX and C++')
    def test_apple_system_sink_does_not_enter_inherited_libtrace_after_fork(self):
        # Compile the actual Apple sink on Linux with a faulting OS boundary.
        # libtrace cannot be safely entered in a pre-exec fork child; such a
        # failure cannot be recovered by the build capture's C++ catch blocks.
        source = (ROOT / 'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
        owner = source[source.index('constinit std::atomic<bool> systemLogForked'):].split('\n}\n#endif', 1)[0]
        start = source.index('void writeSystemLog(const LogEventArgs& e) {')
        end = source.index('\n}', start) + 2
        function = source[start:end]
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            cpp = directory / 'sink.cpp'
            cpp.write_text(r'''#include <string>
#include <cstdlib>
#include <unistd.h>
#include <sys/wait.h>
#include <atomic>
#include <pthread.h>
#include <thread>
#define __APPLE__ 1
#define TARGET_OS_IPHONE 0
#define OS_LOG_TYPE_DEBUG 0
#define OS_LOG_TYPE_ERROR 1
#define OS_LOG_TYPE_FAULT 2
#define OS_LOG_TYPE_DEFAULT 3
using os_log_t = int;
using os_log_type_t = int;
enum class LogLevel { Verbose, Notice, Warning, Error, Fatal };
struct LogEventArgs { LogLevel level; std::string message; };
const char* logLevelToString(LogLevel) { return "WARNING"; }
bool osLogMirroredToConsole() { return false; }
constinit std::atomic<int> calls{0};
pid_t parentProcess() { static const pid_t parent = getpid(); return parent; }
void writeSystemLog(const LogEventArgs&);
// Deliberately before the production guard declarations/initializers.
struct EarlyLog {
    EarlyLog() {
        if (std::getenv("TC_TEST_EARLY_LOG"))
            writeSystemLog({LogLevel::Warning, "before owner initialization"});
    }
} earlyLog;
os_log_t os_log_create(const char*, const char*) {
    if (getpid() != parentProcess()) _exit(90);
    return 1;
}
void os_log_with_type(os_log_t, os_log_type_t, const char*, const char*, const char*) {
    if (getpid() != parentProcess()) _exit(91);
    ++calls;
}
''' + owner + '\n' + function + r'''
int main() {
    LogEventArgs warning{LogLevel::Warning, "forwarding failed"};
    parentProcess(); // Other libraries can initialize libtrace before any TrussC log.
    const bool early = std::getenv("TC_TEST_EARLY_LOG") != nullptr;
    if (early) writeSystemLog(warning);
    const pid_t child = fork();
    if (!child) { writeSystemLog(warning); writeSystemLog(warning); _exit(0); }
    int status = 0;
    if (child < 0 || waitpid(child, &status, 0) != child ||
        !WIFEXITED(status) || WEXITSTATUS(status)) return 1;
    writeSystemLog(warning);
    return calls == (early ? 3 : 1) ? 0 : 2;
}
''', encoding='utf-8')
            fixed = cpp.read_text(encoding='utf-8')
            guard = function[function.index('    if (systemLogForked'):function.index('#if !TARGET_OS_IPHONE')]

            def probe(name, code, expected):
                cpp.write_text(code, encoding='utf-8')
                gate.run_build(['c++', '-std=c++20', '-Wall', '-Wextra', '-Werror',
                                '-pthread', str(cpp), '-o', str(directory / name)], echo=False, check=True)
                for early, status in expected:
                    env = dict(os.environ)
                    env.pop('TC_TEST_EARLY_LOG', None)
                    if early:
                        env['TC_TEST_EARLY_LOG'] = '1'
                    result = subprocess.run([str(directory / name)], env=env, capture_output=True)
                    self.assertEqual(result.returncode, status, (name, early, result.stderr))

            # First log in a fork child, and a parent log even before our
            # load-time initializer: both must work in the same production code.
            probe('fixed', fixed, [(False, 0), (True, 0)])
            # Start logging on a worker before the production load-time guard,
            # and keep reading the PID while its constructor publishes it.
            concurrent = fixed.replace('struct EarlyLog {',
                'constinit std::atomic<bool> stopWorker{false};\n'
                'std::thread worker;\nstruct EarlyLog {').replace(
                '        if (std::getenv("TC_TEST_EARLY_LOG"))\n'
                '            writeSystemLog({LogLevel::Warning, "before owner initialization"});',
                '        parentProcess();\n'
                '        worker = std::thread([] {\n'
                '            while (!stopWorker.load(std::memory_order_relaxed))\n'
                '                writeSystemLog({LogLevel::Warning, "concurrent initialization"});\n'
                '        });\n'
                '        while (calls.load(std::memory_order_relaxed) == 0) std::this_thread::yield();')
            concurrent = concurrent[:concurrent.index('int main() {')] + '''
int main() {
    stopWorker.store(true, std::memory_order_relaxed);
    worker.join();
    return calls.load(std::memory_order_relaxed) > 0 ? 0 : 2;
}
'''
            probe('concurrent', concurrent, [(False, 0)])
            # A failed atfork registration still has the PID fallback.
            probe('pid_fallback', fixed.replace(
                'pthread_atfork(nullptr, nullptr, systemLogForkChild);',
                '(void)systemLogForkChild;'), [(False, 0), (True, 0)])
            # Old dynamic PID initialization incorrectly drops the early log.
            legacy = fixed.replace(owner, 'const pid_t systemLogProcess = getpid();').replace(
                guard, '    if (getpid() != systemLogProcess) return;\n')
            probe('legacy', legacy, [(True, 2)])
            # The reviewed lazy registration misses a fork before any log.
            lazy_owner = '''constinit std::atomic<bool> systemLogForked{false};
constinit pthread_once_t systemLogForkRegistration = PTHREAD_ONCE_INIT;
void systemLogForkChild() { systemLogForked.store(true, std::memory_order_relaxed); }
void registerSystemLogForkGuard() { pthread_atfork(nullptr, nullptr, systemLogForkChild); }
'''
            lazy = fixed.replace(owner, lazy_owner).replace(guard,
                '    pthread_once(&systemLogForkRegistration, registerSystemLogForkGuard);\n'
                '    if (systemLogForked.load(std::memory_order_relaxed)) return;\n')
            probe('lazy', lazy, [(False, 1), (True, 0)])
            # Fault injection is live at both os_log entry points.
            probe('unguarded', fixed.replace(guard, ''), [(False, 1), (True, 1)])

    def test_wrapped_workflow_steps_use_the_running_shell(self):
        # Python's Windows PATH search can choose the System32 WSL launcher
        # for bare bash, even though the step itself runs in Git Bash.
        import shlex
        for filename, expected in [('build.yml', 13), ('daily.yml', 8)]:
            workflow = yaml.load((ROOT / '.github/workflows' / filename).read_text(encoding='utf-8'),
                                 Loader=yaml.BaseLoader)
            wrapped = 0
            for job in workflow['jobs'].values():
                for step in job.get('steps', []):
                    script = step.get('run', '')
                    for line in script.splitlines():
                        if 'check_build_warnings.py --capture' not in line:
                            continue
                        wrapped += 1
                        with self.subTest(workflow=filename, step=step.get('name')):
                            tokens = shlex.split(line)
                            self.assertEqual(tokens[tokens.index('--') + 1], '$BASH')
                            self.assertIn('-- "$BASH" -eo pipefail', line)
                            syntax = subprocess.run(['bash', '-n'], input=script, text=True,
                                                    encoding='utf-8', capture_output=True)
                            self.assertEqual(syntax.returncode, 0, syntax.stderr)
            self.assertEqual(wrapped, expected, filename)
        if os.name != 'nt':
            with tempfile.TemporaryDirectory() as temporary:
                directory = Path(temporary)
                wrong = directory / 'bash'
                wrong.write_text('#!/bin/sh\nexit 73\n', encoding='utf-8')
                wrong.chmod(0o755)
                own_shell = directory / 'Git Bash' / 'bash'
                own_shell.parent.mkdir()
                shutil.copyfile(shutil.which('bash'), own_shell)
                own_shell.chmod(0o755)
                env = dict(os.environ, PATH=str(directory) + os.pathsep + os.environ['PATH'])
                for option in ('--capture', '--capture-builds'):
                    log = directory / 'build.log'
                    prefix = (f'{shlex.quote(sys.executable)} {shlex.quote(str(SCRIPT))} '
                              f'{option} {shlex.quote(str(log))} -- ')
                    command = '-eo pipefail -c "echo shell-ok; exit 7"'
                    good = subprocess.run([str(own_shell), '-c', prefix + '"$BASH" ' + command],
                                          env=env, capture_output=True)
                    self.assertEqual(good.returncode, 7, good.stderr)
                    self.assertIn(b'shell-ok', good.stdout)
                    bad = subprocess.run([str(own_shell), '-c', prefix + 'bash ' + command],
                                         env=env, capture_output=True)
                    self.assertEqual(bad.returncode, 73, bad.stderr)

    def test_merge_queue_uses_build_workflow_and_required_jobs(self):
        workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(encoding='utf-8'),
                             Loader=yaml.BaseLoader)
        self.assertIn('merge_group', workflow['on'])
        self.assertFalse(workflow['on']['merge_group'])  # no event/path filters
        jobs = workflow['jobs']
        for name in ('build', 'build-android', 'build-web', 'build-ios'):
            self.assertEqual(jobs[name]['if'], "needs.changes.outputs.docs_only != 'true'")
            self.assertIn(name, jobs['ci-ok']['needs'])
        self.assertIn("github.event_name == 'merge_group'", jobs['render-regression']['if'])
        self.assertIn('render-regression', jobs['ci-ok']['needs'])
        detector = next(s['run'] for s in jobs['changes']['steps'] if s.get('id') == 'detect')
        self.assertIn('github.event.merge_group.base_sha', detector)
        self.assertIn('github.event.merge_group.head_sha', detector)


if __name__ == '__main__':
    unittest.main()
