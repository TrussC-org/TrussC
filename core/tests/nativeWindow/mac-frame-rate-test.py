#!/usr/bin/env python3
"""SDK-free replay of production display-link creation and refresh notifications.

Keep the original 26 creation checks and replay screen/backing/configuration
changes through the actual handlers and shared helper, without timing claims.
Invoke through tcbuild.sh; TC_MAC_RATE_HEADER supports a private mutation header.
"""
from pathlib import Path
import os
import runpy
import subprocess
import tempfile

root = Path(__file__).resolve().parents[3]
header = Path(os.environ.get('TC_MAC_RATE_HEADER', root / 'core/include/sokol/sokol_app_tc.h')).read_text(encoding='utf-8')
body = runpy.run_path(str(root / 'core/tests/windowTick/native-event-fixture.py'))['body']
start = header.index('    /* per-window display link:')
end = header.index('    _sapp_tc_win_delegate* del', start)
setup = header[start:end]
helper = body(header, 'static void _sapp_tc_mac_update_frame_rate(')
handlers = ''
for name in ('windowDidChangeScreen', 'windowDidChangeBackingProperties', 'applicationDidChangeScreenParameters'):
    signature = '- (void)' + name + ':(NSNotification*)n'
    handlers += body(header, signature).replace(signature, 'void ' + name + '(Delegate* self, void* n)') + '\n'


def native_api(source):
    # Translate native API expressions only; retain production control flow.
    for old, new in (
        ('[view displayLinkWithTarget:view selector:@selector(tick:)]', 'create_link(view)'),
        ('w->window.screen.maximumFramesPerSecond', 'w->screenRate'),
        ('w->link.preferredFrameRateRange', 'w->link->preferredFrameRateRange'),
        ('self.w', 'self->w'),
    ):
        source = source.replace(old, new)
    return source


source = r'''
#include <cstdio>
#include <initializer_list>
struct Range { float minimum=0, maximum=0, preferred=0; };
Range CAFrameRateRangeMake(float a,float b,float c) { return {a,b,c}; }
struct Link { Range preferredFrameRateRange; };
struct _sapp_tc_window_t { int swap_interval=1; Link* link=nullptr; float screenRate=30; bool ready=true; };
using Record=_sapp_tc_window_t;
struct Delegate { Record* w; };
constexpr auto nil=nullptr;
constexpr int _SAPP_TC_MAX_WINDOWS=32, SAPP_EVENTTYPE_RESIZED=1;
struct { Record* windows[_SAPP_TC_MAX_WINDOWS]={}; } _sapp_tc;
bool failLink=false, dimensionsChanged=false;
int dimensions=0, resizes=0;
Link link;
Link* create_link(int) { link={}; return failLink ? nullptr : &link; }
bool _sapp_tc_mac_update_dimensions(Record*) { ++dimensions; return dimensionsChanged; }
void _sapp_tc_send_type(Record*,int) { ++resizes; }
int failures=0, checks=0;
void check(const char* name,bool ok) {
    ++checks; if (!ok) ++failures;
    std::printf("mac-frame-rate: %s: %s\n",name,ok?"PASS":"FAIL");
}
Record* _sapp_tc_mac_fail(Record*,const char*) { return nullptr; }
''' + native_api(helper) + '\n' + native_api(handlers) + r'''
Record* configure(Record* w) {
    int view=0;
''' + native_api(setup) + r'''
    return w;
}
bool range_is(Record& w,float expected) {
    const Range range=w.link->preferredFrameRateRange;
    return range.minimum==expected && range.maximum==expected && range.preferred==expected;
}
int main() {
    for (float rate : {30.f,60.f,120.f,0.f}) for (int interval : {1,2,3}) {
        Record w; w.swap_interval=interval; w.screenRate=rate;
        check("successful native link setup",configure(&w)==&w && w.link);
        const float expected=(rate>0 ? rate:60.f)/interval;
        check("every interval pins the window display rate",range_is(w,expected));
        if (rate==30 && interval==1)
            check("30Hz interval 1 excludes a 6Hz idle source",w.link->preferredFrameRateRange.minimum>6 && range_is(w,30));
    }
    Record failed; failLink=true;
    check("nil link preserves constructor failure",configure(&failed)==nullptr);
    failLink=false;
    check("all 26 inherited creation checks retained",checks==26);
    for (int interval : {1,2,3}) for (float rate : {30.f,60.f,0.f}) {
        Record w; w.swap_interval=interval; w.screenRate=rate; configure(&w);
        Delegate delegate{&w}; dimensions=resizes=0;
        const float next=rate==60 ? 120.f:60.f;
        w.screenRate=next;
        windowDidChangeScreen(&delegate,nullptr);
        check("screen change 60->120 / 30->60 / fallback->60 refreshes all range fields",
              range_is(w,next/interval) && dimensions==1 && resizes==0);
        w.screenRate=30;
        windowDidChangeBackingProperties(&delegate,nullptr);
        check("backing change refreshes range without dimension change",range_is(w,30.f/interval) && dimensions==2 && resizes==0);
        dimensionsChanged=true; w.screenRate=120;
        windowDidChangeBackingProperties(&delegate,nullptr);
        check("backing change retains resized event delivery",range_is(w,120.f/interval) && resizes==1);
        dimensionsChanged=false;
        w.screenRate=0;
        windowDidChangeScreen(&delegate,nullptr);
        check("missing screen rate uses fallback after screen change",range_is(w,60.f/interval));
        w.screenRate=120;
        windowDidChangeScreen(&delegate,nullptr);
        check("screen rate recovers from fallback to 120Hz",range_is(w,120.f/interval));
        Link peerLink; Record peer; peer.link=&peerLink; peer.screenRate=30; peer.swap_interval=2;
        _sapp_tc.windows[0]=&w; _sapp_tc.windows[1]=&peer;
        w.screenRate=60;
        applicationDidChangeScreenParameters(nullptr,nullptr);
        check("display configuration change refreshes each current screen independently",
              range_is(w,60.f/interval) && range_is(peer,15));
        _sapp_tc.windows[0]=_sapp_tc.windows[1]=nullptr;
    }
    Delegate empty{nullptr};
    windowDidChangeScreen(&empty,nullptr);
    windowDidChangeBackingProperties(&empty,nullptr);
    Record unlinked; Delegate delegate{&unlinked};
    windowDidChangeScreen(&delegate,nullptr);
    windowDidChangeBackingProperties(&delegate,nullptr);
    check("notifications tolerate missing window or link",unlinked.link==nullptr);
    std::printf("mac-frame-rate: %d checks, %d failures\n",checks,failures);
    return failures ? 1:0;
}
'''
with tempfile.TemporaryDirectory(prefix='tc-mac-rate-') as temporary:
    scratch = Path(temporary)
    cpp, binary = scratch / 'probe.cpp', scratch / 'probe'
    cpp.write_text(source, encoding='utf-8')
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', str(cpp), '-o', str(binary)], check=True)
    raise SystemExit(subprocess.run([str(binary)], timeout=30).returncode)
