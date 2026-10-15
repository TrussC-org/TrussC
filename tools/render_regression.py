#!/usr/bin/env python3
"""Compare #821 scenes on two refs on the same machine (no git fetch).

Requires a display/GPU and built tools/bin/trusscli in this checkout.
Linux: run under Xvfb with LIBGL_ALWAYS_SOFTWARE=1 and Mesa's EGL vendor.
Use TC_BUILD_WRAPPER to put a local build guard in front of each CMake call.
Worktrees are temporary; PNGs and samples.json remain under --output.
"""
import argparse
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
HARNESS = 'core/tests/renderRegression'


def run(command, cwd=ROOT, capture=False, env=None):
    print('+ ' + shlex.join(map(str, command)), flush=True)
    result = subprocess.run(list(map(str, command)), cwd=cwd, check=True, env=env,
                            text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout.strip() if capture else None


def commit(ref):
    return run(['git', 'rev-parse', '--verify', '--end-of-options', ref + '^{commit}'], capture=True)


def has_harness(ref):
    return subprocess.run(['git', 'cat-file', '-e', f'{ref}:{HARNESS}/src/main.cpp'],
                          cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL).returncode == 0


def build(tree, trusscli):
    project = tree / HARNESS
    wrapper = shlex.split(os.environ.get('TC_BUILD_WRAPPER', ''))
    # Normalize absolute source/include paths separately for each checkout so
    # merge-base and head hit the same cache. CMake inherits the standard
    # CMAKE_{C,CXX}_COMPILER_LAUNCHER env vars on both fresh configurations.
    build_env = dict(os.environ, CCACHE_BASEDIR=str(tree))
    run(wrapper + [trusscli, 'update', '-p', project, '--tc-root', tree, '--ide', 'cmake',
                   '--no-web', '--no-android', '--no-ios'], env=build_env)
    run(wrapper + ['cmake', '-S', project, '-B', project / 'build-regression',
                   '-DCMAKE_BUILD_TYPE=Release', '-DTC_HOT_RELOAD=OFF'], env=build_env)
    run(wrapper + ['cmake', '--build', project / 'build-regression', '--config', 'Release', '--parallel', '4'], env=build_env)
    suffix = '.exe' if sys.platform == 'win32' else ''
    binary = project / 'bin' / ('renderRegression' + suffix)
    if sys.platform == 'darwin':
        binary = project / 'bin/renderRegression.app/Contents/MacOS/renderRegression'
    if not binary.is_file():
        raise RuntimeError(f'Built binary missing: {binary}')
    return binary


def compare_refs(base_ref, head_ref, output):
    base, head = commit(base_ref), commit(head_ref)
    merge_base = run(['git', 'merge-base', base, head], capture=True)
    print(f'Record merge-base {merge_base}; compare head {head}', flush=True)
    trusscli = ROOT / 'tools/bin' / ('trusscli.exe' if sys.platform == 'win32' else 'trusscli')
    if not trusscli.is_file():
        raise RuntimeError('Build tools/bin/trusscli first')
    if output.exists() and any(output.iterdir()):
        raise RuntimeError(f'Output must be empty: {output}')
    output.mkdir(parents=True, exist_ok=True)
    # Cleanup only the detached worktrees this invocation created.
    with tempfile.TemporaryDirectory(prefix='.render-regression-', dir=ROOT) as temp:
        trees = []
        try:
            head_tree = Path(temp) / 'head'
            run(['git', 'worktree', 'add', '--detach', head_tree, head]); trees.append(head_tree)
            head_binary = build(head_tree, trusscli)
            run([head_binary, '--self-check'], cwd=head_tree)
            if not has_harness(merge_base):
                print('Base lacks renderRegression (first batch): head --self-check only.', flush=True)
                return
            base_tree = Path(temp) / 'base'
            run(['git', 'worktree', 'add', '--detach', base_tree, merge_base]); trees.append(base_tree)
            base_binary = build(base_tree, trusscli)
            run([base_binary, '--record', output / 'base'], cwd=base_tree)
            # The binary prints per-scene counts and diff-image paths, including
            # on a failed comparison. Keep all outputs available to CI artifacts.
            run([head_binary, '--compare', output / 'base'], cwd=head_tree)
        finally:
            for tree in reversed(trees):
                run(['git', 'worktree', 'remove', '--force', tree])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('base_ref')
    parser.add_argument('head_ref')
    parser.add_argument('--output', type=Path, default=Path('render-regression-output'))
    args = parser.parse_args()
    try:
        compare_refs(args.base_ref, args.head_ref, args.output.resolve())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f'render-regression: {error}', file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
