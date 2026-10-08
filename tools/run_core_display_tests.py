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
    """Yield (test_dir, words, skip_reason) per marker line.

    A line `skip "<reason>" <command...>` lists a mode that is not run (for
    example a Mesa-only quirk, or a known bug with its issue number): it is
    still reported, with its reason, so it stays visible in the summary.
    """
    for marker in sorted((root / 'core/tests').glob('*/display-test')):
        for number, line in enumerate(marker.read_text().splitlines(), 1):
            words = shlex.split(line, comments=True)
            if not words:
                continue
            reason = None
            if words[0] == 'skip':
                if len(words) < 3:
                    raise ValueError(f'{marker}:{number}: expected skip "<reason>" <command>')
                reason, words = words[1], words[2:]
            if words.count('{test}') != 1:
                raise ValueError(f'{marker}:{number}: expected one {{test}} token')
            yield marker.parent, words, reason


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
    for test_dir, words, reason in discover_modes(root):
        label = f'{test_dir.name}: {shlex.join(words)}'
        if reason is not None:
            print(f'SKIP {label} ({reason})', flush=True)
            timings.append((label, 'SKIP', 0.0, reason))
            continue
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
        status = 'PASS' if ok else 'FAIL'
        timings.append((label, status, elapsed, None))
        print(f'{status} {label} ({elapsed:.3f}s)', flush=True)

    print('\nDisplay modes (wall times are informational):', flush=True)
    for label, status, elapsed, reason in timings:
        if status == 'SKIP':
            print(f'SKIP {label} ({reason})', flush=True)
        else:
            print(f'{status} {elapsed:.3f}s {label}', flush=True)
    skipped = sum(status == 'SKIP' for _, status, _, _ in timings)
    failures = sum(status == 'FAIL' for _, status, _, _ in timings)
    print(f'{len(timings)} modes, {skipped} skipped, {failures} failures', flush=True)
    # Skips never change the result; an empty sweep (nothing ran) fails.
    return 0 if len(timings) > skipped and failures == 0 else 1


if __name__ == '__main__':
    try:
        sys.exit(run_modes(ROOT))
    except ValueError as error:
        sys.exit(str(error))
