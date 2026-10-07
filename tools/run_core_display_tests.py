#!/usr/bin/env python3
"""Run already-built core display modes from core/tests/*/display-test (Linux)."""
from pathlib import Path
import os
import shlex
import signal
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'examples'))
from build_all import find_test_binary, is_combined_core_test

# Same hang guard as the headless core runner; never a timing assertion.
MODE_TIMEOUT = 600


def discover_modes(root):
    for marker in sorted((root / 'core/tests').glob('*/display-test')):
        for number, line in enumerate(marker.read_text().splitlines(), 1):
            words = shlex.split(line, comments=True)
            if not words:
                continue
            if words.count('{test}') != 1:
                raise ValueError(f'{marker}:{number}: expected one {{test}} token')
            yield marker.parent, words


def run_mode(command, cwd):
    # Kill the whole process group on a hang: a hot-reload mode can spawn
    # compilers, and a mode may start its own Xvfb with special server flags.
    with subprocess.Popen(command, cwd=cwd, start_new_session=True) as process:
        try:
            code = process.wait(timeout=MODE_TIMEOUT)
            if code:
                print(f'Exit code: {code}', flush=True)
            return code == 0
        except subprocess.TimeoutExpired:
            os.killpg(process.pid, signal.SIGKILL)
            process.wait()
            print(f'TIMEOUT after {MODE_TIMEOUT}s', flush=True)
            return False


def run_modes(root):
    timings = []
    for test_dir, words in discover_modes(root):
        label = f'{test_dir.name}: {shlex.join(words)}'
        print(f'RUN {label}', flush=True)
        started = time.monotonic()
        combined = is_combined_core_test(test_dir)
        binary_dir = root / 'core/tests/allCoreTests' if combined else test_dir
        binary = find_test_binary(str(binary_dir), {'os': 'linux'})
        ok = False
        if binary:
            entry = [binary, test_dir.name] if combined else [binary]
            command = [arg for word in words for arg in (entry if word == '{test}' else [word])]
            try:
                ok = run_mode(command, test_dir)
            except OSError as error:
                print(f'ERROR {label}: {error}', flush=True)
        else:
            print(f'ERROR {label}: binary missing; build core tests first', flush=True)
        elapsed = time.monotonic() - started
        timings.append((label, ok, elapsed))
        print(f'{"PASS" if ok else "FAIL"} {label} ({elapsed:.3f}s)', flush=True)

    print('\nDisplay modes (wall times are informational):', flush=True)
    for label, ok, elapsed in timings:
        print(f'{"PASS" if ok else "FAIL"} {elapsed:.3f}s {label}', flush=True)
    print(f'{len(timings)} modes, {sum(not ok for _, ok, _ in timings)} failures', flush=True)
    return 0 if timings and all(ok for _, ok, _ in timings) else 1


if __name__ == '__main__':
    try:
        sys.exit(run_modes(ROOT))
    except ValueError as error:
        sys.exit(str(error))
