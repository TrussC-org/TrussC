#!/usr/bin/env python3
"""Production-linked C2 review regressions. Run serially through tcbuild.sh.
--tsan instruments the production Runtime and the worker/ownership probes,
and compiles headlessExit itself with TSan (including its worker path).
Only pre-main TSan address-layout startup failures are retried.
"""
from pathlib import Path
import argparse, json, os, shlex, subprocess
root = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser()
mode = parser.add_mutually_exclusive_group()
mode.add_argument('--tsan', action='store_true')
mode.add_argument('--asan', action='store_true')
args = parser.parse_args()
out = Path(os.environ.get('TC_C2_REVIEW_OUTPUT', root / '.build/c2-review')).resolve()
out.mkdir(parents=True, exist_ok=True)
evidence = Path(os.environ.get('TC_C2_REVIEW_EVIDENCE', out)).resolve()
evidence.mkdir(parents=True, exist_ok=True)
build = root / 'core/tests/allCoreTests/build-linux'
entry = next(e for e in json.loads((build / 'compile_commands.json').read_text(encoding="utf-8"))
             if e['file'].endswith('tcWindowRuntime.cpp'))
command = shlex.split(entry['command']); compiler = command.pop(0)
flags = []; skip = False
for flag in command:
    if skip: skip = False; continue
    if flag == '-o': skip = True; continue
    if flag == '-c' or flag.endswith('tcWindowRuntime.cpp') or flag.startswith(('-O', '-g')): continue
    flags.append(flag)
flags = ['-I'+str(root/'core/include'), *flags, '-O1', '-g1']
link = shlex.split((build / 'CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding="utf-8"))
libs = link[next(i for i, value in enumerate(link) if value.endswith('libTrussC.a')):]
objects = []
if args.tsan or args.asan:
    flags += ['-fsanitize='+('thread' if args.tsan else 'address'), '-fno-omit-frame-pointer', '-fno-pie']
    runtime = out / ('runtime-tsan.o' if args.tsan else 'runtime-asan.o')
    subprocess.run([compiler, *flags, '-c', str(root/'core/include/tc/app/tcWindowRuntime.cpp'), '-o', str(runtime)], check=True, cwd=build)
    objects.append(str(runtime))
    flags.append('-no-pie')
    probes = [('replacement-race', 'c2-worker-quit.cpp'),
              ('headlessExit-worker', '../headlessExit/src/main.cpp'),
              ('c1-owner-review', 'app-owner-review.cpp'),
              ('epoch', 'c2-epoch.cpp'), ('queue-epochs', 'c2-queue-epochs.cpp')]
    if args.asan: probes = [('static-inspector', 'c2-static-inspector.cpp')]
else:
    probes = [('replacement-race', 'c2-worker-quit.cpp'), ('veto-reason', 'c2-veto-reason.cpp'),
              ('addon-reload', 'c2-addon-reload.cpp'),
              ('epoch', 'c2-epoch.cpp'), ('queue-epochs', 'c2-queue-epochs.cpp')]
results = []
for name, source in probes:
    extra = []
    if name in ('addon-reload', 'static-inspector'):
        # Compile the actual addon source, avoiding a stale pre-follow-up archive.
        extra = ['-I'+str(root/'addons/tcxImGui/src'), '-I'+str(root/'addons/tcxNodeInspector/src'),
                 str(root/'addons/tcxNodeInspector/src/tcxNodeInspector.cpp'),
                 str(root/'addons/tcxImGui/example-basic/build-linux/addons/tcxImGui/libtcxImGui.a')]
    binary = out / name
    subprocess.run([compiler, *flags, str(root/'core/tests/windowTick'/source), *objects,
                    *extra, *libs, *(['-Wl,--wrap=sapp_cancel_quit'] if name == 'c1-owner-review' else []),
                    '-o', str(binary)], cwd=build, check=True)
    for attempt in range(1, 101 if args.tsan else 2):
        env = dict(os.environ, TSAN_OPTIONS='halt_on_error=1', ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')
        result = subprocess.run([str(binary)], capture_output=True, env=env, timeout=60)
        content = result.stdout + result.stderr
        log = evidence / f'{name}-{"tsan" if args.tsan else "asan" if args.asan else "plain"}-{attempt}.log'
        log.write_bytes(content)
        # A startup crash must not conceal a probe failure. headlessExit prints
        # its first check before reaching a worker; other probes mark main.
        startup = b'FATAL: ThreadSanitizer: unexpected memory mapping' in content
        startup |= result.returncode == -11 and not content
        startup &= b'ENTERED MAIN' not in content and b'main entered' not in content
        record = dict(name=name, attempt=attempt, exit=result.returncode,
                      startup_failure=bool(startup), log=str(log))
        results.append(record)
        (evidence / ('tsan-results.json' if args.tsan else 'asan-results.json' if args.asan else 'review-results.json')).write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
        print(record, flush=True)
        if result.returncode == 0: break
        if not (args.tsan and startup):
            print(content.decode(errors='replace'), flush=True)
            raise SystemExit(result.returncode if result.returncode > 0 else 1)
    else:
        raise SystemExit(f'{name}: TSan never started successfully')
