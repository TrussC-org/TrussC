#!/usr/bin/env python3
"""Run a built computeBuffer Web app through the shared headless Chrome harness.

Build the computeBuffer project for Web with the installed emsdk first.
SwiftShader is the default; --adapter default selects the host's device adapter.
"""
import argparse
import importlib.util
from pathlib import Path
import sys

TEST = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=TEST / 'bin')
    parser.add_argument('--mode', choices=('async', 'exit', 'map-exit'), default='async')
    parser.add_argument('--adapter', choices=('swiftshader', 'default'), default='swiftshader')
    parser.add_argument('--chrome', default='/usr/bin/google-chrome')
    parser.add_argument('--timeout', type=float, default=180)
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('webgpu_harness', TEST.parent / 'webgpuUniformGrowth/run.py')
    harness = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(harness)
    sys.argv = [str(spec.origin), '--build-dir', str(args.build_dir),
                '--page', f'computeBuffer.html?mode={args.mode}',
                '--ready', 'window.tcComputeBufferVerdict', '--adapter', args.adapter,
                '--chrome', args.chrome, '--timeout', str(args.timeout)]
    return harness.main()


if __name__ == '__main__':
    raise SystemExit(main())
