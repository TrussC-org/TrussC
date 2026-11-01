"""Compile the public opt-out macro in ordinary, host and guest contexts."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('c++'), 'requires a C++ compiler')
class NoHotReloadTests(unittest.TestCase):
    def test_hap_windows_declaration(self):
        # Compile the real declaration with only TrussC.h stubbed; this checks
        # its Windows gate without pretending to compile the Windows backend.
        declaration = (ROOT / 'addons/tcxHap/src/tcxHapDecoder.h').read_text(encoding='utf-8')
        declaration = declaration[:declaration.index('#include <vector>')]
        with tempfile.TemporaryDirectory() as scratch:
            project = Path(scratch)
            (project / 'TrussC.h').write_text(
                'namespace trussc { struct WindowSettings; }\n'
                '#include "tc/app/tcHotReload.h"\n', encoding='utf-8')
            (project / 'hap_decl.h').write_text(declaration, encoding='utf-8')
            source = project / 'hap.cpp'
            source.write_text('#include "hap_decl.h"\nint main() {}\n', encoding='utf-8')
            for defines, fails in ((['TC_HOT_RELOAD_BUILD', 'TC_HOT_RELOAD_GUEST'], False),
                                   (['_WIN32'], False),
                                   (['_WIN32', 'TC_HOT_RELOAD_BUILD'], False),
                                   (['_WIN32', 'TC_HOT_RELOAD_BUILD', 'TC_HOT_RELOAD_GUEST'], True)):
                with self.subTest(defines=defines):
                    result = subprocess.run(
                        [os.environ.get('CXX', 'c++'), '-std=c++20', '-fsyntax-only',
                         '-I', str(project), '-I', str(ROOT / 'core/include'),
                         *['-D' + value for value in defines], str(source)],
                        capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(result.returncode != 0, fails, result.stderr)
                    if fails:
                        self.assertIn('hot reload is not supported here: tcxHap: snappy is not linked '
                                      'into the hot-reload guest on Windows', result.stderr)

    def test_header_and_source_contexts(self):
        with tempfile.TemporaryDirectory() as scratch:
            project = Path(scratch)
            header = project / 'addon.h'
            header.write_text('namespace trussc { struct WindowSettings; }\n'
                              '#include "tc/app/tcHotReload.h"\n'
                              'TC_NO_HOT_RELOAD("addon test reason");\n', encoding='utf-8')
            source = project / 'main.cpp'
            source.write_text('#include "addon.h"\nnamespace user_code {\n'
                              'TC_NO_HOT_RELOAD("user test reason");\n}\n'
                              'int main() { return 0; }\n', encoding='utf-8')
            for defines, fails in (([], False), (['TC_HOT_RELOAD_BUILD'], False),
                                   (['TC_HOT_RELOAD_BUILD', 'TC_HOT_RELOAD_GUEST'], True)):
                with self.subTest(defines=defines):
                    result = subprocess.run(
                        [os.environ.get('CXX', 'c++'), '-std=c++20', '-pedantic-errors',
                         '-Wall', '-Wextra', '-Werror', '-fsyntax-only',
                         '-I', str(ROOT / 'core/include'),
                         *['-D' + value for value in defines], str(source)],
                        capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(result.returncode != 0, fails, result.stderr)
                    if fails:
                        for reason in ('addon test reason', 'user test reason'):
                            self.assertIn('hot reload is not supported here: ' + reason, result.stderr)


if __name__ == '__main__':
    unittest.main()
