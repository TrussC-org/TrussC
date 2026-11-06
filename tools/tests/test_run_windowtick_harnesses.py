"""Offline checks for prerequisite failure verdicts in the windowTick runner."""

from contextlib import redirect_stdout
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location(
    'windowtick_runner', Path(__file__).resolve().parents[1] / 'run_windowtick_harnesses.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class PrerequisiteVerdicts(unittest.TestCase):
    def test_configure_build_and_timeout_fail(self):
        for stage, diagnostic in [('configure', 'configuration error'),
                                  ('build', 'compiler error'),
                                  ('build', 'TIMEOUT after 900s')]:
            with self.subTest(stage=stage, diagnostic=diagnostic), \
                    tempfile.TemporaryDirectory() as temporary:
                work = Path(temporary)
                report = work / 'results.json'

                def run(command, env, timeout=900):
                    failing = command[:2] == ['cmake', '--build'] if stage == 'build' else command[0] == 'cmake'
                    if failing:
                        return 1, '\n'.join(['old chatter'] * 12 + [diagnostic])
                    return 0, ''

                output = io.StringIO()
                with patch.object(runner, 'WORK', work), \
                        patch.object(runner, 'run', side_effect=run), \
                        patch('sys.argv', ['runner', '--json', str(report)]), \
                        redirect_stdout(output):
                    self.assertEqual(runner.main(), 1)
                rows = {row['name']: row for row in json.loads(report.read_text(encoding='utf-8'))}
                for name in runner.LINKED - {'late-window-test.py'}:
                    self.assertEqual(rows[name]['status'], 'FAIL')
                for name in ['windowTick-default', 'windowTick--display',
                             'windowTick--resize-first-frame']:
                    self.assertEqual(rows[name]['status'], 'FAIL')
                self.assertEqual(rows['late-window-test.py']['status'], 'SKIP')
                self.assertEqual(rows['creation-settings-test.py']['status'], 'PASS')
                self.assertIn(diagnostic, output.getvalue())
                self.assertEqual(output.getvalue().count('old chatter'), 9)
                self.assertIn(diagnostic, (work / 'prerequisites.log').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
