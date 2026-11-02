"""Optional Windows checker skips absent SDKs and propagates compiler failures."""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / 'tools/check_windows_sokol_syntax.py'


class WindowsSyntaxCheckTests(unittest.TestCase):
    def run_check(self, script=CHECK, *args, **env):
        environment = dict(os.environ, **env)
        environment.pop('TC_WINDOWS_SYNTAX_SDK', None)
        return subprocess.run([sys.executable, str(script), *map(str, args)],
                              env=environment, capture_output=True, text=True, encoding='utf-8')

    def test_no_sdk(self):
        result = self.run_check(CXX_WINDOWS_SYNTAX='/missing/compiler')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(result.stdout.strip(), 'SKIP: TC_WINDOWS_SYNTAX_SDK not set')

    def test_missing_sdk(self):
        with tempfile.TemporaryDirectory() as temp:
            result = self.run_check(CHECK, '--sdk-root', Path(temp) / 'absent')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('SKIP:', result.stdout)

    @unittest.skipUnless(shutil.which('clang++'), 'clang++ not available')
    def test_actual_syntax_error_fails(self):
        # Run the unchanged checker against an isolated deliberately invalid TU.
        # Empty SDK markers admit compilation; this source needs no SDK headers.
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            script = root / 'tools/check_windows_sokol_syntax.py'
            script.parent.mkdir()
            script.write_bytes(CHECK.read_bytes())
            source = root / 'core/platform/win/sokol_impl.cpp'
            source.parent.mkdir(parents=True)
            source.write_text('int invalid_syntax( ;\n', encoding='utf-8')
            sdk = root / 'sdk'
            (sdk / 'usr/lib/gcc/x86_64-w64-mingw32/test/include/c++').mkdir(parents=True)
            headers = sdk / 'usr/x86_64-w64-mingw32/include'
            headers.mkdir(parents=True)
            (headers / 'windows.h').touch()
            result = self.run_check(script, '--sdk-root', sdk, CXX_WINDOWS_SYNTAX=shutil.which('clang++'))
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('error:', result.stderr)
        self.assertNotIn('SKIP:', result.stdout)
        self.assertNotIn('Windows implementation syntax: PASS', result.stdout)


if __name__ == '__main__':
    unittest.main()
