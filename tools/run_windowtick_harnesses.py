#!/usr/bin/env python3
"""Linux windowTick pre-send sweep. Invoke through the caller's tcbuild.sh.

Offline prerequisites use the installed shader compiler and system libraries.
Compare --json results from this tree and main; late-window is opt-in.
"""
import argparse
from contextlib import contextmanager
import json
import os
from pathlib import Path
import random
import shlex
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
TESTS = ROOT / 'core/tests/windowTick'
WORK = ROOT / '.build/windowtick-runner'
MESA = Path('/usr/share/glvnd/egl_vendor.d/50_mesa.json')
IMGUI = ROOT / 'addons/tcxImGui/example-basic/build-linux/addons/tcxImGui/libtcxImGui.a'
DEFAULT_SKIP = []
SCRIPTS = [
    'app-owner-review-test.py', 'c2-review-test.py', 'creation-settings-test.py',
    'egl-wait-test', 'event-converter-test', 'guest-storage-test.py',
    'host-entry-test', 'late-window-test.py', 'native-admission.py',
    'native-gates-test', 'native-mac-syntax.py', 'native-quit-test.py',
    'render-boundary-test', 'x11-pacing-test',
]
LINKED = {'app-owner-review-test.py', 'c2-review-test.py', 'event-converter-test',
          'guest-storage-test.py', 'host-entry-test', 'late-window-test.py',
          'render-boundary-test'}


