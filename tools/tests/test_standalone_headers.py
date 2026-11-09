"""Compile the file I/O and serial headers without an umbrella header (#1328)."""
from pathlib import Path
import shutil
import subprocess
import unittest

ROOT = Path(__file__).resolve().parents[2]


class StandaloneHeaders(unittest.TestCase):
    def test_standalone_includes(self):
        compilers = [path for name in ('clang++', 'g++')
                     if (path := shutil.which(name))]
        if not compilers:
            self.skipTest('requires Clang or GCC')
        for compiler in compilers:
            for header in ('tc/comm/tcSerial.h', 'tc/utils/tcFileIO.h'):
                with self.subTest(compiler=compiler, header=header):
                    result = subprocess.run(
                        [compiler, '-std=c++20', '-Wall', '-Wextra',
                         # Clang diagnoses the missing inline definition; GCC's
                         # equivalent warning has no dedicated warning option.
                         ('-Werror=undefined-inline' if 'clang' in Path(compiler).name
                          else '-Werror'),
                         '-fsyntax-only', '-x', 'c++', '-I', str(ROOT / 'core/include'), '-'],
                        input=f'#include "{header}"\n', text=True, encoding='utf-8',
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
