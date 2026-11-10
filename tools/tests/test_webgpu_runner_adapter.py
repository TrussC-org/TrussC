"""Offline checks for the WebGPU runner's software and device adapter modes."""
from contextlib import redirect_stdout
import importlib.util
import io
from pathlib import Path
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'webgpu_runner', ROOT / 'core/tests/webgpuUniformGrowth/run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class AdapterTests(unittest.TestCase):
    def command(self, *arguments):
        # Stop at launch: exercise argument parsing and the actual Chrome
        # command without requiring Chrome, a GPU or listening sockets.
        with patch('sys.argv', ['run.py', '--chrome', 'device-chrome', *arguments]), \
                patch.object(runner, 'AssetServer'), \
                patch.object(runner.threading, 'Thread'), \
                patch.object(runner.subprocess, 'Popen', side_effect=OSError('launch intercepted')) as launch, \
                redirect_stdout(io.StringIO()):
            with self.assertRaisesRegex(OSError, 'launch intercepted'):
                runner.main()
        command = launch.call_args.args[0]
        self.assertEqual(command[0], 'device-chrome')
        self.assertIn('--headless=new', command)
        self.assertIn('--password-store=basic', command)
        self.assertIn('--enable-unsafe-webgpu', command)
        self.assertIn('--remote-debugging-port=0', command)
        self.assertEqual(command[-1], 'about:blank')
        return command

    def test_default_keeps_swiftshader(self):
        command = self.command()
        self.assertIn('--use-angle=swiftshader', command)
        self.assertIn('--use-webgpu-adapter=swiftshader', command)
        self.assertIn('--enable-features=Vulkan', command)
        self.assertIn('--use-vulkan=swiftshader', command)

    def test_explicit_swiftshader(self):
        command = self.command('--adapter', 'swiftshader')
        self.assertIn('--use-angle=swiftshader', command)
        self.assertIn('--use-webgpu-adapter=swiftshader', command)
        self.assertIn('--enable-features=Vulkan', command)
        self.assertIn('--use-vulkan=swiftshader', command)

    def test_device_selection_removes_both_software_overrides(self):
        for arguments in [[], ['--url', 'http://localhost:8631/example.html', '--frames', '60']]:
            with self.subTest(arguments=arguments):
                command = self.command('--adapter', 'default', *arguments)
                self.assertFalse(any(arg.startswith('--use-angle=') for arg in command))
                self.assertFalse(any(arg.startswith('--use-webgpu-adapter=') for arg in command))
                self.assertFalse(any(arg.startswith('--enable-features=') for arg in command))
                self.assertFalse(any(arg.startswith('--use-vulkan=') for arg in command))

    def test_invalid_adapter_stops_before_launch(self):
        with patch('sys.argv', ['run.py', '--adapter', 'unknown']), \
                patch.object(runner.subprocess, 'Popen') as launch, \
                patch('sys.stderr', new_callable=io.StringIO):
            with self.assertRaises(SystemExit) as result:
                runner.main()
        self.assertEqual(result.exception.code, 2)
        launch.assert_not_called()


if __name__ == '__main__':
    unittest.main()
