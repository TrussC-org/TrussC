#!/usr/bin/env python3
"""Run built example --self-check entrypoints in the inherited display (no builds)."""
from pathlib import Path
import os
import subprocess
import sys

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
    if not os.environ.get('DISPLAY'):
        print('ERROR: DISPLAY is unset; run under xvfb-run or an existing X display', flush=True)
        return 1
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
            if not ok:
                print(f'Exit code: {result.returncode}', flush=True)
        except FileNotFoundError:
            print(f'ERROR: binary missing: {binary}; build examples first', flush=True)
            ok = False
        except (OSError, subprocess.TimeoutExpired) as error:
            print(f'ERROR: {error}', flush=True)
            ok = False
        failures += not ok
        print(f'{"PASS" if ok else "FAIL"}: {example.name}', flush=True)
    print(f'{len(examples)} example self-checks, {failures} failures', flush=True)
    return int(failures != 0)


if __name__ == '__main__':
    sys.exit(run_checks(ROOT))
