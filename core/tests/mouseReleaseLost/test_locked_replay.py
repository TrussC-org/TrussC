"""Linux headless EGL regression; run after building allCoreTests.

The production XI2 handler records events and lock-query results. A separate
GLES3 consumer replays them through the real main callback, App and node tree.
Only Xlib cookie retrieval and the consumer's lock APIs are substituted.
"""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[3] / "tools"))
from check_build_warnings import run_build

ROOT = Path(__file__).resolve().parents[3]
SOURCE = Path(__file__).resolve().parent / 'replay'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)
    build = ROOT / 'core/tests/allCoreTests/build-linux'
    entries = json.loads((build / 'compile_commands.json').read_text(encoding='utf-8'))
    entry = next(e for e in entries if e['file'].endswith('/mouseReleaseLost/src/main.cpp'))

    def run(command, cwd=ROOT, build=False):
        print(shlex.join(map(str, command)), flush=True)
        (run_build if build else subprocess.run)(command, cwd=cwd, check=True)

    run(['c++', '-std=c++20', '-O2', '-ffunction-sections', '-fdata-sections',
         '-Wl,--gc-sections', '-I' + str(ROOT / 'core/include/sokol'),
         str(SOURCE / 'egl-producer.cpp'), '-o', str(out / 'producer'),
         '-lX11', '-lXi', '-lXcursor', '-ldl', '-pthread'], build=True)
    run([str(out / 'producer'), str(out / 'events.bin')])
    compile_args = [a for a in shlex.split(entry['command'])
                    if not a.startswith('-DTC_CORE_TEST_NAME=')]
    compile_args[compile_args.index('-DSOKOL_GLCORE')] = '-DSOKOL_GLES3'
    compile_args[compile_args.index('-o') + 1] = str(out / 'consumer.o')
    compile_args[compile_args.index('-c') + 1] = str(SOURCE / 'egl-consumer.cpp')
    run(compile_args, cwd=entry['directory'], build=True)
    link = shlex.split((build / 'CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
    link = [a for a in link if not a.endswith(('.cpp.o', '.c.o'))]
    link[link.index('-o') + 1] = str(out / 'consumer')
    link.insert(1, str(out / 'consumer.o'))
    run(link, cwd=build, build=True)
    run([str(out / 'consumer'), str(out / 'events.bin')], cwd=out)


if __name__ == '__main__':
    main()
