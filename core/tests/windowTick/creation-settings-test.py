#!/usr/bin/env python3
"""SDK-free secondary settings regression; run through tcbuild.sh.

Compile the actual complete adapter, secondary entry, shared normalization and
native constructor. macOS translation replaces only ObjC API/type syntax; all
production branches, assignments, publication and fullscreen ordering remain.
Mutants must compile, complete, exit 1 and report named failed assertions.
"""
from pathlib import Path
import json
import os
import re
import runpy
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[3]
OUT = Path(os.environ.get('TC_CREATION_SETTINGS_OUTPUT', ROOT / '.verification/creation-settings'))
OUT.mkdir(parents=True, exist_ok=True)
body = runpy.run_path(str(Path(__file__).with_name('native-event-fixture.py')))['body']
NATIVE = (ROOT / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
HEADER = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
TEMPLATE = Path(__file__).with_name('creation-settings.cpp').read_text(encoding='utf-8')
FILES = {'mac': 'mac/tcWindowMac.mm', 'win': 'win/tcWindowWin.cpp',
         'glx': 'linux/tcWindowLinux.cpp', 'egl': 'linux/tcWindowLinux.cpp'}


def replace_once(source, old, new):
    assert source.count(old) == 1, (old, source.count(old))
    return source.replace(old, new, 1)


def mac_api(source):
    # Each replacement is required. Source drift cannot silently bypass a seam.
    replacements = [
        ('(__bridge id<MTLDevice>)', '(const void*)'), ('id<MTLDevice>', 'const void*'),
        ('NSScreen.mainScreen.frame', 'get_screen_rect()'),
        ('[[NSWindow alloc] initWithContentRect:rect styleMask:style\n'
         '                                  backing:NSBackingStoreBuffered defer:NO]', 'make_window(rect,style)'),
        ('[w->window setContentSize:NSMakeSize(', 'set_content_size(w->window,NSMakeSize('),
        ('scale))];', 'scale)));'),
        ('[w->window contentRectForFrameRect:w->window.frame]', 'content_rect(w->window)'),
        ('NSString* ns_title = [NSString stringWithUTF8String:w->title]', 'const char* ns_title = w->title'),
        ('_sapp_tc_view* view = [[_sapp_tc_view alloc] initWithFrame:rect]', 'View* view = new View'),
        ('[CAMetalLayer layer]', 'new Layer'),
        ('[w->window makeFirstResponder:view]', 'make_first_responder(w->window,view)'),
        ('[w->window center]', 'center(w->window)'),
        ('[view displayLinkWithTarget:view selector:@selector(tick:)]', 'new Link'),
        ('[NSScreen mainScreen].maximumFramesPerSecond', '120.0f'),
        ('_sapp_tc_win_delegate* del = [_sapp_tc_win_delegate new]', 'Delegate* del = new Delegate'),
        ('[w->link addToRunLoop:[NSRunLoop mainRunLoop] forMode:NSRunLoopCommonModes]', 'add_link(w)'),
        ('[w->window makeKeyAndOrderFront:nil]', 'mac_show(w)'),
    ]
    for old, new in replacements:
        source = replace_once(source, old, new)
    for name in ('w->window', 'w->layer', 'w->link', 'view', 'del'):
        source = source.replace(name + '.', name + '->')
    return re.sub(r'\bnil\b', 'nullptr', source).replace('= NO;', '= false;').replace('= YES;', '= true;')


def generate(backend, variant):
    port = {'mac': 'mac', 'win': 'win32', 'glx': 'x11', 'egl': 'x11'}[backend]
    adapter = (ROOT / 'core/platform' / FILES[backend]).read_text(encoding='utf-8')
    adapter = body(adapter, 'std::shared_ptr<Window> internal::createSecondaryWindow(')
    # Pick the correct same-named public entry, with no last-occurrence shortcut.
    entry_region = NATIVE[NATIVE.index('static _sapp_tc_window_t* _sapp_tc_' + port + '_create_window'):]
    entry = body(entry_region, 'sapp_window sapp_create_window(')
    constructor_name = '_sapp_tc_' + port + ('_create_window_ex' if backend == 'mac' else '_create_window')
    constructor = body(NATIVE, 'static _sapp_tc_window_t* ' + constructor_name + '(')
    if variant == 'adapter_fullscreen':
        adapter = replace_once(adapter, 'd.fullscreen = settings.fullscreen;', 'd.fullscreen = false;')
    elif variant == 'adapter_interval':
        adapter = replace_once(adapter, 'd.swap_interval = settings.swapInterval;', 'd.swap_interval = 1;')
    elif variant == 'entry_fullscreen':
        entry = replace_once(entry, 'native.fullscreen = desc->fullscreen;', 'native.fullscreen = false;')
    elif variant == 'entry_interval':
        old = ('native.swap_interval = 0;' if port == 'x11' else
               'native.swap_interval = desc->swap_interval > 0 ? desc->swap_interval : 1;')
        entry = replace_once(entry, old, 'native.swap_interval = 1;')
    elif variant == 'record_interval':
        constructor = replace_once(constructor, 'w->swap_interval = d->swap_interval;', 'w->swap_interval = 1;')
    elif variant == 'constructor_fullscreen':
        constructor = replace_once(constructor, 'if (d->fullscreen)', 'if (false && d->fullscreen)')
    elif variant == 'fullscreen_order':
        if backend == 'mac':
            request = '    if (d->fullscreen) _sapp_tc_request_fullscreen(w, true);\n'
            show = '    [w->window makeKeyAndOrderFront:nil];\n'
        elif backend == 'win':
            request = '    if (d->fullscreen) _sapp_tc_win32_set_fullscreen(w, true, SWP_HIDEWINDOW);\n'
            show = '    ShowWindow(w->hwnd, SW_SHOW);\n'
        else:
            request = '    _sapp_tc_x11_show_window(w);\n'
            show = '    if (d->fullscreen) {'
        constructor = replace_once(constructor, request, '')
        constructor = replace_once(constructor, show, show + request)
    elif variant == 'mac_rate':
        constructor = replace_once(constructor, 'if (w->swap_interval > 1)', 'if (false && w->swap_interval > 1)')
    elif variant not in ('baseline', 'win_present'):
        raise AssertionError(variant)
    if port == 'x11' and variant == 'fullscreen_order':
        # In this mutant mapping happens inside the fullscreen block, after
        # the fullscreen request. Windowed windows still need to be shown.
        constructor = replace_once(constructor, '    _sapp_tc_x11_show_window(w);\n', '')
        constructor = replace_once(constructor, '        _sapp_tc_x11_update_dimensions(w);',
                                   '        _sapp_tc_x11_show_window(w);\n        _sapp_tc_x11_update_dimensions(w);')
        constructor = replace_once(constructor, '    return w;', '    if (!d->fullscreen) _sapp_tc_x11_show_window(w);\n    return w;')
    if backend == 'mac':
        constructor = mac_api(constructor)
        request = body(NATIVE, 'static void _sapp_tc_request_fullscreen(')
        request = replace_once(request, '[w->window toggleFullScreen:nil]', 'native_toggle(w)')
        prefix = body(NATIVE, 'struct _sapp_tc_mac_window_attrs') + ';\n'
        wrapper = body(NATIVE, 'static _sapp_tc_window_t* _sapp_tc_mac_create_window(')
        production = prefix + request + constructor + wrapper + entry
    else:
        extra = (body(NATIVE, 'static void _sapp_tc_win32_set_fullscreen(') if backend == 'win' else
                 body(NATIVE, 'static void _sapp_tc_x11_pace_periodic(') +
                 body(NATIVE, 'static void _sapp_tc_x11_set_fullscreen('))
        if backend == 'win':
            swap = body(NATIVE, 'static void _sapp_tc_win32_swap(')
            if variant == 'win_present':
                swap = replace_once(swap, 'Present((UINT)w->swap_interval, flags)', 'Present(1, flags)')
            extra += body(NATIVE, 'static void _sapp_tc_win32_pace(') + swap
        production = extra + constructor + entry
    replacements = {
        '// PRODUCTION_NATIVE': production,
        '// PRODUCTION_SETTINGS': body(HEADER, 'struct WindowSettings {') + ';',
        '// PRODUCTION_BIND': body((ROOT / 'core/platform' / FILES[backend]).read_text(encoding='utf-8'), 'bool bindAdapter('),
        '// PRODUCTION_ADAPTER': adapter,
    }
    result = TEMPLATE
    for marker, text in replacements.items():
        result = replace_once(result, marker, text)
    return result


def main():
    rows = []
    with tempfile.TemporaryDirectory(prefix='creation-settings-') as temporary:
        scratch = Path(temporary)
        for backend in FILES:
            variants = ['baseline', 'adapter_fullscreen', 'adapter_interval', 'entry_fullscreen',
                        'entry_interval', 'record_interval', 'constructor_fullscreen', 'fullscreen_order']
            if backend == 'mac':
                variants.append('mac_rate')
            if backend == 'win':
                variants.append('win_present')
            for variant in variants:
                name = backend + '-' + variant
                code = generate(backend, variant)
                cpp = OUT / (name + '.cpp')
                cpp.write_text(code, encoding='utf-8')
                binary = scratch / name
                command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-I', str(ROOT / 'core/include/sokol'),
                           '-DTEST_' + backend.upper(), '-D' + ('SOKOL_GLES3' if backend == 'egl' else 'SOKOL_GLCORE'),
                           str(cpp), '-o', str(binary)]
                with (OUT / (name + '-build.log')).open('w', encoding='utf-8') as log:
                    subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, check=True)
                run = subprocess.run([str(binary)], capture_output=True, text=True, encoding='utf-8', timeout=15)
                (OUT / (name + '.log')).write_text(run.stdout + run.stderr, encoding='utf-8')
                summary = re.search(r'creation-settings: (\d+) checks, (\d+) failures', run.stdout)
                assert summary, (name, run.returncode, run.stdout, run.stderr)
                count, failed = map(int, summary.groups())
                valid = (run.returncode == 0 and failed == 0) if variant == 'baseline' else (
                    run.returncode == 1 and failed > 0 and 'creation-settings: FAIL:' in run.stdout)
                row = dict(backend=backend, variant=variant, compiled=True, returncode=run.returncode,
                           checks=count, failures=failed, valid=valid)
                rows.append(row)
                (OUT / 'results.json').write_text(json.dumps(rows, indent=2) + '\n', encoding='utf-8')
                print(row, flush=True)
                assert valid, (name, run.stdout, run.stderr)
    print('PASS: production secondary settings on macOS/Win32/X11 GLX/EGL; all semantic mutations detected')


if __name__ == '__main__':
    main()
