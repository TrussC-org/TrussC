"""Offline regression for daily web step scheduling (#355), not a runner emulator.

Evaluate the actual workflow condition's boolean/string subset and GitHub's
implicit success() rule. Hosted-runner execution remains a separate CI check.
"""
import ast
from pathlib import Path
import re
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]


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


if __name__ == '__main__':
    unittest.main()
