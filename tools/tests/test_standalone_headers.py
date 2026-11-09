"""Compile public headers without an umbrella header (#1328, #1295)."""
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
            for header in ('tc/comm/tcSerial.h', 'tc/utils/tcFileIO.h',
                           'tc/gpu/tcReadbackResult.h'):
                with self.subTest(compiler=compiler, header=header):
                    result = subprocess.run(
                        [compiler, '-std=c++20', '-Wall', '-Wextra',
                         # Clang diagnoses the missing inline definition; GCC's
                         # equivalent warning has no dedicated warning option.
                         ('-Werror=undefined-inline' if header != 'tc/gpu/tcReadbackResult.h'
                          and 'clang' in Path(compiler).name
                          else '-Werror'),
                         '-fsyntax-only', '-x', 'c++', '-I', str(ROOT / 'core/include'), '-'],
                        input=(f'#include "{header}"\n' +
                               ('int main() { trussc::ReadbackResult r; return r.ok(); }\n'
                                if header == 'tc/gpu/tcReadbackResult.h' else '')), text=True, encoding='utf-8',
                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
