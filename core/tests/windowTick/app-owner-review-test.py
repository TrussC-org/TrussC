#!/usr/bin/env python3
"""Compile and run ownership/concurrency/deferred-stage regressions against production."""
from pathlib import Path
import json, os, shlex, subprocess
root = Path(__file__).resolve().parents[3]
build = root / 'core/tests/allCoreTests/build-linux'
scratch = Path(os.environ.get('TC_OWNER_REVIEW_OUTPUT', root / '.build/app-owner-review')).resolve()
scratch.mkdir(parents=True, exist_ok=True)
entry = next(e for e in json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
             if e['file'].endswith('tcWindowRuntime.cpp'))
args = shlex.split(entry['command']); compiler = args.pop(0); flags = []; skip = False
for arg in args:
    if skip: skip = False; continue
    if arg == '-o': skip = True; continue
    if arg == '-c' or arg.endswith('tcWindowRuntime.cpp') or arg.startswith(('-O', '-g')): continue
    flags.append(arg)
link = shlex.split((build / 'CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
libs = link[next(i for i, arg in enumerate(link) if arg.endswith('libTrussC.a')):]
subprocess.run([compiler, *flags, '-O0', '-g0', str(root / 'core/tests/windowTick/app-owner-review.cpp'),
                *libs, '-Wl,--wrap=sapp_cancel_quit', '-o', str(scratch / 'probe')], cwd=build, check=True)
subprocess.run([str(scratch / 'probe')], check=True)
