"""Offline checks for build_all's short output and log files (#1240).

Fixture outputs stand in for the builds and the test binaries; nothing is
compiled or started.
"""

from contextlib import redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_all_short', ROOT / 'examples/build_all.py')
build_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_all)

PLATFORM = {'os': 'linux', 'build_dir': 'build-linux', 'cmake_generator': None}


def completed(code, text):
    return subprocess.CompletedProcess([], code, text.encode())


class ShortOutputCase(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.output = io.StringIO()
        self.enterContext(redirect_stdout(self.output))
        self.enterContext(patch.object(build_all, 'ROOT_DIR', str(self.root)))
        # Print log paths relative to the temporary root, like a run from the repo root.
        self.enterContext(patch.object(build_all, 'display_path',
                                       lambda p: os.path.relpath(p, str(self.root))))
        build_all.reset_short_output()
        self.options = SimpleNamespace(verbose=False, core_test_timings=[])

    def screen(self):
        return self.output.getvalue()

    def make_test_dir(self, name):
        directory = self.root / 'core' / 'tests' / name
        (directory / 'src').mkdir(parents=True)
        (directory / 'src' / 'main.cpp').touch()
        return str(directory)

    def feed(self, lines, verbose=False, progress=None, cmd=('cmake', '--build', 'x'), fail=False):
        # run_command with run_build replaced by a fixture that replays `lines`.
        def fake_run_build(command, *, echo=True, on_line=None, **kwargs):
            for line in lines:
                if echo:
                    print(line, end='')
                on_line(line if line.endswith('\n') else line + '\n')
            if fail:
                raise subprocess.CalledProcessError(2, command)
        with patch.object(build_all, 'run_build', fake_run_build):
            return build_all.run_command(list(cmd), cwd=str(self.root), verbose=verbose,
                                         progress=progress)

    def combined(self, outputs, list_names=None):
        # run_combined_core_tests over fixture test outputs: name -> (code, text) or an exception.
        names = list(outputs)
        tests = [self.make_test_dir(n) for n in names]

        def fake_run(cmd, **kwargs):
            if cmd[-1] == '--list':
                return completed(0, '\n'.join(list_names or names))
            result = outputs[cmd[-1]]
            if isinstance(result, Exception):
                raise result
            return completed(*result)
        with patch.object(build_all, 'build_test_project', return_value=('fake-binary', None)), \
                patch.object(build_all, 'run_command', return_value=True), \
                patch.object(build_all.subprocess, 'run', side_effect=fake_run):
            return build_all.run_combined_core_tests(tests, 'pg', PLATFORM, self.options)


class TestRunScreen(ShortOutputCase):
    def test_exit_77_is_skipped_in_combined_and_final_summaries(self):
        self.assertEqual(self.combined({'a': (0, 'PASS\n'), 'b': (77, 'SKIP unavailable\n')}), 0)
        self.assertIn('allCoreTests: 1/2 passed, 1 skipped', self.screen())
        self.assertIn('SKIP: core/tests/b (exit 77)', self.screen())
        self.assertIn('1/2 passed, 1 skipped', build_all.SHORT.summary(0)[0])
        self.assertEqual(build_all.SHORT.failures, [])
        self.assertIsNone(self.options.core_test_timings[-1][2])
        build_all.print_core_test_timings(self.options.core_test_timings, verbose=False)
        timing_log = self.root / 'build-logs/core-test-times.log'
        self.assertIn('core/tests/b (SKIP)', timing_log.read_text(encoding='utf-8'))

    def test_skipped_unit_test_is_neither_passed_nor_failed(self):
        directory = str(self.root / 'core/tests/unit')
        with patch.object(build_all, 'run_command', return_value=True), \
                patch.object(build_all, 'find_test_binary', return_value='fake'), \
                patch.object(build_all.subprocess, 'run', return_value=completed(77, 'SKIP headers missing\n')):
            rc = build_all.run_test_suite([directory], 'core unit', 'pg', PLATFORM, self.options,
                                          builder=build_all.build_and_run_unit_test)
        self.assertEqual(rc, 0)
        self.assertIn('Core unit tests: 0/1 passed, 1 skipped', self.screen())
        self.assertNotIn('FAILED', self.screen())
        self.assertIn('0/1 passed, 1 skipped', build_all.SHORT.summary(0)[0])

    def test_skip_does_not_hide_a_failure(self):
        self.assertEqual(self.combined({'a': (77, 'SKIP\n'), 'b': (1, 'FAIL broken\n')}), 1)
        self.assertIn('allCoreTests: 0/2 passed, 1 skipped', self.screen())
        self.assertIn('0/2 passed, 1 failed, 1 skipped', build_all.SHORT.summary(1)[0])

    def test_all_pass_prints_dots_and_counts_but_no_pass_lines(self):
        passing = (0, ''.join(f'case{i}: PASS\n' for i in range(50)) + 'ALL PASSED\n')
        rc = self.combined({'a': passing, 'b': passing, 'c': passing})
        self.assertEqual(rc, 0)
        screen = self.screen()
        self.assertNotIn('PASS', screen)
        self.assertIn('Running 3 core tests .........50%.........100%', screen)
        self.assertRegex(screen, r'allCoreTests: 3/3 passed \(build [\d.]+s, runs [\d.]+s\)')
        self.assertLess(len(screen.splitlines()), 6)

    def test_pass_lines_are_in_the_log(self):
        self.combined({'a': (0, 'case1: PASS\ncase2: PASS\n')})
        log = self.root / 'build-logs' / 'core' / 'tests' / 'a.log'
        self.assertIn('case1: PASS\ncase2: PASS\n', log.read_text(encoding='utf-8'))

    def test_fail_line_shows_with_details_and_log_path(self):
        text = ('one: PASS\n'
                'two: FAIL: expected 3, got 4\n'
                '  at widget.cpp:12\n'
                '  value was 4\n'
                'three: PASS\n')
        rc = self.combined({'a': (0, 'ok: PASS\n'), 'b': (1, text)})
        self.assertEqual(rc, 1)
        screen = self.screen()
        self.assertIn('    two: FAIL: expected 3, got 4\n      at widget.cpp:12\n', screen)
        self.assertIn('      value was 4', screen)
        self.assertNotIn('one: PASS', screen)
        self.assertNotIn('three: PASS', screen)
        self.assertIn('core/tests/b: FAILED (run). Log: build-logs/core/tests/b.log', screen)
        self.assertIn('allCoreTests: 1/2 passed', screen)
        self.assertEqual(build_all.SHORT.failures, ['core/tests/b (run)'])

    def test_crash_without_fail_line_shows_log_path_and_tail(self):
        text = ''.join(f'step {i}\n' for i in range(30))
        rc = self.combined({'a': (-11, text)})
        self.assertEqual(rc, 1)
        screen = self.screen()
        self.assertIn('step 29', screen)
        self.assertIn('step 20', screen)
        self.assertNotIn('step 19\n', screen)
        self.assertIn('exit code -11', screen)
        self.assertIn('core/tests/a: FAILED (run). Log: build-logs/core/tests/a.log', screen)

    def test_timeout_shows_tail_and_log_path(self):
        timeout = subprocess.TimeoutExpired('fake', 600, output=b'started\nstill going\n')
        self.assertEqual(self.combined({'a': timeout}), 1)
        screen = self.screen()
        self.assertIn('still going', screen)
        self.assertIn('timed out after 600s', screen)
        self.assertIn('core/tests/a: FAILED (run). Log:', screen)

    def test_fail_words_in_a_passing_run_stay_off_the_screen(self):
        text = ('(a) nonzero FAILED sg pipeline rolls back   PASS\n'
                'LEASE FAILURE parent intact=1 expected=1\n'
                '[23:05:19] [ERROR] [Window] No open window\n')
        self.assertEqual(self.combined({'a': (0, text)}), 0)
        self.assertNotIn('FAIL', self.screen())
        self.assertNotIn('ERROR', self.screen())

    def test_a_pass_line_whose_name_says_failed_is_not_a_fail_line(self):
        text = ('(a) nonzero FAILED sg pipeline rolls back   PASS\n'
                'real: FAIL: broken\n'
                'detail line\n')
        self.assertEqual(self.combined({'a': (1, text)}), 1)
        screen = self.screen()
        self.assertNotIn('rolls back', screen)
        self.assertIn('    real: FAIL: broken\n    detail line', screen)

    def test_non_zero_exit_with_pass_lines_only_still_fails(self):
        self.assertEqual(self.combined({'a': (3, 'x: PASS\n')}), 1)
        self.assertIn('core/tests/a: FAILED (run)', self.screen())

    def test_dot_line_is_broken_before_a_failure_and_continues_after_it(self):
        outputs = {f't{i:02d}': (0, 'ok: PASS\n') for i in range(10)}
        outputs['t03'] = (1, 'bad: FAIL: nope\n')
        self.combined(outputs)
        lines = self.screen().splitlines()
        first = next(i for i, l in enumerate(lines) if l.startswith('Running 10 core tests'))
        self.assertRegex(lines[first], r'^Running 10 core tests \.+$')  # dots up to the failure
        self.assertIn('bad: FAIL: nope', lines[first + 1])
        failed = next(i for i, l in enumerate(lines) if 'FAILED (run)' in l)
        self.assertTrue(lines[failed + 1].startswith('.'))              # dots go on, on a new line
        self.assertTrue(lines[failed + 1].endswith('100%'))

    def test_fail_lines_are_capped(self):
        text = ''.join(f'case{i}: FAIL\n' for i in range(100))
        self.combined({'a': (1, text)})
        screen = self.screen()
        self.assertIn('case29: FAIL', screen)
        self.assertNotIn('case30: FAIL', screen)
        self.assertIn('70 more lines in the log', screen)
        self.assertIn('case99: FAIL', (self.root / 'build-logs/core/tests/a.log').read_text(encoding='utf-8'))

    def test_runs_are_not_verbose_by_default_for_direct_callers(self):
        # run_test_binary keeps the full echo unless told otherwise.
        with patch.object(build_all.subprocess, 'run', return_value=completed(0, 'x: PASS\n')):
            build_all.run_test_binary('fake', str(self.root))
        self.assertIn('x: PASS', self.screen())


class BuildScreen(ShortOutputCase):
    def test_noise_is_hidden_and_progress_becomes_dots(self):
        lines = ['-- The C compiler identification is GNU 13.3.0\n',
                 'Scanning dependencies of target Foo\n']
        lines += [f'[{p:3d}%] Building CXX object a{p}.o\n' for p in range(5, 101, 5)]
        lines += ['[100%] Built target Foo\n']
        progress = build_all.Progress('Building foo')
        progress.begin()
        self.assertTrue(self.feed(lines, progress=progress))
        progress.finish()
        self.assertEqual(self.screen(), 'Building foo .........50%.........100%\n')

    def test_ninja_progress_and_unknown_format(self):
        progress = build_all.Progress('Building foo')
        progress.begin()
        self.feed([f'[{n}/40] Building CXX object a{n}.o\n' for n in range(1, 41)], progress=progress)
        progress.finish()
        self.assertEqual(self.screen(), 'Building foo .........50%.........100%\n')
        self.output.truncate(0)
        self.output.seek(0)
        progress = build_all.Progress('Building bar')
        progress.begin()
        self.feed(['  main.cpp\n', '  other.cpp\n', 'bar.vcxproj -> out\\bar.exe\n'], progress=progress)
        progress.finish()
        self.assertEqual(self.screen(), 'Building bar ..\nbar.vcxproj -> out\\bar.exe\n')

    def test_compile_error_in_the_middle_of_the_dots(self):
        lines = ['[ 10%] Building CXX object a.o\n',
                 '[ 20%] Building CXX object b.o\n',
                 'In file included from b.cpp:1:\n',
                 'b.cpp:7:3: error: use of undeclared identifier \'x\'\n',
                 '[ 50%] Building CXX object c.o\n',
                 '[100%] Building CXX object d.o\n']
        progress = build_all.Progress('Building foo')
        progress.begin()
        self.assertFalse(self.feed(lines, progress=progress, fail=True))
        screen = self.screen()
        self.assertTrue(screen.startswith('Building foo ....\nIn file included from b.cpp:1:\n'
                                          'b.cpp:7:3: error: use of undeclared identifier'))
        # The dots resume on a fresh line, and nothing was glued to a dot line.
        self.assertRegex(screen, r'\n\.+50%\.+100%\n|\n\.+50%\.+100%')
        for line in screen.splitlines():
            if line.startswith(('In file', 'b.cpp')):
                self.assertNotIn('..', line)
        self.assertEqual(build_all.SHORT.errors, ['b.cpp'])
        # The error was on the screen, so the hidden tail is not repeated.
        self.assertEqual(screen.count('Building CXX object'), 0)

    def test_failure_without_diagnostic_brings_the_tail_back(self):
        lines = [f'-- configure step {i}\n' for i in range(20)] + ['CMake stopped here\n']
        self.assertFalse(self.feed(lines, fail=True))
        screen = self.screen()
        self.assertIn('CMake stopped here', screen)
        self.assertIn('-- configure step 15', screen)       # hidden lines come back
        self.assertNotIn('-- configure step 5\n', screen)
        self.assertIn('(exit code 2 of cmake)', screen)

    def test_diagnostics_are_counted(self):
        lines = ['a.cpp:3:4: warning: unused variable\n',
                 'a.cpp:9:1: warning: another\n',
                 'src/b.h:2:2: warning: third\n',
                 'a.cpp:12:1: note: declared here\n',
                 'c.cpp:1:1: fatal error: nothing.h: No such file\n',
                 'C:\\p\\d.cpp(5,2): error C2065: bad\n',
                 'e.cpp(7): warning C4100: unused\n',
                 'CMake Warning at CMakeLists.txt:5 (message):\n',
                 '/usr/bin/ld: x.o: undefined reference to `foo()\'\n',
                 '1 warning generated.\n']
        self.feed(lines)
        short = build_all.SHORT
        self.assertEqual(short.errors, ['c.cpp', 'd.cpp', None])
        self.assertEqual(short.warnings, ['a.cpp', 'a.cpp', 'b.h', 'e.cpp', 'CMakeLists.txt'])
        # Everything that is not noise stays on the screen.
        self.assertEqual(self.screen().splitlines(), [l.rstrip() for l in lines])

    def test_pass_lines_of_a_test_run_by_the_build_are_hidden(self):
        self.feed(['GLCORE\n', 'late periodic phase: PASS\n',
                   'a.cpp:3:1: warning: PASS is a word here\n', 'check x: FAIL: broken\n'])
        self.assertEqual(self.screen(),
                         'GLCORE\na.cpp:3:1: warning: PASS is a word here\ncheck x: FAIL: broken\n')
        self.assertEqual(build_all.SHORT.warnings, ['a.cpp'])

    def test_update_chatter_is_hidden_and_the_note_shown_once(self):
        update = ['Note: TrussC has changed since trusscli was built.\n',
                  "      Run 'trusscli upgrade' to rebuild.\n",
                  'Updating project...\n', 'Updating addons.make...\n', 'Writing CMakeLists.txt...\n',
                  'Writing CMakePresets.json...\n', 'Checking: /x/build-linux\n', 'Not found: build-linux\n',
                  'Update complete!\n', 'Project updated: /x\n']
        for _ in range(2):
            self.feed(update, cmd=('/x/tools/bin/trusscli', 'update', '-p', 'a'))
        self.assertEqual(self.screen().count('Note: TrussC has changed'), 1)
        self.assertNotIn('Updating', self.screen())
        self.assertNotIn('Project updated', self.screen())

    def test_warning_gate_failure_does_not_repeat_the_warnings(self):
        def fake_run_build(command, *, echo=True, on_line=None, **kwargs):
            on_line('a.cpp:1:1: warning: x\n')
            raise RuntimeError('Build warning gate: 1 unexpected warning(s)\na.cpp:1:1: warning: x\n')
        with patch.object(build_all, 'run_build', fake_run_build):
            self.assertFalse(build_all.run_command(['cmake'], cwd=str(self.root)))
        self.assertEqual(self.screen().count('warning: x'), 1)
        self.assertIn('Build warning gate: 1 unexpected warning(s)', self.screen())


class SummaryLines(ShortOutputCase):
    def test_done_has_no_diagnostic_lines_when_all_counts_are_zero(self):
        build_all.SHORT.groups.append(('core tests', 5, 5))
        lines = build_all.SHORT.summary(0)
        self.assertEqual(len(lines), 1)
        self.assertRegex(lines[0], r'^Done: 5/5 passed \(\d+m\d\ds\)\. Logs: build-logs$')

    def test_failed_lists_errors_warnings_and_failures_last(self):
        short = build_all.SHORT
        short.groups.append(('core tests', 3, 5))
        short.errors += ['a.cpp', 'a.cpp', None]
        short.warnings += ['w1.cpp', 'w2.cpp', 'w3.h', 'w4.cpp', 'w1.cpp']
        short.failures += ['core/tests/x (run)', 'core/tests/y (build)']
        lines = short.summary(1)
        self.assertTrue(lines[0].startswith('Failed: 3/5 passed, 2 failed ('))
        self.assertEqual(lines[1], 'ERROR: 3 (a.cpp, link failed)')
        self.assertEqual(lines[2], 'WARNING: 5 (w1.cpp, w2.cpp, w3.h, +1 more)')
        self.assertEqual(lines[3], 'FAIL: 2 (core/tests/x (run), core/tests/y (build))')

    def test_main_prints_the_summary_after_a_short_run(self):
        def suite(tests, label, pg, info, args, builder=None):
            build_all.SHORT.groups.append((label, 2, 2))
            build_all.say('Addon tests: 2/2 passed')
            return 0
        argv = ['build_all.py', '--addon-tests-only']
        with patch.object(sys, 'argv', argv), \
                patch.object(build_all, 'get_platform_info', return_value=PLATFORM), \
                patch.object(build_all, 'find_project_generator', return_value='pg'), \
                patch.object(build_all, 'find_addon_tests', return_value=['x']), \
                patch.object(build_all, 'run_test_suite', suite), \
                self.assertRaises(SystemExit) as raised:
            build_all.main()
        self.assertEqual(raised.exception.code, 0)
        lines = self.screen().splitlines()
        self.assertTrue(lines[0].startswith('build_all (linux): Addon tests (build + run). Full output: build-logs/'))
        self.assertTrue(lines[-1].startswith('Done: 2/2 passed'))
        self.assertNotIn('=== TrussC', self.screen())

    def test_main_verbose_keeps_the_old_header_and_no_summary(self):
        argv = ['build_all.py', '--addon-tests-only', '--verbose']
        with patch.object(sys, 'argv', argv), \
                patch.object(build_all, 'get_platform_info', return_value=PLATFORM), \
                patch.object(build_all, 'find_project_generator', return_value='pg'), \
                patch.object(build_all, 'find_addon_tests', return_value=['x']), \
                patch.object(build_all, 'run_test_suite', return_value=0), \
                self.assertRaises(SystemExit):
            build_all.main()
        screen = self.screen()
        self.assertIn('=== TrussC Examples Batch Build (Python) ===', screen)
        self.assertIn('Mode: Addon tests (build + run)', screen)
        self.assertNotIn('Done:', screen)
        build_all.VERBOSE = True   # main() switched the module default


class VerboseOutput(ShortOutputCase):
    def setUp(self):
        super().setUp()
        self.options = SimpleNamespace(verbose=True, core_test_timings=[])

    def test_verbose_run_prints_every_line(self):
        out = 'a: PASS\nb: PASS\nc: FAIL: no\n'
        self.combined({'a': (0, out)})
        screen = self.screen()
        for expected in ('[1/1] Running: core/tests/a (allCoreTests a)', 'a: PASS', 'b: PASS',
                         'c: FAIL: no', 'Passed!', '=== Core Test Summary ===', 'All core tests passed!',
                         'Wall time:'):
            self.assertIn(expected, screen)
        self.assertNotIn('Done:', screen)

    def test_verbose_summary_reports_exit_77_as_skipped(self):
        self.assertEqual(self.combined({'a': (77, 'SKIP headers missing\n')}), 0)
        self.assertIn('Passed: 0', self.screen())
        self.assertIn('Skipped: 1', self.screen())
        self.assertNotIn('Passed!', self.screen())
        self.assertNotIn('All core tests passed!', self.screen())

    def test_verbose_build_echoes_and_hides_nothing(self):
        lines = ['-- configure\n', '[ 50%] Building CXX object a.o\n', 'a.cpp:1:1: warning: x\n']
        self.feed(lines, verbose=True)
        self.assertEqual(self.screen(), ''.join(lines))
        self.assertEqual(build_all.SHORT.warnings, [])

    def test_verbose_still_writes_the_log(self):
        self.combined({'a': (0, 'a: PASS\n')})
        self.assertIn('a: PASS', (self.root / 'build-logs/core/tests/a.log').read_text(encoding='utf-8'))

    def test_verbose_run_command_passes_echo_to_run_build(self):
        with patch.object(build_all, 'run_build') as run:
            build_all.run_command(['cmake'], cwd=str(self.root), verbose=True)
            self.assertTrue(run.call_args.kwargs['echo'])
            build_all.run_command(['cmake'], cwd=str(self.root), verbose=False)
            self.assertFalse(run.call_args.kwargs['echo'])


class LogFiles(ShortOutputCase):
    def test_log_holds_commands_and_full_output_and_is_overwritten(self):
        with build_all.unit_log(str(self.root / 'core/tests/foo')):
            self.feed(['-- configure\n', '[ 50%] Building a.o\n', 'a.cpp:1:1: warning: w\n'],
                      cmd=('cmake', '--build', 'b'))
        log = self.root / 'build-logs/core/tests/foo.log'
        text = log.read_text(encoding='utf-8')
        self.assertIn('$ cmake --build b\n', text)
        for line in ('-- configure', '[ 50%] Building a.o', 'a.cpp:1:1: warning: w'):
            self.assertIn(line, text)
        with build_all.unit_log(str(self.root / 'core/tests/foo')):
            self.feed(['second run\n'])
        text = log.read_text(encoding='utf-8')
        self.assertIn('second run', text)
        self.assertNotIn('-- configure', text)
        self.assertEqual(len(list((self.root / 'build-logs').rglob('*.log'))), 1)

    def test_web_log_is_a_separate_file(self):
        with build_all.unit_log(str(self.root / 'core/tests/foo')):
            pass
        with build_all.unit_log(str(self.root / 'core/tests/foo'), '.web'):
            pass
        self.assertTrue((self.root / 'build-logs/core/tests/foo.log').is_file())
        self.assertTrue((self.root / 'build-logs/core/tests/foo.web.log').is_file())

    def test_unwritable_log_folder_does_not_stop_the_run(self):
        with patch.object(build_all, 'RunLog', side_effect=OSError('read-only')):
            with build_all.unit_log(str(self.root / 'core/tests/foo')) as log:
                self.assertIsNone(log)
                self.assertTrue(self.feed(['x\n']))

    def test_timings_table_goes_to_the_log_folder_in_short_output(self):
        build_all.print_core_test_timings([('core/tests/a', 2.0, True), ('core/tests/b', 3.0, False)],
                                          verbose=False)
        self.assertEqual(self.screen(), '')
        text = (self.root / 'build-logs/core-test-times.log').read_text(encoding='utf-8')
        self.assertLess(text.index('core/tests/b'), text.index('core/tests/a'))
        self.assertIn('core/tests/b (FAILED)', text)

    def test_logs_folder_is_ignored_by_git(self):
        result = subprocess.run(['git', 'check-ignore', '-q', 'build-logs/core/tests/foo.log'],
                                cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode == 128:
            self.skipTest('not a git checkout')
        self.assertEqual(result.returncode, 0)


class DotLine(ShortOutputCase):
    def test_progress_steps(self):
        progress = build_all.Progress('Building x')
        progress.begin()
        progress.update(0.52)
        self.assertEqual(self.screen(), 'Building x .........50%')
        progress.update(0.52)           # never moves back or repeats
        progress.update(0.3)
        progress.update(1.0)
        progress.finish()
        self.assertEqual(self.screen(), 'Building x .........50%.........100%\n')

    def test_a_passed_through_line_starts_on_a_new_line(self):
        build_all.write_dots('..')
        build_all.say('warning line')
        build_all.write_dots('..')
        build_all.close_dots()
        build_all.close_dots()
        self.assertEqual(self.screen(), '..\nwarning line\n..\n')

    def test_colored_message_also_closes_the_dot_line(self):
        build_all.write_dots('..')
        build_all.Colors.print('note')
        self.assertEqual(self.screen(), '..\nnote\n')


class CiStaysVerbose(unittest.TestCase):
    def test_every_workflow_invocation_of_build_all_passes_verbose(self):
        # CI logs are read with grep; they keep the full output.
        found = 0
        for workflow in sorted((ROOT / '.github/workflows').glob('*.yml')):
            text = workflow.read_text(encoding='utf-8')
            for match in re.finditer(r'build_all\.py', text):
                line_start = text.rfind('\n', 0, match.start()) + 1
                line = text[line_start:text.find('\n', match.end())].strip()
                if line.startswith(('#', 'chmod')):
                    continue
                found += 1
                with self.subTest(workflow=workflow.name, line=line):
                    self.assertIn('--verbose', text[match.start():match.end() + 200])
        self.assertGreater(found, 5)


if __name__ == '__main__':
    unittest.main()
