#!/usr/bin/env python3
"""Run a stencil GPU check through Chrome and return its explicit page verdict.

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
    parser.add_argument('test', choices=('stencilRef', 'stencilDesc', 'stencilRoundTrip'))
    parser.add_argument('--build-dir', type=Path)
    parser.add_argument('--adapter', choices=('swiftshader', 'default'), default='swiftshader')
    parser.add_argument('--chrome', default='/usr/bin/google-chrome')
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    project = 'stencilDesc' if args.test == 'stencilDesc' else 'allCoreTests'
    argv = ([] if project == args.test else [args.test]) + ['--gpu-check']
    page = project + '.html?' + urlencode([('arg', value) for value in argv])
    spec = importlib.util.spec_from_file_location('webgpu_harness', TESTS / 'webgpuUniformGrowth/run.py')
    harness = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(harness)
    sys.argv = [str(spec.origin), '--build-dir', str(args.build_dir or TESTS / project / 'bin'),
                '--page', page, '--ready', 'window.tcCoreTestVerdict',
                '--adapter', args.adapter, '--print-adapter', '--chrome', args.chrome,
                '--timeout', str(args.timeout)]
    # Successful display tests destroy their device during normal app cleanup.
    return harness.main(allow_device_destruction=True)


if __name__ == '__main__':
    raise SystemExit(main())
