"""Exercise the reload-check exit handler without a display or the core build.

Run through tcbuild.sh: this compiles a small fixture from the actual handler.
"""
from pathlib import Path
import os
import re
import shutil
import signal
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def reload_artifacts(project, platform=sys.platform, config='Release'):
    """Select the sweep's build tree and the guest matching its host config.

    build_all.py uses --config Release for multi-config hosts. A manually built
    host can use TC_RELOAD_CHECK_CONFIG to specify its own configuration.
    """
    system, guest_name = {
        'darwin': ('macos', 'libguest.dylib'),
        'win32': ('windows', 'guest.dll'),
    }.get(platform, ('linux', 'libguest.so'))
    cache = project / f'build-{system}' / 'CMakeCache.txt'
    values = dict(re.findall(r'^([^#/:=]+):[^=]*=(.*)$',
                             cache.read_text(encoding='utf-8'), re.MULTILINE))
    # Like Host::updateGuestLibPath(), use <config>/ only for multi-config.
    multi = bool(values.get('CMAKE_CONFIGURATION_TYPES'))
    guest_dir = cache.parent / config if multi else cache.parent
    guest = guest_dir / guest_name
    if not guest.is_file():
        raise AssertionError(f'Guest for host configuration {config} missing: {guest}')
    return cache, guest


def run_reload_host(command, project, timeout=600):
    # Bound (hang guard): allow three reload builds on slow native machines;
    # this deadline detects hangs, not performance regressions.
    options = ({'creationflags': subprocess.CREATE_NEW_PROCESS_GROUP} if os.name == 'nt'
               else {'start_new_session': True})
    with subprocess.Popen(command, cwd=project, stdout=subprocess.PIPE,
                          stderr=subprocess.STDOUT, text=True, encoding='utf-8',
                          errors='replace', **options) as process:
        try:
            output, _ = process.communicate(timeout=timeout)
        except subprocess.TimeoutExpired:
            if os.name == 'nt':
                # /PID targets only this host and /T its descendants.
                subprocess.run(['taskkill', '/PID', str(process.pid), '/T', '/F'],
                               check=True, capture_output=True)
            else:
                try:
                    os.killpg(process.pid, signal.SIGTERM)
                    # Bound (cleanup grace): let the host reap stopped children.
                    process.communicate(timeout=5)
                except (ProcessLookupError, subprocess.TimeoutExpired):
                    pass
                finally:
                    # Also stop descendants that ignored TERM or held no pipe.
                    try:
                        os.killpg(process.pid, signal.SIGKILL)
                    except ProcessLookupError:
                        pass
            output, _ = process.communicate()
            raise AssertionError(f'--reload-check timed out after {timeout}s\n{output}') from None
        return subprocess.CompletedProcess(command, process.returncode, output)


class ReloadCheckRunnerTests(unittest.TestCase):
    def test_multiconfig_uses_host_configuration_and_platform_tree(self):
        for platform, system, name in (('darwin', 'macos', 'libguest.dylib'),
                                       ('win32', 'windows', 'guest.dll'),
                                       ('linux', 'linux', 'libguest.so')):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as scratch:
                project = Path(scratch)
                build = project / f'build-{system}'
                build.mkdir()
                cache = build / 'CMakeCache.txt'
                cache.write_text('CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n',
                                 encoding='utf-8')
                for config in ('Debug', 'Release'):
                    (build / config).mkdir()
                    (build / config / name).touch()
                other = project / ('build-windows' if system != 'windows' else 'build-linux')
                other.mkdir()
                (other / 'CMakeCache.txt').touch()
                for config in ('Release', 'Debug'):
                    self.assertEqual(reload_artifacts(project, platform, config),
                                     (cache, build / config / name))
                self.assertEqual(reload_artifacts(project, platform)[1], build / 'Release' / name)
                cache.write_text('CMAKE_BUILD_TYPE:STRING=RelWithDebInfo\n', encoding='utf-8')
                (build / name).touch()
                self.assertEqual(reload_artifacts(project, platform)[1], build / name)

    def test_timeout_stops_spawned_child(self):
        with tempfile.TemporaryDirectory() as scratch:
            project = Path(scratch)
            pid_file = project / 'child.pid'
            host = project / 'host.py'
            host.write_text('''import os, signal, subprocess, sys, time
from pathlib import Path
child = subprocess.Popen([sys.executable, '-c', 'import time; time.sleep(120)'])
# The host reaps its child after the group receives TERM on POSIX.
def stop(signum, frame):
    child.wait()
    sys.exit(0)
if os.name != 'nt':
    signal.signal(signal.SIGTERM, stop)
Path(sys.argv[1]).write_text(str(child.pid), encoding='utf-8')
time.sleep(120)
''', encoding='utf-8')
            child_stopped = False
            try:
                # Bound (fixture hang guard): ample time to start two interpreters.
                with self.assertRaisesRegex(AssertionError, 'timed out'):
                    run_reload_host([sys.executable, str(host), str(pid_file)], project, timeout=3)
                child_pid = int(pid_file.read_text(encoding='utf-8'))
                if os.name == 'nt':
                    import ctypes
                    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
                    kernel.OpenProcess.restype = ctypes.c_void_p
                    kernel.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_ulong]
                    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
                    handle = kernel.OpenProcess(0x00100000, False, child_pid)
                    if handle:
                        try:
                            self.assertEqual(kernel.WaitForSingleObject(handle, 0), 0)
                        finally:
                            kernel.CloseHandle(handle)
                    else:
                        self.assertEqual(ctypes.get_last_error(), 87)  # PID no longer exists
                else:
                    with self.assertRaises(ProcessLookupError):
                        os.kill(child_pid, 0)
                child_stopped = True
            finally:
                # If the runner regresses, clean up only this fixture's child.
                if not child_stopped and pid_file.exists():
                    child_pid = int(pid_file.read_text(encoding='utf-8'))
                    if os.name != 'nt':
                        try:
                            os.kill(child_pid, signal.SIGKILL)
                        except ProcessLookupError:
                            pass
                    else:
                        subprocess.run(['taskkill', '/PID', str(child_pid), '/F'],
                                       capture_output=True)


