"""Host compile/link checks of the Android entry bridge and quit callback.

Run through tcbuild.sh. Uses the real sapp_desc declaration and production
bridge code with recording quit/logging seams; no SDK or device required.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class AndroidMissingRunAppTests(unittest.TestCase):
    def test_entry_bridge_and_quit_callback(self):
        source = (ROOT / 'core/platform/android/sokol_impl.cpp').read_text(encoding='utf-8')
        bridge = source[source.index('extern int main();'):source.rindex('#endif')]
        header = (ROOT / 'core/include/TrussC.h').read_text(encoding='utf-8')
        declaration = header[header.index('    extern sapp_desc g_androidDesc;'):].splitlines()[0]
        with tempfile.TemporaryDirectory(prefix='android-entry-') as scratch:
            work = Path(scratch)
            (work / 'seams.h').write_text(r'''
#include "sokol_app_tc.h"
#include <cassert>
#include <sstream>
namespace trussc {
extern std::ostringstream errors;
inline std::ostringstream& logError(const char*) { return errors; }
}
''', encoding='utf-8')
            (work / 'bridge.cpp').write_text('#include "seams.h"\n' + bridge, encoding='utf-8')
            compiler = [os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                        '-Werror', '-I' + str(ROOT / 'core/include/sokol')]
            subprocess.run(compiler + ['-Dmain=tc_user_main', '-c', str(work / 'bridge.cpp'),
                           '-o', str(work / 'bridge.o')], check=True)
            # The missing-runApp translation unit never references the descriptor.
            # A separate object must supply its definition for both variants.
            for configured in (False, True):
                app = 'int tc_user_main() { return 0; }\n'
                if configured:
                    app = ('namespace trussc { namespace internal {\n' + declaration +
                           '\n} }\nvoid setup(void*) {}\n'
                           'int tc_user_main() {\n'
                           '    trussc::internal::g_androidDesc.init_userdata_cb = setup;\n'
                           '    trussc::internal::g_androidDesc.width = 320;\n'
                           '    trussc::internal::g_androidDesc.height = 240;\n'
                           '    return 0;\n}\n')
                (work / 'app.cpp').write_text('#include "seams.h"\n' + app, encoding='utf-8')
                (work / 'check.cpp').write_text('#include "seams.h"\n'
                    'extern sapp_desc sokol_main(int, char**);\n'
                    'extern void setup(void*);\n'
                    'namespace trussc { std::ostringstream errors; }\n'
                    'int quits = 0;\nvoid sapp_quit(void) { ++quits; }\n'
                    'int main() {\n    sapp_desc desc = sokol_main(0, nullptr);\n'
                    '    assert(quits == 0);\n' +
                    ('    assert(desc.init_cb == nullptr);\n'
                     '    assert(desc.init_userdata_cb == setup);\n'
                     '    assert(desc.width == 320 && desc.height == 240);\n'
                     '    assert(trussc::errors.str().empty());\n' if configured else
                     '    assert(desc.init_cb != nullptr);\n'
                     '    assert(desc.init_userdata_cb == nullptr);\n'
                     '    assert(desc.width == 0 && desc.height == 0);\n'
                     '    assert(trussc::errors.str().find("runApp() was never called")'
                     ' != std::string::npos);\n'
                     '    desc.init_cb();\n'
                     '    assert(quits == 1);\n') + '    return 0;\n}\n', encoding='utf-8')
                executable = work / 'entry-check'
                subprocess.run(compiler + [str(work / 'app.cpp'), str(work / 'check.cpp'),
                               str(work / 'bridge.o'),
                               '-o', str(executable)], check=True)
                subprocess.run([str(executable)], check=True)


if __name__ == '__main__':
    unittest.main()
