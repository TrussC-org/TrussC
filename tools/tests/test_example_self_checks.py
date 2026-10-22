"""Discovery and failure handling for Daily's built example checks."""
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

SCRIPT = Path(__file__).resolve().parents[1] / 'run_example_self_checks.py'
spec = importlib.util.spec_from_file_location('self_checks', SCRIPT)
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ExampleSelfChecksTests(unittest.TestCase):
    def setUp(self):
        display = patch.dict(runner.os.environ, {'DISPLAY': ':9972'})
        display.start()
        self.addCleanup(display.stop)

    def test_discovery_uses_example_sources(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            included = ['examples/graphics/newExample', 'addons/demo/example-new']
            for name in included + ['examples/graphics/ordinary', 'addons/demo/tests']:
                source = root / name / 'src'
                source.mkdir(parents=True)
                (source / 'main.cpp').write_text(
                    '"--self-check"' if not name.endswith('ordinary') else 'int main() {}',
                    encoding='utf-8')
                (source.parent / 'README.md').write_text('--self-check', encoding='utf-8')
            self.assertEqual(set(runner.discover_examples(root)), {root / p for p in included})

    def test_nonzero_exit_and_timeout_fail_but_continue(self):
        root = Path('/checkout')
        examples = [root / 'examples' / name for name in ['failed', 'ok']]
        for outcome in (Mock(returncode=7), subprocess.TimeoutExpired('failed', 60)):
            with self.subTest(outcome=outcome), \
                    patch.object(runner, 'discover_examples', return_value=examples), \
                    patch.object(runner.subprocess, 'run', side_effect=[outcome, Mock(returncode=0)]) as run:
                self.assertEqual(runner.run_checks(root), 1)
                self.assertEqual(run.call_count, 2)
                self.assertEqual(run.call_args_list[0].args[0], ['/checkout/examples/failed/bin/failed', '--self-check'])
                self.assertEqual(run.call_args_list[0].kwargs['cwd'], examples[0])
                self.assertEqual(runner.EXAMPLE_TIMEOUT, 60)
                self.assertTrue(all(call.kwargs['timeout'] == 60 for call in run.call_args_list))

    def test_success_and_empty_discovery(self):
        with patch.object(runner, 'discover_examples', return_value=[Path('/checkout/example')]), \
                patch.object(runner.subprocess, 'run', return_value=Mock(returncode=0)):
            self.assertEqual(runner.run_checks(Path('/checkout')), 0)
        with patch.object(runner, 'discover_examples', return_value=[]):
            self.assertEqual(runner.run_checks(Path('/checkout')), 1)

    def test_missing_binary_reports_path(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            example = root / 'missing'
            example.mkdir()
            output = io.StringIO()
            with patch.object(runner, 'discover_examples', return_value=[example]), \
                    redirect_stdout(output):
                self.assertEqual(runner.run_checks(root), 1)
            self.assertIn(f'binary missing: {example / "bin/missing"}', output.getvalue())
            self.assertIn('FAIL: missing', output.getvalue())

    def test_unset_or_empty_display_fails_before_discovery(self):
        for environment in ({}, {'DISPLAY': ''}):
            with self.subTest(environment=environment), \
                    patch.dict(runner.os.environ, environment, clear=True), \
                    patch.object(runner, 'discover_examples') as discover, \
                    patch.object(runner.subprocess, 'run') as run, \
                    redirect_stdout(io.StringIO()) as output:
                self.assertEqual(runner.run_checks(Path('/checkout')), 1)
                self.assertIn('DISPLAY is unset', output.getvalue())
                discover.assert_not_called()
                run.assert_not_called()


if __name__ == '__main__':
    unittest.main()
