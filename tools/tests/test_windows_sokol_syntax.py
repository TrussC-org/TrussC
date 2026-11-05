"""Optional Windows checker skips absent SDKs and propagates compiler failures."""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CHECK = ROOT / 'tools/check_windows_sokol_syntax.py'


class WindowsSyntaxCheckTests(unittest.TestCase):
    def test_dialog_worker_owns_native_inputs(self):
        source = (ROOT / 'core/platform/win/tcFileDialog_win.cpp').read_text(encoding='utf-8')
        native = source.split('struct NativeDialog {', 1)[1].split('\n};', 1)[0]
        self.assertIn('HWND owner = nullptr;', native)
        self.assertIn('std::wstring title, message, defaultPath, defaultName;', native)
        self.assertIn('std::wstring filterName', native)
        self.assertNotRegex(native, r'\b(?:Window|App|DialogInputs|weak_ptr)\b')
        presenter = source.split('trussc::internal::DialogDismiss presentDialog(', 1)[1]
        presenter = presenter.split('\n} // anonymous namespace', 1)[0]
        preparation, worker = presenter.split('std::thread([native]', 1)
        self.assertIn('inputs.requestingWindow.lock()', preparation)
        self.assertIn('window->isOpen()', preparation)
        self.assertIn('sapp_window_win32_get_hwnd(', preparation)
        for field in ('title', 'message', 'defaultPath', 'defaultName', 'folderSelection'):
            self.assertRegex(preparation, rf'native->{field} = [^;]*inputs\.{field}')
        self.assertNotRegex(worker, r'\b(?:inputs|window|GetActiveWindow|currentAppOwner|currentWindowCtx)\b')
        self.assertIn('runDialog(*native)', worker)
        self.assertIn('native->complete(std::move(result))', worker)
        self.assertIn('}).detach();', worker)
        run_dialog = source.split('trussc::FileDialogResult runDialog(', 1)[1].split(
            'trussc::internal::DialogDismiss presentDialog(', 1)[0]
        self.assertNotRegex(run_dialog, r'\b(?:Window|App|GetActiveWindow|currentWindowCtx)\b')
        self.assertIn('dialog->Show(native.owner)', run_dialog)
        self.assertIn('MessageBoxW(native.owner,', run_dialog)

    def test_all_async_dialogs_use_common_request(self):
        source = (ROOT / 'core/platform/win/tcFileDialog_win.cpp').read_text(encoding='utf-8')
        for kind in ('alert', 'confirm', 'load', 'save'):
            match = re.search(r'void ' + kind + r'DialogAsync\([^\{]+\{(.*?)\n\}', source, re.S)
            self.assertIsNotNone(match, kind)
            self.assertIn('internal::requestAsyncDialog(', match[1])
            self.assertNotRegex(match[1], r'\b' + kind + r'Dialog\(')

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