@unittest.skipUnless(shutil.which('c++'), 'requires a C++ compiler')
class ReloadCheckExit(unittest.TestCase):
    def test_returning_and_nonreturning_hosts(self):
        source = (ROOT / 'core/tests/hotReloadLifecycle/src/main.cpp').read_text(encoding='utf-8')
        handler_start = source.index('static int g_reloadCheckResult =')
        function_start = source.index('static void reloadCheckExit()', handler_start)
        handler_end = source.index('\n}', function_start) + 2
        handler = source[handler_start:handler_end]
        with tempfile.TemporaryDirectory() as scratch:
            fixture = Path(scratch) / 'exit.cpp'
            fixture.write_text('''#include <cstdio>
#include <cstdlib>
#include <string>
''' + handler + '''
int main(int argc, char** argv) {
    std::atexit(reloadCheckExit);
    std::string mode = argv[1];
    if (mode == "finished") {
        g_reloadCheckFinished = true;
        g_reloadCheckResult = std::atoi(argv[2]);
    } else if (mode != "unfinished") {
        g_reloadCheckStopReason = mode;
    }
    if (std::string(argv[argc - 1]) == "exit") std::exit(0);
    return g_reloadCheckResult;
}
''', encoding='utf-8')
            for platform in ('returning', 'macos'):
                binary = Path(scratch) / platform
                command = ['c++', '-std=c++17', str(fixture), '-o', str(binary)]
                if platform == 'macos':
                    command.insert(1, '-D__APPLE__')
                compiled = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
                self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
                for stop in ('return', 'exit'):
                    for reason in ('unfinished', 'stale host', 'missing guest', 'build failed'):
                        with self.subTest(platform=platform, stop=stop, reason=reason):
                            result = subprocess.run([str(binary), reason, stop],
                                                    capture_output=True, text=True, encoding='utf-8')
                            self.assertEqual(result.returncode, 65)
                            self.assertIn('--reload-check: FAIL - ', result.stdout)
                            self.assertIn(reason if reason != 'unfinished' else
                                          'host stopped before completing the checks', result.stdout)
                            self.assertNotIn('--reload-check: OK', result.stdout)
                # macOS exits inside sapp_run; other platforms return to main.
                stop = 'exit' if platform == 'macos' else 'return'
                for code in (0, 61, 64):
                    result = subprocess.run([str(binary), 'finished', str(code), stop],
                                            capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(result.returncode, code)
                    self.assertEqual(result.stdout, '')


@unittest.skipUnless(os.environ.get('TC_RELOAD_CHECK_BINARY'),
                     'set TC_RELOAD_CHECK_BINARY after a core sweep; requires a display')
class ReloadCheckIntegration(unittest.TestCase):
    def test_fresh_and_early_stops(self):
        binary = Path(os.environ['TC_RELOAD_CHECK_BINARY']).resolve()
        project = ROOT / 'core/tests/hotReloadLifecycle'
        self.assertTrue(binary.is_relative_to(project))
        cache, guest = reload_artifacts(
            project, config=os.environ.get('TC_RELOAD_CHECK_CONFIG', 'Release'))

        def run(expected_code, verdict):
            result = run_reload_host([str(binary), '--reload-check'], project)
            for line in result.stdout.splitlines():
                if line.startswith(('hotReloadLifecycle --reload-check: OK',
                                    'hotReloadLifecycle --reload-check: FAIL')):
                    print(line, flush=True)
            print(f'--reload-check exit: {result.returncode}', flush=True)
            self.assertEqual(result.returncode, expected_code, result.stdout)
            self.assertIn(verdict, result.stdout)
            if expected_code:
                self.assertIn('--reload-check: FAIL - ', result.stdout)
                self.assertNotIn('--reload-check: OK', result.stdout)

        # Must be first: no manual host rebuild between the sweep and this run.
        run(0, 'hotReloadLifecycle --reload-check: OK')
        before = binary.stat()
        try:
            os.utime(binary, ns=(before.st_atime_ns, 1_000_000_000))
            run(65, 'host executable is older than TrussC')
        finally:
            os.utime(binary, ns=(before.st_atime_ns, before.st_mtime_ns))

        original = cache.read_bytes()
        try:
            # Use the existing CMAKE_COMMAND seam to fail/skip only the build.
            # No guest or core sources are changed, and all fixtures are restored.
            for command, reason in (('false', 'Build failed'), ('true', 'Failed')):
                mock = shutil.which(command)
                if mock is None:
                    self.skipTest('build-failure/missing-guest mocks require true and false')
                cache.write_bytes(re.sub(rb'^CMAKE_COMMAND:INTERNAL=.*$',
                                         lambda match: b'CMAKE_COMMAND:INTERNAL=' + os.fsencode(mock),
                                         original, flags=re.MULTILINE))
                if command == 'true':
                    saved = guest.with_suffix(guest.suffix + '.saved')
                    guest.rename(saved)
                    try:
                        run(65, reason)
                    finally:
                        saved.rename(guest)
                else:
                    run(65, reason)
        finally:
            cache.write_bytes(original)


if __name__ == '__main__':
    unittest.main()
