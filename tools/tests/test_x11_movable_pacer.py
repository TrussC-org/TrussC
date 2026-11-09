"""Offline production X11 pacing seams. Run through tcbuild.sh.

The frozen pre-change functions and current functions execute identical native
boundaries and synthetic clocks. Single-window traces compare every native call
and each turn's refresh estimate, presentation history and deadline at N=1/2.
No real-time assertions, display, network or git history is required.
"""
from pathlib import Path
import os
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
FIXTURES = Path(__file__).parent / 'fixtures/x11_pacer'


def body(source, marker):
    start = source.index(marker)
    opening = source.index('{', start)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


def main():
    before = (FIXTURES / 'before.inc').read_text(encoding='utf-8')
    source = Path(os.environ.get('TC_X11_PACING_HEADER', ROOT / 'core/include/sokol/sokol_app_tc.h')).read_text(encoding='utf-8')
    markers = [line for line in before.splitlines() if line.startswith('static ') and line.endswith('{')]
    # Helper ordering matches the snapshot; the role selector is new.
    current = '\n\n'.join(body(source, marker) for marker in markers)
    insertion = 'static void _sapp_tc_x11_tick(_sapp_tc_window_t* w) {'
    current = current.replace(insertion, body(source, 'static _sapp_tc_window_t* _sapp_tc_x11_update_pacer(') + '\n' + insertion)
    template = (FIXTURES / 'probe.cpp').read_text(encoding='utf-8')
    with tempfile.TemporaryDirectory(prefix='x11-pacer-', dir=os.environ.get('TMPDIR')) as temporary:
        work = Path(temporary)
        for backend in ('GLCORE', 'GLES3'):
            binaries = []
            for label, functions in [('before', before), ('current', current)]:
                cpp = work / f'{label}.cpp'
                cpp.write_text(template.replace('PRODUCTION_FUNCTIONS', functions), encoding='utf-8')
                binary = work / label
                subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-O1', '-DSOKOL_' + backend,
                                '-DCURRENT=' + str(int(label == 'current')), str(cpp), '-o', str(binary)], check=True)
                binaries.append(binary)
            for extension in (('EXT', 'MESA') if backend == 'GLCORE' else ('EGL',)):
                for interval in (1, 2):
                    for mode in ('steady', 'skip', 'fast'):
                        traces = [subprocess.check_output([str(binary), 'single', str(interval), mode, extension], timeout=20)
                                  for binary in binaries]
                        if mode == 'steady':
                            for trace in traces:
                                samples = [float(line.split()[3]) for line in trace.decode().splitlines() if line.startswith('state ')]
                                assert samples[-1] != samples[0], 'steady native period must exercise refresh feedback'
                        interval_call = f'interval 1 {interval}\n'.encode()
                        assert all(trace.count(interval_call) == 1 for trace in traces), 'initial interval must be applied exactly once'
                        assert traces[0] == traces[1], f'{backend}/{extension} N={interval} {mode}: native/state trace changed\n{traces}'
                        print(f'{backend}/{extension} single visible N={interval} {mode}: identical native calls and pacing decisions', flush=True)
            subprocess.run([str(binaries[1]), 'handoff'], check=True, timeout=20)
            print(f'{backend} handoff and step-2 models: PASS', flush=True)
            for draw in (0, 1):
                traces = [subprocess.check_output([str(binary), 'iconify-single', str(draw)], text=True, encoding='utf-8', timeout=20)
                          for binary in binaries]
                expected = 'current 1 0\ncurrent 0 0\n' if draw else ''
                assert traces[1] == expected, traces
                if backend == 'GLES3' and draw:
                    expected = 'current 1 0\ncurrent 1 0\nswap 1 1\ncurrent 0 0\n'
                assert traces[0] == expected, traces
                print(f'{backend} single iconify draw={draw}: baseline/current native sequence checked', flush=True)
                traces = [subprocess.check_output([str(binary), 'hidden-two', str(draw)], text=True, encoding='utf-8', timeout=20).strip()
                          for binary in binaries]
                assert traces[1] == 'hidden-two hz=60.000000 swaps=0', traces
                # Frozen EGL baseline ON still swaps at the driver's clamped
                # rate. OFF uses the N-divided estimate as its timer period.
                if backend == 'GLES3':
                    assert traces[0] == ('hidden-two hz=60.000000 swaps=120' if draw else
                                         'hidden-two hz=120.000000 swaps=0'), traces
                print(f'{backend} ignored N=2 hidden draw={draw}: before {traces[0]}; current {traces[1]}', flush=True)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
