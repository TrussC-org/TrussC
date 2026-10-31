"""Offline regression guard for display tests whose runApp never returns on macOS."""

import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location(
    'display_test_verdicts', Path(__file__).resolve().parents[1] / 'check_display_test_verdicts.py')
check = importlib.util.module_from_spec(spec)
spec.loader.exec_module(check)


class DisplayTestVerdicts(unittest.TestCase):
    def test_rejects_post_launch_verdicts(self):
        cases = [
            'runApp<App>(settings); return failures ? 1 : 0;',
            'tc::runApp<App>(settings); check(completed);',
            'const int code = runApp<App>(settings); return code;',
            'check("startup", tc::runApp<App>(settings) == 0);',
            'if (gpu) { runApp<App>(settings); } return failures;',
            'runApp\n < Wrapper<App> >\n (settings); check(completed);',
            'TC_RUN_APP(App, settings); return failures;',
            'return runApp<App>(settings) == 0 ? failures : 1;',
            'auto code = tcCoreTest::runDisplayTest<App>(settings, failures); check(done);',
        ]
        for source in cases:
            with self.subTest(source=source):
                self.assertTrue(check.violations(source, 'core/tests/new/src/main.cpp'))

    def test_accepts_direct_returns_with_callback_checks(self):
        cases = [
            'return runApp<App>(settings);',
            'return tc::runApp<App>(settings);',
            'return ::tc::runApp<App>(settings);',
            'return TC_RUN_APP(App, settings);',
            'return tcCoreTest::runDisplayTest<App>(settings, failures, [] { check(done); });',
            'return tcCoreTest::runDisplayTest<App>(settings, failures, [x] { check(x); });',
            'template<class App> int runDisplayTest(const Settings& s) { return tc::runApp<App>(s); }',
        ]
        for source in cases:
            with self.subTest(source=source):
                self.assertEqual(check.violations(source, 'core/tests/new/src/main.cpp'), [])

    def test_ignores_comments_and_literals_and_preserves_line_numbers(self):
        source = '''// runApp<App>(settings);
/* TC_RUN_APP(App, settings); */
const char* a = "runApp<App>(settings);";
const char* b = R"tag(runApp<App>(settings); " quoted)tag";
runApp<App>(settings);'''
        self.assertEqual([line for line, _ in check.violations(source, 'test.cpp')], [5])

    def test_inactive_platform_branches_are_checked(self):
        source = '#ifdef __APPLE__\nrunApp<App>(settings);\n#endif'
        self.assertEqual(len(check.violations(source, 'test.cpp')), 1)

    def test_exceptions_require_exact_path_individual_annotation_and_reason(self):
        path = 'core/tests/startupExit/src/main.cpp'
        source = ('// runapp-verdict: returning-startup-failure -- mock never calls init\n'
                  'check(runApp<App>(s) == 1);')
        self.assertEqual(check.violations(source, path), [])
        self.assertTrue(check.violations(source, 'core/tests/new/src/main.cpp'))
        self.assertTrue(check.violations(source + '\nrunApp<App>(s);', path))
        self.assertTrue(check.violations(source.replace(' -- mock never calls init', ''), path))

    def test_scans_core_examples_and_addon_tests_including_headers(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            for name in ['core/tests/foo/src/main.cpp', 'core/tests/buildContract/src/main.cpp',
                         'examples/tests/foo/src/main.cpp',
                         'addons/tcxFoo/tests/src/main.cpp', 'addons/tcxFoo/tests-gpu/src/probe.h',
                         'core/tests/foo/build-linux/generated.cpp']:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text('runApp<App>(s); return failures;', encoding="utf-8")
            self.assertEqual(len(check.check(root)), 5)

    def test_setup_quit_post_run_exception_stays_narrow(self):
        path = 'core/tests/headlessConsole/src/windowConsoleChecks.h'
        source = ('// runapp-verdict: returning-setup-quit -- auditor requires final post-run counters\n'
                  'const int result = runApp<SetupQuit>(settings); check(updates == 0);')
        self.assertEqual(check.violations(source, path), [])
        self.assertTrue(check.violations(source, 'core/tests/new/src/main.cpp'))
        self.assertTrue(check.violations(source.split('\n', 1)[1], path))
        self.assertTrue(check.violations(source.replace(' -- auditor requires final post-run counters', ''), path))
        self.assertTrue(check.violations(source + '\nrunApp<Other>(settings);', path))

    def test_repository_display_verdicts(self):
        self.assertEqual(check.check(), [])


if __name__ == '__main__':
    unittest.main()
