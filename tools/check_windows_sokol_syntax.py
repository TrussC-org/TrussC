#!/usr/bin/env python3
"""Cross-target syntax check of the real Windows implementation (no backend stubs).

On Linux, set TC_WINDOWS_SYNTAX_SDK to a MinGW prefix containing usr/, or pass
--sdk-root. A checkout-local SDK can be made from the Ubuntu packages without installing them:
  apt-get download mingw-w64-common mingw-w64-x86-64-dev g++-mingw-w64-x86-64-posix
  for package in *.deb; do dpkg-deb -x "$package" sdk; done
  export TC_WINDOWS_SYNTAX_SDK="$PWD/sdk"
Missing SDK configuration/headers: explicit SKIP, exit status 0.
Optional local check only; CI must not depend on this SDK.
Requires clang++ with the x86_64 Windows target; never links or executes Windows code.
"""
from pathlib import Path
import argparse, os, shutil, subprocess, tempfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--sdk-root', type=Path)
    parser.add_argument('--mutations', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    prefix = args.sdk_root or os.environ.get('TC_WINDOWS_SYNTAX_SDK')
    if not prefix:
        print('SKIP: TC_WINDOWS_SYNTAX_SDK not set')
        return
    sdk = Path(prefix)
    sdk = sdk.resolve()
    compiler = os.environ.get('CXX_WINDOWS_SYNTAX') or shutil.which('clang++')
    cxx = sorted((sdk / 'usr/lib/gcc/x86_64-w64-mingw32').glob('*/include/c++'))
    headers = sdk / 'usr/x86_64-w64-mingw32/include'
    if not cxx or not (headers / 'windows.h').exists():
        print('SKIP: Windows C++/SDK prefix missing:', sdk)
        return
    if not compiler:
        parser.error('clang++ is required')
    cxx = cxx[-1]
    flags = [compiler, '-target', 'x86_64-w64-windows-gnu', '-std=c++20',
             '-fsyntax-only', '-DSOKOL_D3D11']
    for include in [cxx, cxx / 'x86_64-w64-mingw32', headers]:
        flags += ['-isystem', str(include)]
    source = root / 'core/platform/win/sokol_impl.cpp'
    include = root / 'core/include/sokol'
    command = [*flags, '-I', str(include), str(source)]
    print('Checking full Windows sokol_impl.cpp:', ' '.join(command), flush=True)
    subprocess.run(command, check=True)
    # Runtime owns the new control HWND and prestarted watchdog outside sokol.
    # Compile those real TUs too, including the host coordinator and public API.
    for relative in ('core/platform/win/tcPlatform_win.cpp',
                     'core/include/tc/app/tcExit.cpp',
                     'core/include/tc/app/tcWindowRuntime.cpp',
                     'core/tests/osExit/src/main.cpp',
                     'core/tests/nativeWindow/win32.cpp'):
        path = root / relative
        print('Checking Windows Runtime TU:', relative, flush=True)
        subprocess.run([*flags, '-DNOMINMAX', '-I', str(root / 'core/include'),
                        '-I', str(include), '-I', str(root / 'core/include/tc/gpu/shaders'),
                        str(path)], check=True)
    print('Windows implementation syntax: PASS', flush=True)
    if args.mutations:
        # Compile the same production TU with only its QPC helper removed.
        # Keep real declarations, Windows headers and every other backend body.
        with tempfile.TemporaryDirectory(prefix='tc-windows-syntax-') as temp:
            copy = Path(temp) / 'sokol'; copy.mkdir()
            for path in include.iterdir():
                if path.name != 'sokol_app_tc.h':
                    (copy / path.name).symlink_to(path)
            (copy.parent / 'tc').symlink_to(root / 'core/include/tc')
            text = (include / 'sokol_app_tc.h').read_text(encoding='utf-8')
            begin = text.index('static double _sapp_tc_now(void) {', text.index('static void _sapp_tc_win32_close'))
            end = text.index('\n}', begin) + 2
            (copy / 'sokol_app_tc.h').write_text(text[:begin] + text[end:], encoding='utf-8')
            result = subprocess.run([*flags, '-I', str(copy), str(source)],
                                    text=True, encoding='utf-8', stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            print(result.stdout, end='')
            if result.returncode == 0 or "undeclared identifier '_sapp_tc_now'" not in result.stdout:
                raise SystemExit('QPC deletion mutant was not rejected for the missing production helper')
        print('QPC deletion mutant: detected', flush=True)


if __name__ == '__main__':
    main()
