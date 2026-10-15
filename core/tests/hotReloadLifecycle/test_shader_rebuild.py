#!/usr/bin/env python3
"""Check shader/header edits and repeated guest-only builds leave the host alone.

Run through the local build wrapper where one is required, e.g.:
  tcbuild.sh python3 core/tests/hotReloadLifecycle/test_shader_rebuild.py BUILD_DIR
Use --config for a multi-config generator. No graphics runtime is needed. The
daily display lane passes the test binary instead (display-test); the build
directory is then this test's one configured build-* dir that holds a guest.
"""
import argparse
import os
from pathlib import Path
import subprocess
import time


def bump(path):
    # An explicit future mtime: a coarse-timestamp filesystem cannot make the
    # edit look older than the generated header.
    stamp = max(time.time(), path.stat().st_mtime) + 2
    os.utime(path, (stamp, stamp))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build', type=Path)
    parser.add_argument('--config', default='RelWithDebInfo')
    args = parser.parse_args()
    project = Path(__file__).resolve().parent
    names = ('libguest.so', 'libguest.dylib', 'guest.dll')

    def guests(build):
        found = [build / args.config / n for n in names if (build / args.config / n).is_file()]
        return found or [build / n for n in names if (build / n).is_file()]

    if args.build.is_file():
        # The display lane passes the built binary, which lives in bin/, not in
        # the build tree: use this test's configured build-* dir that holds a guest.
        trees = [cache.parent for cache in sorted(project.glob('build-*/CMakeCache.txt'))
                 if guests(cache.parent)]
        assert len(trees) == 1, f'expected one built build-* dir, found {trees}'
        args.build = trees[0]
    shader = project / 'src/shaders/reloadProbe.glsl'
    app_header = project / 'src/reloadHeaderProbe.h'
    header = shader.with_suffix('.glsl.h')
    libraries = guests(args.build)
    assert len(libraries) == 1, libraries
    guest = libraries[0]
    original = shader.read_bytes()
    original_app_header = app_header.read_bytes()
    before_header = header.read_bytes()
    before_guest = guest.stat().st_mtime_ns
    hosts = [p for p in (project / 'bin/hotReloadLifecycle',
                         project / 'bin/hotReloadLifecycle.exe',
                         project / 'bin/hotReloadLifecycle.app/Contents/MacOS/hotReloadLifecycle')
             if p.is_file()]
    assert len(hosts) == 1, hosts
    host = hosts[0]
    before_host = host.stat().st_mtime_ns
    command = ['cmake', '--build', str(args.build), '--target', 'guest',
               '--config', args.config]

    def build_guest(capture=False):
        result = subprocess.run(command, check=True,
                                stdout=subprocess.PIPE if capture else None,
                                stderr=subprocess.STDOUT, text=True)
        assert host.stat().st_mtime_ns == before_host, 'guest build relinked the host executable'
        return result

    try:
        # A content edit is needed for Ninja's intentional copy_if_different:
        # touching unchanged GLSL regenerates but correctly avoids relinking.
        changed = original.replace(b'vec4(0.25,', b'vec4(0.75,')
        assert changed != original
        shader.write_bytes(changed)
        bump(shader)
        build_guest()
        # One build must do both: on Windows Ninja an edited header used to be
        # picked up by the guest's objects only on the next build.
        assert header.read_bytes() != before_header, 'guest did not regenerate app shader'
        assert guest.stat().st_mtime_ns != before_guest, 'guest did not relink in the same build'
        # A running host cannot be relinked (Windows locks the executable), so
        # a shader edit must reach the guest without touching the host.
        print('Shader edit: header regenerated and guest relinked', flush=True)
        # The first shader build can leave a stale Host object: the next
        # reload used to relink the Host on Windows (#827).
        build_guest()
        bump(shader)
        result = build_guest(capture=True)
        print(result.stdout, end='', flush=True)
        assert 'Compiling shader: reloadProbe.glsl' in result.stdout, 'mtime bump did not run shader compiler'
        print('Shader mtime bump: generator completed', flush=True)
        for generation in (1, 2):
            before_guest = guest.stat().st_mtime_ns
            app_header.write_bytes(original_app_header +
                                   f'\n// Reload header edit {generation}.\n'.encode())
            bump(app_header)
            build_guest()
            assert guest.stat().st_mtime_ns != before_guest, 'app header edit did not relink guest'
            build_guest()
            print(f'App header edit {generation}: guest relinked; host unchanged', flush=True)
    finally:
        shader.write_bytes(original)
        bump(shader)
        app_header.write_bytes(original_app_header)
        bump(app_header)
        build_guest()
    assert header.read_bytes() == before_header, 'restored shader header differs'


if __name__ == '__main__':
    main()
