#!/usr/bin/env python3
"""SDK-free declaration/definition/call-site check; run through tcbuild.sh."""
from pathlib import Path
import json
import os
import re
import runpy
import shutil
import subprocess

root = Path(__file__).resolve().parents[3]
out = Path(os.environ.get('TC_MAC_SYNTAX_OUTPUT', root / '.verification/cp3fu2/mac-syntax'))
out.mkdir(parents=True, exist_ok=True)
helpers = runpy.run_path(str(Path(__file__).with_name('native-event-fixture.py')))
source = (root / 'core/include/sokol/sokol_app_tc.h').read_text(encoding='utf-8')
decl = helpers['declaration'](source, '_sapp_tc_mac_observe')
definition = helpers['definition'](source, '_sapp_tc_mac_observe')
stubs = r'''
#include <cstdint>
enum sapp_event_type { SAPP_EVENTTYPE_INVALID, SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_RESTORED };
constexpr unsigned NSWindowOcclusionStateVisible = 1;
struct Window { unsigned occlusionState; bool miniaturized; explicit operator bool() const { return true; } };
struct _sapp_tc_window_t {
    Window window; bool link_suspended, trust_hidden_signals, occluded, iconified; uint32_t win_id;
};
static void _sapp_tc_mac_select_source(_sapp_tc_window_t*);
static void _sapp_tc_send_type(_sapp_tc_window_t*, sapp_event_type);
static _sapp_tc_window_t* _sapp_tc_mac_live(uint32_t);
static void _sapp_tc_mac_update_suspension(_sapp_tc_window_t*);
'''
# Copy every production observer call expression, including one-argument calls.
calls = re.findall(r'^\s+(_sapp_tc_mac_observe\([^;]+\);)', source, re.M)
assert len(calls) >= 5, calls
callsite = '\nvoid calls(_sapp_tc_window_t* w) { struct { _sapp_tc_window_t* w; } self{w};\n' + '\n'.join(calls) + '\n}\n'
results = []
for name, declaration, expected in [
    ('baseline', decl, 0),
    ('old_forward_declaration', 'static void _sapp_tc_mac_observe(_sapp_tc_window_t* w);\n', 1),
    ('duplicate_default_argument', decl, 1),
]:
    body = definition
    if name == 'old_forward_declaration':
        body = body.replace('sapp_event_type detail)', 'sapp_event_type detail = SAPP_EVENTTYPE_INVALID)')
    if name == 'duplicate_default_argument':
        body = body.replace('sapp_event_type detail)', 'sapp_event_type detail = SAPP_EVENTTYPE_INVALID)')
    path = out / (name + '.cpp')
    path.write_text(stubs + declaration + body + callsite, encoding='utf-8')
    command = [os.environ.get('CXX', 'c++'), '-std=c++20', '-fsyntax-only', str(path)]
    with (out / (name + '.log')).open('w', encoding='utf-8') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    valid = result.returncode == 0 if expected == 0 else result.returncode != 0
    record = dict(name=name, command=command, returncode=result.returncode, valid=valid)
    results.append(record)
    print(record, flush=True)
if clang := shutil.which('clang++'):
    objc = r'''
#include <cstdint>
#define nil ((id)0)
enum sapp_event_type { SAPP_EVENTTYPE_INVALID, SAPP_EVENTTYPE_ICONIFIED, SAPP_EVENTTYPE_RESTORED };
constexpr unsigned NSWindowOcclusionStateVisible = 1;
__attribute__((objc_root_class)) @interface NSObject @end
@class NSNotification, CADisplayLink, NSTimer;
@interface NSWindow : NSObject
@property unsigned occlusionState;
@property bool miniaturized;
@end
struct _sapp_tc_window_t {
    NSWindow* window;
    bool link_suspended, trust_hidden_signals, occluded, iconified, fallback_active;
    uint32_t win_id;
};
static void _sapp_tc_mac_select_source(_sapp_tc_window_t*);
static void _sapp_tc_send_type(_sapp_tc_window_t*, sapp_event_type);
static _sapp_tc_window_t* _sapp_tc_mac_live(uint32_t);
static void _sapp_tc_mac_update_suspension(_sapp_tc_window_t*);
'''
    objc += helpers['declaration'](source, '_sapp_tc_mac_tick') + decl + definition
    for cls, markers in [
        ('_sapp_tc_view', ['- (void)tick:(CADisplayLink*)link', '- (void)fallbackTick:(NSTimer*)timer']),
        ('_sapp_tc_win_delegate', ['- (void)windowDidChangeOcclusionState:(NSNotification*)n',
                                   '- (void)windowDidMiniaturize:(NSNotification*)n',
                                   '- (void)windowDidDeminiaturize:(NSNotification*)n']),
    ]:
        objc += '\n@interface ' + cls + ' : NSObject\n@property (assign) _sapp_tc_window_t* w;\n@end\n'
        objc += '@implementation ' + cls + '\n'
        objc += ''.join(helpers['body'](source, marker) for marker in markers) + '@end\n'
    path = out / 'observer-delegates.mm'
    path.write_text(objc, encoding='utf-8')
    command = [clang, '-std=c++20', '-fsyntax-only', '-fobjc-arc', '-fobjc-runtime=macosx-10.13',
               '-Wno-undefined-internal', str(path)]
    with (out / 'observer-delegates.log').open('w', encoding='utf-8') as log:
        result = subprocess.run(command, stdout=log, stderr=subprocess.STDOUT)
    record = dict(name='objc_observer_delegates', command=command, returncode=result.returncode,
                  valid=result.returncode == 0)
    results.append(record)
    print(record, flush=True)
(out / 'results.json').write_text(json.dumps(results, indent=2), encoding='utf-8')
raise SystemExit(any(not r['valid'] for r in results))
