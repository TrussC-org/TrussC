"""Offline regressions for daily scheduling and display-mode discovery.

Evaluate the actual workflow condition's boolean/string subset and GitHub's
implicit success() rule. Hosted-runner execution remains a separate CI check.
"""
import ast
from contextlib import redirect_stdout
import io
from pathlib import Path
import re
import signal
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import run_core_display_tests as display


def should_run(step, outcomes, cancelled=False):
    """Only status functions, step outcomes and boolean comparisons are supported.

    GitHub adds success() unless the condition contains a status function:
    https://docs.github.com/en/actions/reference/workflows-and-actions/expressions#status-check-functions
    """
    successful = not cancelled and all(value != 'failure' for value in outcomes.values())
    expression = step.get('if', 'success()').removeprefix('${{').removesuffix('}}').strip()
    statuses = {'success': successful, 'failure': not successful and not cancelled,
                'cancelled': cancelled, 'always': True}
    has_status = re.search(r'\b(success|failure|cancelled|always)\(\)', expression)
    expression = re.sub(r'\b(success|failure|cancelled|always)\(\)',
                        lambda match: str(statuses[match[1]]), expression)
    expression = re.sub(r'steps\.([\w-]+)\.outcome',
                        lambda match: repr(outcomes[match[1]]), expression)
    expression = re.sub(r'!(?!=)', ' not ', expression).replace('&&', ' and ').replace('||', ' or ')
    tree = ast.parse(expression.strip(), mode='eval')
    allowed = (ast.Expression, ast.BoolOp, ast.And, ast.Or, ast.UnaryOp, ast.Not,
               ast.Compare, ast.Eq, ast.NotEq, ast.Constant)
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError('unsupported workflow condition')
    result = eval(compile(tree, '<workflow condition>', 'eval'), {'__builtins__': {}})
    return bool(result) and (bool(has_status) or successful)


class DailyWebWorkflowTests(unittest.TestCase):
    def setUp(self):
        workflow = yaml.load((ROOT / '.github/workflows/daily.yml').read_text(), Loader=yaml.BaseLoader)
        self.steps = workflow['jobs']['sweep-web']['steps']
        self.examples = next(step for step in self.steps if step.get('name') == 'Build every example (Web)')
        self.tests = next(step for step in self.steps if step.get('name') == 'Build and run core tests (Web, under node)')
        self.example_id = self.examples.get('id', 'unidentified_examples')

    def test_core_tests_run_after_success_or_failure(self):
        self.assertLess(self.steps.index(self.examples), self.steps.index(self.tests))
        for outcome in ('success', 'failure'):
            with self.subTest(example_outcome=outcome):
                self.assertTrue(should_run(self.tests, {self.example_id: outcome}))
        # Failures in either sweep still fail the job; they are not tolerated.
        self.assertNotIn('continue-on-error', self.examples)
        self.assertNotIn('continue-on-error', self.tests)
        self.assertIn('--web-only --core-tests-only --include-daily --verbose', self.tests['run'])

    def test_each_failed_prerequisite_skips_examples_and_tests(self):
        for prerequisite in self.steps[:self.steps.index(self.examples)]:
            with self.subTest(prerequisite=prerequisite.get('name', prerequisite.get('uses'))):
                outcomes = {'prerequisite': 'failure'}
                self.assertFalse(should_run(self.examples, outcomes))
                outcomes[self.example_id] = 'skipped'
                self.assertFalse(should_run(self.tests, outcomes))

    def test_cancelled_job_skips_tests_for_every_example_outcome(self):
        for outcome in ('success', 'failure', 'skipped', 'cancelled'):
            with self.subTest(example_outcome=outcome):
                self.assertFalse(should_run(self.tests, {self.example_id: outcome}, cancelled=True))

    def test_skipped_examples_skip_tests(self):
        self.assertFalse(should_run(self.tests, {self.example_id: 'skipped'}))

    def test_regression_runs_in_pr_ci(self):
        workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(), Loader=yaml.BaseLoader)
        matches = [step for step in workflow['jobs']['header-state-check']['steps']
                   if step.get('run') == 'python3 tools/tests/test_daily_workflow.py -v']
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]['if'], 'always()')


