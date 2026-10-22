"""Discovery and failure handling for Daily's built example checks."""
import importlib.util
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

    def test_failure_missing_binary_and_timeout_fail_but_continue(self):
        root = Path('/checkout')
        examples = [root / 'examples' / name for name in ['ok', 'failed', 'missing', 'hung']]
        outcomes = [Mock(returncode=0), Mock(returncode=7), FileNotFoundError(),
                    subprocess.TimeoutExpired('hung', 60)]
        with patch.object(runner, 'discover_examples', return_value=examples), \
                patch.object(runner.subprocess, 'run', side_effect=outcomes) as run:
            self.assertEqual(runner.run_checks(root), 1)
            self.assertEqual(run.call_count, 4)
            self.assertEqual(run.call_args_list[0].args[0], ['/checkout/examples/ok/bin/ok', '--self-check'])
            self.assertTrue(all(call.kwargs['timeout'] == runner.EXAMPLE_TIMEOUT
                                for call in run.call_args_list))

    def test_success_and_empty_discovery(self):
        with patch.object(runner, 'discover_examples', return_value=[Path('/checkout/example')]), \
                patch.object(runner.subprocess, 'run', return_value=Mock(returncode=0)):
            self.assertEqual(runner.run_checks(Path('/checkout')), 0)
        with patch.object(runner, 'discover_examples', return_value=[]):
            self.assertEqual(runner.run_checks(Path('/checkout')), 1)

    def test_display_export_readiness_and_pid_cleanup_on_failure(self):
        server = Mock()
        server.poll.return_value = None
        def launch(command):
            self.assertEqual(runner.os.environ['DISPLAY'], ':9972')
            self.assertEqual(command[1], ':9972')
            return server
        with patch.dict(runner.os.environ, {'DISPLAY': ':9972'}), \
                patch.object(runner.subprocess, 'Popen', side_effect=launch), \
                patch.object(runner.subprocess, 'run', side_effect=[Mock(returncode=1), Mock(returncode=0)]), \
                patch.object(runner.time, 'sleep'), \
                patch.object(runner, 'run_checks', return_value=1) as checks:
            self.assertEqual(runner.run_with_display(Path('/checkout')), 1)
            checks.assert_called_once()
            server.terminate.assert_called_once()
            server.wait.assert_called_once()

    def test_readiness_deadline_fails_and_stops_server(self):
        server = Mock()
        server.poll.return_value = None
        with patch.object(runner.subprocess, 'Popen', return_value=server), \
                patch.object(runner.subprocess, 'run', return_value=Mock(returncode=1)), \
                patch.object(runner.time, 'monotonic', side_effect=[0, 0, 11, 11]), \
                patch.object(runner, 'run_checks') as checks:
            self.assertEqual(runner.run_with_display(Path('/checkout')), 1)
            checks.assert_not_called()
            server.terminate.assert_called_once()
            server.wait.assert_called_once()

    def test_failed_xvfb_start_does_not_run_checks(self):
        server = Mock()
        server.poll.return_value = 1
        with patch.object(runner.subprocess, 'Popen', return_value=server), \
                patch.object(runner, 'run_checks') as checks:
            self.assertEqual(runner.run_with_display(Path('/checkout')), 1)
            checks.assert_not_called()
            server.terminate.assert_not_called()
            server.wait.assert_called_once()

    def test_probe_error_still_stops_xvfb(self):
        server = Mock()
        server.poll.return_value = None
        with patch.object(runner.subprocess, 'Popen', return_value=server), \
                patch.object(runner.subprocess, 'run', side_effect=FileNotFoundError()), \
                patch.object(runner, 'run_checks') as checks:
            with self.assertRaises(FileNotFoundError):
                runner.run_with_display(Path('/checkout'))
            checks.assert_not_called()
            server.terminate.assert_called_once()
            server.wait.assert_called_once()


if __name__ == '__main__':
    unittest.main()
