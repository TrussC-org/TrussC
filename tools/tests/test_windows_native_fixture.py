"""Run the Windows native fixture's close client and launch routing without a GPU."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_web_window_controls import body

ROOT = Path(__file__).resolve().parents[2]
SOURCE = Path(os.environ.get('TC_WIN32_FIXTURE_SOURCE', ROOT / 'core/tests/nativeWindow/win32.cpp'))


class NativeFixtureTests(unittest.TestCase):
    def test_initial_dimensions_use_granted_client_area(self):
        source = SOURCE.read_text(encoding='utf-8')
        predicate = body(source, 'bool initialDimensionsMatchClient(')
        # Exercise the actual paired startup check as well as the constructor predicate.
        pair_start = source.index('    bool firstClamped = false, otherClamped = false;')
        pair_end = source.index('    check("initial drawing', pair_start)
        pair_check = source[pair_start:pair_end]
        code = r'''
#include <cassert>
using DWORD = unsigned long;
constexpr int GWL_STYLE = -16, GWL_EXSTYLE = -20;
constexpr int FALSE = 0, MONITOR_DEFAULTTONEAREST = 2;
struct RECT { long left, top, right, bottom; };
struct MONITORINFO { unsigned cbSize; RECT rcMonitor, rcWork; unsigned dwFlags; };
RECT clients[3]{}, work{100, 50, 2020, 1210}; // 1920x1160 work area, offset origin.
RECT& granted = clients[1];
bool clientAvailable = true, pixelSizes = false;
unsigned dpi = 96;
bool GetClientRect(int hwnd, RECT* client) { *client = clients[hwnd]; return clientAvailable; }
unsigned getDpi(int) { return dpi; }
long GetWindowLongPtrW(int, int index) { return index == GWL_STYLE ? 0x1234 : 0x5678; }
int MonitorFromWindow(int hwnd, int flags) { assert(flags == MONITOR_DEFAULTTONEAREST); return hwnd; }
bool GetMonitorInfoW(int, MONITORINFO* monitor) {
    assert(monitor->cbSize == sizeof(MONITORINFO));
    monitor->rcWork = work;
    monitor->rcMonitor = {0, 0, 3840, 2160}; // Must use the work area, not the full monitor.
    return true;
}
bool adjustRect(RECT* outer, DWORD style, int menu, DWORD ex, unsigned windowDpi) {
    assert(style == 0x1234 && ex == 0x5678 && menu == FALSE && windowDpi == dpi);
    const float scale = float(dpi) / 96.f;
    outer->left -= int(8 * scale); outer->right += int(8 * scale);
    outer->top -= int(31 * scale); outer->bottom += int(8 * scale);
    return true;
}
bool AdjustWindowRectEx(RECT* outer, DWORD style, int menu, DWORD ex) {
    return adjustRect(outer, style, menu, ex, dpi);
}
struct {
    unsigned (*GetDpiForWindow)(int) = getDpi;
    bool (*AdjustWindowRectExForDpi)(RECT*, DWORD, int, DWORD, unsigned) = adjustRect;
} _sapp_tc;
struct _sapp_tc_window_t {
    int hwnd = 1, frame_count = 0, fb_width = 0, fb_height = 0;
    int win_width = 0, win_height = 0;
    float dpi_scale = 0;
    struct { bool no_high_dpi = false; } desc;
};
''' + predicate + r'''
_sapp_tc_window_t metadata(bool highDpi = true, int hwnd = 1) {
    const float scale = float(dpi) / 96.f;
    _sapp_tc_window_t w;
    w.hwnd = hwnd;
    w.desc.no_high_dpi = !highDpi;
    w.win_width = int(float(clients[hwnd].right) / scale + .5f);
    w.win_height = int(float(clients[hwnd].bottom) / scale + .5f);
    w.fb_width = highDpi ? int(clients[hwnd].right) : w.win_width;
    w.fb_height = highDpi ? int(clients[hwnd].bottom) : w.win_height;
    w.dpi_scale = highDpi ? scale : 1.f;
    return w;
}
bool pairResult;
void check(const char*, bool ok) { pairResult = ok; }
bool startupPairPasses(_sapp_tc_window_t* first, _sapp_tc_window_t* other) {
    const float expected = first->desc.no_high_dpi ? 1.f : float(dpi) / 96.f;
''' + pair_check + r'''
    return pairResult;
}
int main() {
    for (unsigned monitorDpi : {96u, 120u, 144u, 192u}) {
        dpi = monitorDpi;
        const float scale = float(dpi) / 96.f;
        for (bool highDpi : {false, true}) {
            for (bool pixels : {false, true}) {
                pixelSizes = pixels;
                const float clientScale = pixels && highDpi ? 1.f : scale;
                for (bool clamped : {false, true}) {
                    if (clamped && clientScale != 2.f) continue;
                    granted = {0, 0, clamped ? 1924 : int(1001 * clientScale + .5f),
                        clamped ? 1159 : int(667 * clientScale + .5f)};
                    auto w = metadata(highDpi);
                    bool wasClamped = !clamped;
                    assert(initialDimensionsMatchClient(&w, &wasClamped));
                    assert(wasClamped == clamped);
                    if (!clamped) {
                        const float renderScale = highDpi ? scale : 1.f;
                        assert(w.fb_width == (pixels ? 1001 : int(1001 * renderScale + .5f)));
                        assert(w.fb_height == (pixels ? 667 : int(667 * renderScale + .5f)));
                    }
                    auto bad = w; ++bad.fb_width; assert(!initialDimensionsMatchClient(&bad));
                    bad = w; ++bad.fb_height; assert(!initialDimensionsMatchClient(&bad));
                    bad = w; ++bad.win_width; assert(!initialDimensionsMatchClient(&bad));
                    bad = w; ++bad.win_height; assert(!initialDimensionsMatchClient(&bad));
                    bad = w; bad.dpi_scale += .25f; assert(!initialDimensionsMatchClient(&bad));
                    bad = w; bad.frame_count = 1; assert(!initialDimensionsMatchClient(&bad));
                    bad = w; bad.fb_width = 0; assert(!initialDimensionsMatchClient(&bad));
                    clientAvailable = false; assert(!initialDimensionsMatchClient(&w));
                    clientAvailable = true;
                    if (clamped && highDpi) {
                        bad = w; bad.fb_width = 2002; bad.fb_height = 1334;
                        assert(!initialDimensionsMatchClient(&bad));
                    }
                    granted.right = 0; assert(!initialDimensionsMatchClient(&w));
                }
                // Metadata alone must not admit an incorrect ordinary size at any DPI.
                const auto savedWork = work;
                work = {0, 0, 3840, 2160};
                granted = {0, 0, int(1001 * clientScale + .5f), int(667 * clientScale + .5f)};
                --granted.right; auto bad = metadata(highDpi); assert(!initialDimensionsMatchClient(&bad));
                granted.right += 2; bad = metadata(highDpi); assert(!initialDimensionsMatchClient(&bad));
                work = savedWork;
            }
        }
    }
    dpi = 144; pixelSizes = true;
    granted = {0, 0, 1502, 1001}; // Ignored --pixels, with self-consistent metadata.
    auto w = metadata();
    assert(!initialDimensionsMatchClient(&w));
    granted = clients[2] = {0, 0, 1001, 667};
    w = metadata(); auto other = metadata(true, 2);
    assert(startupPairPasses(&w, &other));
    --clients[2].bottom; other = metadata(true, 2);
    assert(!startupPairPasses(&w, &other));
    pixelSizes = false;
    granted = {0, 0, 1501, 1001}; // Wrong ordinary logical-size request at 150%.
    w = metadata(); assert(!initialDimensionsMatchClient(&w));

    dpi = 192;
    granted = {0, 0, 1924, 1159}; w = metadata();
    bool clamped = false;
    assert(initialDimensionsMatchClient(&w, &clamped) && clamped);
    clients[2] = {0, 0, 2002, 1334}; other = metadata(true, 2);
    assert(startupPairPasses(&w, &other)); // Only the main grant is clamped.
    clients[2] = {0, 0, 1900, 1150}; other = metadata(true, 2);
    assert(startupPairPasses(&w, &other)); // Different legitimate clamped grants.
    granted = {0, 0, 2003, 1159}; w = metadata();
    assert(!initialDimensionsMatchClient(&w)); // Oversized request never permits growth.
    granted = {0, 0, 1924, 1335}; w = metadata();
    assert(!initialDimensionsMatchClient(&w));
    granted = {0, 0, 0, 1159}; w = metadata(); assert(!initialDimensionsMatchClient(&w));
    granted = {0, 0, 1924, 0}; w = metadata(); assert(!initialDimensionsMatchClient(&w));

    // Fit includes the frame, uses > (not >=), and is independent for each axis.
    work = {0, 0, 2034, 1412}; // Exactly the requested outer dimensions at 200%.
    granted = {0, 0, 2001, 1334}; w = metadata(); assert(!initialDimensionsMatchClient(&w));
    work.right = 2033; assert(initialDimensionsMatchClient(&w)); // Width alone may shrink.
    granted.bottom = 1333; w = metadata(); assert(!initialDimensionsMatchClient(&w));
    work = {0, 0, 2034, 1411};
    granted = {0, 0, 2002, 1333}; // Height alone may shrink.
    w = metadata(); assert(initialDimensionsMatchClient(&w));
    granted.right = 2001; w = metadata(); assert(!initialDimensionsMatchClient(&w));
    _sapp_tc.AdjustWindowRectExForDpi = nullptr;
    granted.right = 2002; w = metadata(); assert(initialDimensionsMatchClient(&w));
}
'''
        code = '#include <initializer_list>\n' + code
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'dimensions.cpp').write_text(code, encoding='utf-8')
            subprocess.run(['c++', '-std=c++20', str(path / 'dimensions.cpp'), '-o', str(path / 'dimensions')], check=True)
            subprocess.run([str(path / 'dimensions')], check=True)

    def test_close_policy_and_session_launch(self):
        source = SOURCE.read_text(encoding='utf-8')
        # Compile the fixture's actual descriptor setup and early launch routing.
        launch = source[source.index('    sapp_desc d{};', source.index('int main(')):
                        source.index('    // Inspect the real launch constructor')]
        callbacks = body(source, 'void close(sapp_window win, void*)')
        if 'bool allowClose(sapp_window win, void*)' in source:
            callbacks = body(source, 'bool allowClose(sapp_window win, void*)') + callbacks
        code = r'''
#include <cstdio>
#include <cstdint>
struct sapp_window { unsigned id; };
struct Window { unsigned win_id; };
Window first{1}, peer{2};
struct {
    Window* windows[2]{&first, &peer};
    struct { bool quit_ordered=false; } app;
} _sapp_tc;
int failures=0, closed=0, quitRequests=0, launches=0, cleanupCalls=0, liveHwnds=0;
bool vetoQuit=false, sessionEnd=false, noHighDpi=false, pixelSizes=false;
bool launchCovered=false, initQuit=false, runLaunch=false;
int launchPolicy=0, legacyFrames=0, cleanupFrames=0;
unsigned lastLaunchId=0;
struct { int presents=0; } *launchProbe=nullptr;
struct sapp_desc {
    int width=0,height=0,_tc_pixel_width=0,_tc_pixel_height=0,_tc_draw_while_hidden=0;
    bool high_dpi=false;
    const char* window_title=nullptr;
    void (*init_cb)()=nullptr, (*frame_cb)()=nullptr, (*cleanup_cb)()=nullptr;
    void (*event_cb)(int)=nullptr;
    bool (*_tc_close_request_cb)(sapp_window,void*)=nullptr;
    void (*_tc_close_cb)(sapp_window,void*)=nullptr;
};
void initialized() {} void frame() {} void legacyEvent(int) {} void cleanup() {}
Window* _sapp_tc_lookup(sapp_window win) {
    for (auto* entry : _sapp_tc.windows) if (entry && entry->win_id==win.id) return entry;
    return nullptr;
}
bool sapp_window_valid(sapp_window win) { return _sapp_tc_lookup(win); }
bool _sapp_tc_win32_request_exit(Window*) { ++quitRequests; return !vetoQuit; }
void sapp_destroy_window(sapp_window win) {
    for (auto*& entry : _sapp_tc.windows) if (entry && entry->win_id==win.id) entry=nullptr;
}
void sapp_quit() { _sapp_tc.app.quit_ordered=true; }
void sapp_run(sapp_desc*) { ++launches; ++cleanupCalls; }
void check(const char* label, bool ok) {
    std::printf("%s: %s\n",label,ok?"PASS":"FAIL"); failures+=!ok;
}
''' + callbacks + '\nint launch() {\n' + launch + r'''
    // Fallthrough is the manual multi-window suite, which requires peers.
    return 2;
}
int main() {
    launch(); // Default mode continues to the manual suite.
    sapp_desc d{};
''' + '\n'.join(line for line in launch.splitlines() if 'd._tc_close_' in line) + r'''
    auto request = [&](unsigned id) {
        if (!d._tc_close_request_cb || d._tc_close_request_cb({id},nullptr))
            if (d._tc_close_cb) d._tc_close_cb({id},nullptr);
    };
    close({2},nullptr);
    check("sibling close disposes without process quit", !sapp_window_valid({2}) && !_sapp_tc.app.quit_ordered && quitRequests==0);
    vetoQuit=true; request(1);
    check("last close veto retains launch window", sapp_window_valid({1}) && !_sapp_tc.app.quit_ordered && quitRequests==1);
    vetoQuit=false; request(1);
    check("accepted last close disposes and quits", !sapp_window_valid({1}) && _sapp_tc.app.quit_ordered && quitRequests==2);
    sessionEnd=true;
    const int result=launch();
    check("session mode returns before peer-dependent suite", result==0 && launches==1 && cleanupCalls==1);
    return failures?1:0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'probe.cpp').write_text(code, encoding='utf-8')
            subprocess.run(['c++', '-std=c++20', str(path / 'probe.cpp'), '-o', str(path / 'probe')], check=True)
            result = subprocess.run([str(path / 'probe')], capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