# Bound (hung commands): allow up to 15 minutes for builds and sanitizer harnesses.
def run(command, env, timeout=900):
    """Capture chatter; inherit the caller's watchdog process group."""
    with subprocess.Popen(command, cwd=ROOT, env=env,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as proc:
        try:
            output = proc.communicate(timeout=timeout)[0]
            return proc.returncode, output.decode('utf-8', errors='replace')
        except subprocess.TimeoutExpired as error:
            proc.kill()
            proc.wait()
            # A descendant may still hold stdout; use the already captured output.
            output = (error.output or b'').decode('utf-8', errors='replace')
            return 1, output + f'\nTIMEOUT after {timeout}s'
        finally:
            if proc.poll() is None:
                proc.kill()
                proc.wait()


def prerequisites(env):
    """Generate real compiler/link metadata, without building allCoreTests.

    The small target uses windowTick sources. Older linked harnesses consume
    its metadata at the allCoreTests location; no prebuilt CLI is needed.
    Override shader acquisition so even a missing/wrong compiler cannot fetch.
    """
    WORK.mkdir(parents=True, exist_ok=True)
    source = WORK / 'source'
    source.mkdir(exist_ok=True)
    (source / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(windowTickHarnesses LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
include("${TC_ROOT}/core/cmake/tc_acquire_shdc.cmake")
function(trussc_acquire_shdc path url expected_hash)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Offline prerequisite missing: ${path}")
    endif()
    file(SHA256 "${path}" actual_hash)
    if(NOT actual_hash STREQUAL expected_hash)
        message(FATAL_ERROR "Offline shader compiler hash mismatch: ${path}")
    endif()
endfunction()
add_subdirectory("${TC_ROOT}/core" "${TC_ROOT}/core/build-linux")
file(GLOB sources "${TC_ROOT}/core/tests/windowTick/src/*.cpp")
add_executable(windowTick ${sources})
target_link_libraries(windowTick PRIVATE TrussC)
''', encoding='utf-8')
    build = WORK / 'build'
    commands = [
        ['cmake', '-S', str(source), '-B', str(build), '-G', 'Unix Makefiles',
         f'-DTC_ROOT={ROOT}', '-DCMAKE_BUILD_TYPE=Release',
         '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON', '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'],
        ['cmake', '--build', str(build), '--target', 'windowTick'],
    ]
    for command in commands:
        try:
            code, output = run(command, env)
        except FileNotFoundError as error:
            return None, str(error)
        if code:
            (WORK / 'prerequisites.log').write_text(output, encoding='utf-8')
            raise subprocess.CalledProcessError(code, command, output=output)
    metadata = ROOT / 'core/tests/allCoreTests/build-linux'
    (metadata / 'CMakeFiles/allCoreTests.dir').mkdir(parents=True, exist_ok=True)
    # Preserve metadata belonging to a real allCoreTests build.
    for source_file, destination in [
        (build / 'compile_commands.json', metadata / 'compile_commands.json'),
        (build / 'CMakeFiles/windowTick.dir/link.txt',
         metadata / 'CMakeFiles/allCoreTests.dir/link.txt'),
    ]:
        if not destination.is_file():
            shutil.copyfile(source_file, destination)
    return build / 'windowTick', None


@contextmanager
def private_display(env):
    if not shutil.which('Xvfb') or not shutil.which('xdpyinfo'):
        yield None, 'Xvfb or xdpyinfo missing'
        return
    # Never use an inherited display or the desktop's :0/:1. Xvfb's own
    # exclusive lock resolves races with other runners; failed starts retry.
    # Bound (display collisions): try at most 20 private display numbers.
    for number in random.sample(range(1000, 30000), 20):
        if Path(f'/tmp/.X{number}-lock').exists() or Path(f'/tmp/.X11-unix/X{number}').exists():
            continue
        with subprocess.Popen(['Xvfb', f':{number}', '-screen', '0', '1280x720x24',
                               '-nolisten', 'tcp'], env=env,
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL) as server:
            display_env = dict(env, DISPLAY=f':{number}')
            try:
                # Bound (server startup): at most 50 readiness probes per server.
                for _ in range(50):
                    if server.poll() is not None:
                        break
                    # Bound (stalled X11): each readiness probe gets five seconds.
                    if run(['xdpyinfo'], display_env, timeout=5)[0] == 0:
                        yield display_env, None
                        return
                    # Bound (probe rate): wait 100 ms between readiness probes.
                    time.sleep(0.1)
            finally:
                if server.poll() is None:
                    server.terminate()
                    try:
                        # Bound (server shutdown): force exit after five seconds.
                        server.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        server.kill()
                        server.wait()
    yield None, 'could not start a private high-numbered Xvfb'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--include-late-window', action='store_true')
    parser.add_argument('--skip', action='append', default=[], metavar='NAME')
    parser.add_argument('--json', type=Path, metavar='PATH')
    args = parser.parse_args()
    modes = [("windowTick-default", [])]
    for line in (TESTS / 'display-test').read_text(encoding='utf-8').splitlines():
        words = shlex.split(line, comments=True)
        if words:
            assert words[0] == '{test}', 'unexpected display-test command'
            modes.append(('windowTick' + words[1], words[1:]))
    names = SCRIPTS + [name for name, _ in modes]
    unknown = set(args.skip) - set(names)
    if unknown:
        parser.error('unknown harness: ' + ', '.join(sorted(unknown)))
    env = dict(os.environ, __EGL_VENDOR_LIBRARY_FILENAMES=str(MESA),
               LIBGL_ALWAYS_SOFTWARE='1', TRUSSC_MCP='0')
    env.pop('DISPLAY', None)
    for probe in ('CREATION_SETTINGS', 'EVENT_CONVERTER', 'HOST_ENTRY', 'LATE_WINDOW', 'MAC_SYNTAX'):
        env[f'TC_{probe}_OUTPUT'] = str(WORK / probe.lower())
    build_failed = False
    try:
        binary, build_reason = prerequisites(env)
    except subprocess.CalledProcessError as error:
        binary = None
        build_failed = True
        build_reason = 'prerequisite build failed; see ' + str(WORK / 'prerequisites.log')
        print('\n'.join(error.output.splitlines()[-10:]), flush=True)
    results = []

    def record(name, command, reason=None, child_env=env, failure=False):
        if reason:
            status = 'FAIL' if failure else 'SKIP'
            results.append(dict(name=name, status=status, seconds=0, reason=reason))
            print(f'{status} {name}: {reason}', flush=True)
            return
        started = time.monotonic()
        try:
            code, output = run(command, child_env)
        except FileNotFoundError as error:
            record(name, command, str(error))
            return
        elapsed = round(time.monotonic() - started, 2)
        status = 'FAIL' if code else 'PASS'
        results.append(dict(name=name, status=status, seconds=elapsed))
        print(f'{status} {name} ({elapsed:.2f}s)', flush=True)
        if code:
            print('\n'.join(output.splitlines()[-10:]), flush=True)

    skips = dict.fromkeys(DEFAULT_SKIP, 'default skip')
    if not args.include_late_window:
        skips['late-window-test.py'] = 'use --include-late-window'
    skips.update(dict.fromkeys(args.skip, 'requested by --skip'))
    for name in SCRIPTS:
        reason = skips.get(name) or (build_reason if name in LINKED else None)
        if not reason and name == 'c2-review-test.py' and not IMGUI.is_file():
            reason = 'missing local ImGui archive (libtcxImGui.a)'
        if not reason and name in ('late-window-test.py', 'render-boundary-test') and not MESA.is_file():
            reason = 'Mesa EGL vendor file missing'
        if not reason and name == 'late-window-test.py' and not shutil.which('xvfb-run'):
            reason = 'xvfb-run missing'
        if not reason and not shutil.which(env.get('CXX', 'c++')):
            reason = 'C++ compiler missing'
        if not reason and name in ('egl-wait-test', 'render-boundary-test'):
            packages = ['egl']
            if name == 'egl-wait-test':
                packages += ['glesv2', 'x11', 'xcursor', 'xi']
            if not shutil.which('pkg-config') or run(['pkg-config', '--exists', *packages], env)[0]:
                reason = 'missing development libraries: ' + ', '.join(packages)
        path = TESTS / name
        interpreter = 'sh' if path.read_bytes().startswith(b'#!/bin/sh') else sys.executable
        record(name, [interpreter, str(path)], reason,
               failure=build_failed and name in LINKED and name not in skips)
    for name, flags in modes:
        reason = 'requested by --skip' if name in args.skip else build_reason
        if not reason and flags and not MESA.is_file():
            reason = 'Mesa EGL vendor file missing'
        if reason or not flags:
            record(name, [str(binary), *flags], reason,
                   failure=build_failed and name not in args.skip)
        else:
            with private_display(env) as (display_env, reason):
                record(name, [str(binary), *flags], reason, display_env)
    if args.json:
        args.json.write_text(json.dumps(results, indent=2) + '\n', encoding='utf-8')
    return int(build_failed or any(row['status'] == 'FAIL' for row in results))


if __name__ == '__main__':
    sys.exit(main())