class DailyDisplayWorkflowTests(unittest.TestCase):
    def test_display_step_is_linux_daily_only(self):
        daily = yaml.load((ROOT / '.github/workflows/daily.yml').read_text(), Loader=yaml.BaseLoader)
        steps = daily['jobs']['sweep']['steps']
        matches = [step for step in steps if 'tools/run_core_display_tests.py' in step.get('run', '')]
        self.assertEqual(len(matches), 1)
        step = matches[0]
        self.assertEqual(step['if'], "runner.os == 'Linux'")
        self.assertEqual(step['env']['LIBGL_ALWAYS_SOFTWARE'], '1')
        self.assertEqual(step['env']['__EGL_VENDOR_LIBRARY_FILENAMES'],
                         '/usr/share/glvnd/egl_vendor.d/50_mesa.json')
        self.assertIn('xvfb-run -a', step['run'])
        self.assertIn('libgl1-mesa-dri', step['run'])
        self.assertNotIn('continue-on-error', step)
        core = next(s for s in steps if s.get('name') == 'Build & run core tests')
        self.assertLess(steps.index(core), steps.index(step))
        pr = (ROOT / '.github/workflows/build.yml').read_text()
        self.assertNotIn('run_core_display_tests.py', pr)
        self.assertNotIn('display-test', pr)
        # This step fails the existing sweep, so the existing failure reporter
        # reports it without adding another job or failure-reporting path.
        self.assertIn('sweep', daily['jobs']['report']['needs'])
        self.assertIn("contains(needs.*.result, 'failure')", daily['jobs']['report']['if'])

    def test_marker_fixture_runs_combined_and_own_binary_and_continues_after_failure(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            tests = root / 'core/tests'
            for name in ('fixture', 'own', 'unmarked'):
                test = tests / name
                (test / 'src').mkdir(parents=True)
                (test / 'src/main.cpp').touch()
            (tests / 'fixture/display-test').write_text(
                '# comments and quoted arguments\n{test} --fail\n{test} --window "two words"\n')
            (tests / 'own/own-binary').touch()
            (tests / 'own/display-test').write_text('{test} --gpu-check\n')
            # Real fixture processes record argv and cwd, then return the
            # requested status. No TrussC build/display is needed here.
            for name, combined in (('allCoreTests', True), ('own', False)):
                binary = tests / name / 'bin' / name
                binary.parent.mkdir(parents=True)
                binary.write_text(
                    f'#!{sys.executable}\n'
                    'from pathlib import Path\nimport sys\n'
                    f'args = sys.argv[{2 if combined else 1}:]\n'
                    'with Path("calls").open("a") as log: log.write(repr(args) + "\\n")\n'
                    'sys.exit(7 if "--fail" in args else 0)\n')
                binary.chmod(0o755)
            output = io.StringIO()
            with redirect_stdout(output):
                self.assertEqual(display.run_modes(root), 1)
            self.assertEqual((tests / 'fixture/calls').read_text().splitlines(),
                             ["['--fail']", "['--window', 'two words']"])
            self.assertEqual((tests / 'own/calls').read_text().strip(), "['--gpu-check']")
            self.assertFalse((tests / 'unmarked/calls').exists())
            self.assertIn('FAIL fixture:', output.getvalue())
            self.assertIn('--window', output.getvalue())
            self.assertIn('3 modes, 1 failures', output.getvalue())
            (tests / 'fixture/display-test').write_text('{test} --window\n')
            with redirect_stdout(io.StringIO()):
                self.assertEqual(display.run_modes(root), 0)
            (tests / 'own/bin/own').unlink()
            with redirect_stdout(io.StringIO()):
                self.assertEqual(display.run_modes(root), 1)

    def test_timeout_fails_and_reaps_process_group_without_timing_assertions(self):
        with patch.object(display.subprocess, 'Popen') as popen, \
                patch.object(display.os, 'killpg') as killpg, redirect_stdout(io.StringIO()):
            process = popen.return_value.__enter__.return_value
            process.pid = 12345
            process.wait.side_effect = [subprocess.TimeoutExpired('fixture', display.MODE_TIMEOUT), -9]
            self.assertFalse(display.run_mode(['fixture'], ROOT))
            self.assertTrue(popen.call_args.kwargs['start_new_session'])
            killpg.assert_called_once_with(12345, signal.SIGKILL)
            self.assertEqual(process.wait.call_count, 2)

    def test_markers_require_an_entry_and_empty_sweep_fails(self):
        with tempfile.TemporaryDirectory() as scratch:
            root = Path(scratch)
            with redirect_stdout(io.StringIO()):
                self.assertEqual(display.run_modes(root), 1)
            marker = root / 'core/tests/fixture/display-test'
            marker.parent.mkdir(parents=True)
            marker.write_text('--window\n')
            with self.assertRaisesRegex(ValueError, 'expected one'):
                list(display.discover_modes(root))


if __name__ == '__main__':
    unittest.main()
