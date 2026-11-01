"""Web projects configure through trusscli's preset, including on Windows."""

from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_all', ROOT / 'examples/build_all.py')
build_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_all)


class WebPresetTests(unittest.TestCase):
    def test_web_only_example_uses_preset_without_emcmake_generator_override(self):
        for host in ('windows', 'macos', 'linux'):
            with self.subTest(host=host), \
                    patch.object(build_all, 'get_platform_info', return_value={'os': host}), \
                    patch.object(build_all, 'find_project_generator', return_value='trusscli'), \
                    patch.object(build_all, 'update_target_flags', return_value=['--web']), \
                    patch.object(build_all, 'run_command', return_value=True) as run, \
                    patch('sys.argv', ['build_all.py', '--web-only', '--test-only']), \
                    redirect_stdout(io.StringIO()):
                with self.assertRaises(SystemExit) as exit_result:
                    build_all.main()
                self.assertEqual(exit_result.exception.code, 0)
                commands = [call.args[0] for call in run.call_args_list]
                self.assertIn('--web', commands[0])
                self.assertEqual(commands[1], ['cmake', '--preset', 'web'])
                self.assertEqual(commands[2][:3], ['cmake', '--build', 'build-web'])
                self.assertEqual(len(commands), 3)

    def test_web_project_test_uses_preset(self):
        project = str(ROOT / 'core/tests/trusscliPresets')
        with patch.object(build_all, 'find_node', return_value='node'), \
                patch.object(build_all, 'update_target_flags', return_value=['--web']), \
                patch.object(build_all, 'run_command', return_value=True) as run, \
                patch.object(build_all.os.path, 'isfile', return_value=True), \
                patch.object(build_all, 'run_timed_test_binary', return_value=True), \
                redirect_stdout(io.StringIO()):
            result = build_all.build_and_run_web_test(
                project, 'trusscli', {'os': 'windows'}, SimpleNamespace(verbose=False))
            self.assertEqual(result, (True, None))
            self.assertEqual(run.call_args_list[1].args[0], ['cmake', '--preset', 'web'])


if __name__ == '__main__':
    unittest.main()
