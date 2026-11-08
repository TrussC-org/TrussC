"""Run the production iOS frame/cleanup bodies with native calls recorded."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'core/include/sokol/sokol_app_tc.h'


def function(source, signature):
    start = source.index(signature)
    return source[start:source.index('\n}', start) + 2]


class IOSExitTests(unittest.TestCase):
    def test_cleanup_before_native_release_and_process_exit(self):
        source = HEADER.read_text(encoding='utf-8')
        cleanup = function(source, '_SOKOL_PRIVATE void _sapp_tc_call_cleanup(void) {')
        frame = function(source, '_SOKOL_PRIVATE void _sapp_tc_ios_frame(void) {')
        # Only native syntax and backend selection are replaced. The quit branch,
        # cleanup dispatch and exit(code) remain the production code.
        frame = frame.replace('@autoreleasepool', '')
        fixture = r'''
#include <cassert>
#include <cstdlib>
#define _SOKOL_PRIVATE
#define SOKOL_METAL
int phase = 0, code = 0;
bool requestQuit = false;
struct State {
    bool quit_ordered = false, cleanup_called = false;
    int timing = 0;
    struct {
        void (*cleanup_cb)() = nullptr;
        void (*cleanup_userdata_cb)(void*) = nullptr;
        void* user_data = nullptr;
    } desc;
} _sapp_tc;
namespace trussc::internal { int& appExitCode() { return code; } }
void _sapp_tc_timing_update(int*, double) { assert(phase == 0); }
void _sapp_tc_ios_mtl_timing_update() {}
void _sapp_tc_ios_update_dimensions() {}
void _sapp_tc_frame() { assert(phase == 0); phase = 1; _sapp_tc.quit_ordered = requestQuit; }
void cleanupCallback() { assert(phase == 1); phase = 2; }
void cleanupUserdata(void* data) { assert(data == &phase); cleanupCallback(); }
void _sapp_tc_ios_discard_state() { assert(phase == 2); phase = 3; }
void _sapp_tc_discard_state() { assert(phase == 3); phase = 4; }
void atProcessExit() { assert(phase == 4); }
CLEANUP
FRAME
int main(int argc, char** argv) {
    assert(argc == 2);
    code = std::atoi(argv[1]);
    requestQuit = code >= 0;
    if (code == 0) _sapp_tc.desc.cleanup_cb = cleanupCallback;
    else {
        _sapp_tc.desc.cleanup_userdata_cb = cleanupUserdata;
        _sapp_tc.desc.user_data = &phase;
    }
    if (requestQuit) std::atexit(atProcessExit);
    _sapp_tc_ios_frame();
    assert(!requestQuit && phase == 1 && !_sapp_tc.cleanup_called);
}
'''.replace('CLEANUP', cleanup).replace('FRAME', frame)
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'ios_exit.cpp'
            binary = Path(temporary) / 'ios_exit'
            cpp.write_text(fixture, encoding='utf-8')
            subprocess.run([shutil.which('c++') or 'c++', '-std=c++20', '-Wall',
                            '-Wextra', '-Werror', str(cpp), '-o', str(binary)], check=True)
            for code in (-1, 0, 7):
                with self.subTest(code=code):
                    result = subprocess.run([str(binary), str(code)], timeout=20)
                    self.assertEqual(result.returncode, code if code >= 0 else 0)


if __name__ == '__main__':
    unittest.main()
