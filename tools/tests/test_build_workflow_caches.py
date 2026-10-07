"""Offline checks that Build caches never save on merge_group (#352).

Only boolean expressions and event-name comparisons are supported; unfamiliar
conditions fail closed so they require review instead of silently passing.
"""
import ast
from pathlib import Path
import re
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]


def on_merge_group(expression):
    expression = expression.strip().removeprefix('${{').removesuffix('}}').strip()
    expression = expression.replace('github.event_name', "'merge_group'")
    expression = re.sub(r'\btrue\b', 'True', expression)
    expression = re.sub(r'\bfalse\b', 'False', expression)
    expression = re.sub(r'!(?!=)', ' not ', expression).replace('&&', ' and ').replace('||', ' or ')
    tree = ast.parse(expression.strip(), mode='eval')
    allowed = (ast.Expression, ast.BoolOp, ast.And, ast.Or, ast.UnaryOp, ast.Not,
               ast.Compare, ast.Eq, ast.NotEq, ast.Constant)
    if any(not isinstance(node, allowed) for node in ast.walk(tree)):
        raise ValueError('unsupported cache condition')
    result = eval(compile(tree, '<cache condition>', 'eval'), {'__builtins__': {}})
    if not isinstance(result, bool):
        raise ValueError('cache condition must evaluate to a boolean')
    return result


def can_save_on_merge_group(step):
    return (on_merge_group(step.get('if', 'true'))
            and on_merge_group(step.get('with', {}).get('save', 'true')))


class BuildWorkflowCacheTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(),
                                  Loader=yaml.BaseLoader)

    def test_cache_steps_cannot_save_on_merge_group(self):
        for job_name, job in self.workflow['jobs'].items():
            for step in job.get('steps', []):
                if step.get('uses', '').startswith(('actions/cache@', 'actions/cache/save@',
                                                    'hendrikmuhs/ccache-action@')):
                    with self.subTest(job=job_name, step=step.get('name', step['uses'])):
                        self.assertFalse(can_save_on_merge_group(step))

    def test_sokol_cache_twins(self):
        for job_name in ('build', 'build-android', 'build-web', 'build-ios'):
            with self.subTest(job=job_name):
                steps = self.workflow['jobs'][job_name]['steps']
                saves = [step for step in steps if step.get('name') == 'Cache sokol-shdc']
                restores = [step for step in steps if step.get('name') ==
                            'Cache sokol-shdc (restore only, merge queue)']
                self.assertEqual(len(saves), 1)
                self.assertEqual(len(restores), 1)
                save, restore = saves[0], restores[0]
                self.assertEqual(steps.index(restore), steps.index(save) + 1)
                self.assertEqual(save['uses'], 'actions/cache@v5')
                self.assertEqual(save['if'], "github.event_name != 'merge_group'")
                self.assertEqual(restore['uses'], 'actions/cache/restore@v5')
                self.assertEqual(restore['if'], "github.event_name == 'merge_group'")
                expected = {'path': 'core/tools/sokol-shdc',
                            'key': 'sokol-shdc-v1-${{ runner.os }}'}
                self.assertEqual(save['with'], expected)
                self.assertEqual(restore['with'], expected)

    def test_guard_evaluation(self):
        for step, expected in [
            ({}, True),
            ({'if': "github.event_name != 'merge_group'"}, False),
            ({'if': "${{ github.event_name != 'merge_group' }}"}, False),
            ({'with': {'save': "${{ github.event_name != 'merge_group' }}"}}, False),
            ({'with': {'save': 'false'}}, False),
            ({'if': "github.event_name == 'merge_group'"}, True),
            ({'if': "github.event_name != 'merge_group' || true"}, True),
            ({'with': {'save': "${{ github.event_name == 'merge_group' }}"}}, True),
        ]:
            with self.subTest(step=step):
                self.assertEqual(can_save_on_merge_group(step), expected)
        with self.assertRaises(ValueError):
            on_merge_group('unknown.context')

    def test_regression_runs_in_pr_ci(self):
        matches = [step for step in self.workflow['jobs']['header-state-check']['steps']
                   if step.get('run') == 'python3 tools/tests/test_build_workflow_caches.py -v']
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]['if'], 'always()')


if __name__ == '__main__':
    unittest.main()
