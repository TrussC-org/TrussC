#!/usr/bin/env python3
"""Run a core display check through Chrome and return its explicit page verdict.

Build without --emrun. Use --adapter default and --chrome <Edge path> on a
device; SwiftShader is the default for local verification.
"""
import argparse
import importlib.util
from pathlib import Path
import sys
from urllib.parse import urlencode

TESTS = Path(__file__).resolve().parent.parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('test', choices=('stencilRef', 'stencilDesc', 'stencilRoundTrip', 'webAsyncifyLoop'))
    parser.add_argument('--mode', choices=('all', 'update', 'control'), default='all',
                        help='webAsyncifyLoop: setup and update sleeps, update sleeps only, or no sleeps')
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--adapter', choices=('swiftshader', 'default'), default='swiftshader')
    parser.add_argument('--chrome', default='/usr/bin/google-chrome')
    parser.add_argument('--timeout', type=float, default=180)
    parser.add_argument('--profile-dir', type=Path, help='Parent for a private temporary Chrome profile')
    args = parser.parse_args()
    project = 'stencilDesc' if args.test == 'stencilDesc' else 'allCoreTests'
    argv = ([] if project == args.test else [args.test]) + ['--gpu-check']
    if args.test == 'webAsyncifyLoop':
        argv.append(args.mode)
    page = project + '.html?' + urlencode([('arg', value) for value in argv])
    spec = importlib.util.spec_from_file_location('webgpu_harness', TESTS / 'webgpuUniformGrowth/run.py')
    harness = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(harness)
    sys.argv = [str(spec.origin), '--build-dir', str(args.build_dir or TESTS / project / 'bin'),
                '--page', page, '--ready', 'window.tcCoreTestVerdict',
                '--adapter', args.adapter, '--print-adapter', '--chrome', args.chrome,
                '--timeout', str(args.timeout)]
    if args.profile_dir:
        sys.argv += ['--profile-dir', str(args.profile_dir)]
    if args.test == 'webAsyncifyLoop':
        sys.argv.append('--loop-diagnostics')
    # Successful display tests destroy their device during normal app cleanup.
    return harness.main(allow_device_destruction=True)


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError) as error:
        print(f'CORE_WEB: FAIL {error}', flush=True)
        raise SystemExit(1)
