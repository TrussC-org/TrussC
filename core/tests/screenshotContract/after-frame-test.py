#!/usr/bin/env python3
"""Build/run the production display regression and its drain-order mutation.

Run through tcbuild.sh after the shared Linux GLX core build. The configured
core archive and flags supply production code; Xvfb supplies the display. Temporary
objects/binaries are removed, and only logs/results are optionally retained.
"""
from pathlib import Path
import json
import os
import re
import shlex
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[3]


def main():
    output = os.environ.get('TC_AFTER_FRAME_OUTPUT')
    out = Path(output) if output else None
    if out:
        out.mkdir(parents=True, exist_ok=True)
    flags = ['-std=c++20', '-O0', '-g0', '-DNDEBUG']
    build = ROOT / 'core/build-linux'
    for line in (build / 'CMakeFiles/TrussC.dir/flags.make').read_text(encoding='utf-8').splitlines():
        if line.startswith(('CXX_DEFINES =', 'CXX_INCLUDES =')):
            flags += shlex.split(line.split('=', 1)[1])
    link = shlex.split((ROOT / 'core/tests/allCoreTests/build-linux/CMakeFiles/allCoreTests.dir/link.txt').read_text(encoding='utf-8'))
    libs = link[next(i for i, arg in enumerate(link) if arg.endswith('libTrussC.a')):]
    libs[0] = str(build / 'libTrussC.a')
    source = (ROOT / 'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
    notify = '{ EntryStackGuard guard(AppEntry::AfterFrame); internal::currentWindowContext().coreEvents->afterFrame.notify(); }'
    drain = 'else drainPendingScreenshots();'
    assert source.count(notify) == source.count(drain) == 1
    # Move the actual drain call, leaving notification and presentation intact.
    mutant = source.replace(notify, 'if (!presentation) drainPendingScreenshots();\n    ' + notify)
    mutant = mutant.replace(drain, '')
    rows = []
    with tempfile.TemporaryDirectory(prefix='tc-after-frame-') as directory:
        scratch = Path(directory)
        # The aggregate entry became a headless smoke test (54a8c0d5).
        # Launch the retained display fixture directly, with its assertions intact.
        fixture = scratch / 'after-frame.cpp'
        fixture.write_text('''#include <TrussC.h>
#include <cstdio>
using namespace tc;
namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\\n", name, ok ? "PASS" : "FAIL");
    std::fflush(stdout);
    if (!ok) ++failures;
}
}
#include "''' + str(ROOT / 'core/tests/screenshotContract/src/afterFrame.h') + '''"
int main() {
    WindowSettings settings;
    settings.setSize(40, 30).setHighDpi(false).setTitle("afterFrame screenshot main");
    return tcCoreTest::runDisplayTest<AfterFrameScreenshotApp>(settings, failures);
}
''', encoding='utf-8')
        probe = scratch / 'probe.o'
        subprocess.run(['c++', *flags, '-c', str(fixture), '-o', str(probe)], check=True)
        for variant, content in [('baseline', source), ('drain_before_afterFrame', mutant)]:
            # Relocate app-local includes through the configured core include
            # root, preserving its system-header classification and warnings.
            content = content.replace('#include "internal/', '#include "tc/app/internal/')
            cpp = scratch / (variant + '.cpp')
            cpp.write_text(content, encoding='utf-8')
            obj = scratch / (variant + '.o')
            subprocess.run(['c++', *flags, '-c', str(cpp), '-o', str(obj)], check=True)
            binary = scratch / variant
            subprocess.run(['c++', str(probe), str(obj), *libs, '-o', str(binary)], check=True)
            # Bound (test termination): a hung display process fails after 60 seconds; it never detects a mutation.
            run = subprocess.run(['xvfb-run', '-a', '-s', '-screen 0 1280x720x24', str(binary)],
                                 cwd=scratch, env=dict(os.environ, LIBGL_ALWAYS_SOFTWARE='1',
                                     __EGL_VENDOR_LIBRARY_FILENAMES='/usr/share/glvnd/egl_vendor.d/50_mesa.json',
                                     __GLX_VENDOR_LIBRARY_NAME='mesa', GALLIUM_DRIVER='llvmpipe'),
                                 stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                 encoding='utf-8', timeout=60)
            counts = {window: len(re.findall(r'afterFrame THAT frame ' + window + r' .*FAIL', run.stdout))
                      for window in ('main', 'secondary')}
            completed = all(f'{window} verified all nine afterFrame captures' in run.stdout for window in counts)
            decoded = len(re.findall(r'afterFrame PNG decodes at rendered dimensions\s+PASS', run.stdout))
            if out:
                (out / (variant + '.log')).write_text(run.stdout, encoding='utf-8')
            print(run.stdout, end='')
            rows.append(dict(variant=variant, returncode=run.returncode, pixel_failures=counts,
                             completed=completed, decoded=decoded))
            assert decoded == 18, rows[-1]
            if variant == 'baseline':
                assert run.returncode == 0 and completed and not any(counts.values()), rows[-1]
            else:
                # Compilation, startup, crash and timeout failures never count.
                assert run.returncode == 1 and completed and all(n == 9 for n in counts.values()), rows[-1]
            print(variant, 'PASS', flush=True)
    if out:
        (out / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
