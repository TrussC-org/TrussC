#!/usr/bin/env python3
"""Run built example --self-check entrypoints under Linux Xvfb (no builds)."""
from pathlib import Path
import os
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'examples'))
from build_all import find_examples

EXAMPLE_TIMEOUT = 60  # Bound (hung example): stop a self-check that never exits.


def discover_examples(root):
    """Search only example sources, excluding README mentions and test harnesses."""
    for directory in find_examples(str(root)):
        example = Path(directory)
        if any('"--self-check"' in source.read_text(encoding='utf-8')
               for source in sorted((example / 'src').rglob('*'))
               if source.suffix in {'.cpp', '.cc', '.cxx', '.h', '.hpp'}):
            yield example


def run_checks(root):
    examples = list(discover_examples(root))
    if not examples:
        print('ERROR: no example self-checks discovered', flush=True)
        return 1
    failures = 0
    for example in examples:
        binary = example / 'bin' / example.name
        print(f'RUN {example.relative_to(root)} --self-check', flush=True)
        try:
            result = subprocess.run([str(binary), '--self-check'], cwd=example,
                                    timeout=EXAMPLE_TIMEOUT, check=False)
            ok = result.returncode == 0
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f'ERROR: {error}', flush=True)
            ok = False
        failures += not ok
        print(f'{"PASS" if ok else "FAIL"}: {example.name}', flush=True)
    print(f'{len(examples)} example self-checks, {failures} failures', flush=True)
    return int(failures != 0)


def run_with_display(root):
    started = time.monotonic()
    # Export before Xvfb starts so readiness probes and every child agree.
    os.environ['DISPLAY'] = os.environ.get('DISPLAY', ':97')
    server = subprocess.Popen(['Xvfb', os.environ['DISPLAY'], '-screen', '0',
                               '1280x1024x24', '-nolisten', 'tcp'])
    try:
        deadline = time.monotonic() + 10  # Bound (display startup): fail if Xvfb never becomes ready.
        while server.poll() is None:
            ready = subprocess.run(['xdpyinfo'], stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL, timeout=2)  # Bound (hung probe): stop unresponsive xdpyinfo.
            if ready.returncode == 0:
                return run_checks(root)
            if time.monotonic() >= deadline:
                break
            time.sleep(0.1)
        print('ERROR: Xvfb did not become ready', flush=True)
        return 1
    finally:
        if server.poll() is None:
            server.terminate()  # This Xvfb PID only.
        try:
            server.wait(timeout=5)  # Bound (shutdown): allow graceful Xvfb exit before killing it.
        except subprocess.TimeoutExpired:
            server.kill()
            server.wait()
        print(f'Example self-check step: {time.monotonic() - started:.3f}s', flush=True)


if __name__ == '__main__':
    try:
        sys.exit(run_with_display(ROOT))
    except (OSError, subprocess.TimeoutExpired) as error:
        sys.exit(f'ERROR: {error}')
