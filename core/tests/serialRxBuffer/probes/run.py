#!/usr/bin/env python3
"""Linux process-lifetime/interposition tests. Build allCoreTests first.
Use --before-include DIR to test a saved pre-fix tc/comm header tree.
Every compile/link goes through the supplied build wrapper.
"""
import argparse
import os
from pathlib import Path
import shlex
import subprocess

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-wrapper', required=True)
p.add_argument('--output', type=Path, required=True)
p.add_argument('--before-include', type=Path)
p.add_argument('--probe', choices=['staticExit', 'allocationFailure'])
a = p.parse_args()
root = Path(__file__).resolve().parents[4]
link = shlex.split((root / 'core/tests/allCoreTests/build-linux/CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
libs = link[link.index(str(root / 'core/build-linux/libTrussC.a')):]
a.output.mkdir(parents=True, exist_ok=True)
for name, flags in [('staticExit', ['-fsanitize=address,undefined']),
                    ('allocationFailure', ['-Wl,--wrap=malloc'])]:
    if a.probe and a.probe != name:
        continue
    exe = a.output.resolve() / name
    command = [a.build_wrapper, 'c++', '-std=c++20', '-O1', '-g', '-fno-omit-frame-pointer'] + flags
    if a.before_include:
        command += ['-I' + str(a.before_include.resolve())]
    command += ['-I' + str(root / 'core/include'), str(Path(__file__).with_name(name + '.cpp')), '-o', str(exe)] + libs
    subprocess.run(command, check=True, cwd=root)
    result = subprocess.run([str(exe)], env={**os.environ, 'ASAN_OPTIONS': 'detect_leaks=0'}, timeout=90)
    print(f'{name}: exit {result.returncode}', flush=True)
    if result.returncode:
        raise SystemExit(result.returncode)
