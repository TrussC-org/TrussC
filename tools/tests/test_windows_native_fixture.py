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
