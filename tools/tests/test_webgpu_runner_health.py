"""Exercise GPU health observation and verdict precedence without a browser."""
from contextlib import redirect_stdout
import base64
import importlib.util
import io
from pathlib import Path
import subprocess
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location(
    'webgpu_runner_health', ROOT / 'core/tests/webgpuUniformGrowth/run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class HealthTests(unittest.TestCase):
    def test_observes_app_device_and_retains_first_failure(self):
        # Run the actual injected JS against device promises/events. This also
        # checks that the wrapper preserves the receiver and request arguments.
        script = r'''
const assert = require('node:assert/strict');
global.window = global;
let lose, listener;
const device = {
    lost: new Promise(resolve => { lose = resolve; }),
    addEventListener(name, callback) {
        assert.equal(name, 'uncapturederror'); listener = callback;
    }
};
global.GPUAdapter = class {
    async requestDevice(descriptor) {
        assert.equal(this, adapter);
        assert.equal(descriptor.label, 'app device');
        return device;
    }
};
const adapter = new GPUAdapter();
'''
        checks = r'''
(async () => {
    assert.equal(await adapter.requestDevice({label: 'app device'}), device);
    assert.equal(window.tcCaptureGpuFailure, null);
    if (process.argv[1] === 'shutdown') {
        lose({reason: 'destroyed', message: 'normal app shutdown'});
        await Promise.resolve();
        assert.equal(window.tcCaptureGpuFailure, null);
        listener({error: {message: 'invalid buffer'}});
        assert.equal(window.tcCaptureGpuFailure, 'FAIL uncaptured GPU error: invalid buffer');
    } else if (process.argv[1] === 'loss') {
        lose({reason: 'destroyed', message: 'forced loss'});
        await Promise.resolve();
        assert.match(window.tcCaptureGpuFailure, /^FAIL GPU device lost: destroyed: forced loss$/);
        listener({error: {message: 'later error'}});
        assert.match(window.tcCaptureGpuFailure, /forced loss$/);
    } else {
        listener({error: {message: 'invalid buffer'}});
        assert.equal(window.tcCaptureGpuFailure, 'FAIL uncaptured GPU error: invalid buffer');
        lose({reason: 'destroyed', message: 'later loss'});
        await Promise.resolve();
        assert.match(window.tcCaptureGpuFailure, /invalid buffer$/);
    }
})().catch(error => { console.error(error); process.exitCode = 1; });
'''
        for mode in ('loss', 'error', 'shutdown'):
            with self.subTest(mode=mode):
                health_script = runner.GPU_HEALTH_SCRIPT.replace(
                    'ALLOW_DEVICE_DESTRUCTION', 'true' if mode == 'shutdown' else 'false')
                subprocess.run(['node', '-e', script + health_script + checks, mode], check=True)

    def run_verdict(self, failure=None, during_capture=False, frames=True):
        process = Mock()
        process.poll.return_value = None
        def launch(command, **kwargs):
            profile = next(arg.split('=', 1)[1] for arg in command if arg.startswith('--user-data-dir='))
            Path(profile, 'DevToolsActivePort').write_text('12345\n', encoding='utf-8')
            return process
        connection = Mock()
        captured = False
        def call(method, params=None):
            nonlocal captured
            if method == 'Page.captureScreenshot':
                captured = True
                return {'data': base64.b64encode(b'png').decode()}
            if method == 'Runtime.evaluate':
                expression = params['expression']
                if expression == 'window.tcCaptureGpuFailure':
                    value = failure if not during_capture or captured else None
                else:
                    value = 'PASS frame count'
                return {'result': {'value': value}}
            return {}
        connection.call.side_effect = call
        output = io.StringIO()
        with runner.tempfile.TemporaryDirectory() as directory:
            args = ['run.py', '--screenshot', str(Path(directory, 'capture.png'))]
            args += ['--frames', '1'] if frames else ['--ready', 'window.rafVerdict']
            with patch('sys.argv', args), patch.object(runner, 'AssetServer'), \
                    patch.object(runner.threading, 'Thread'), \
                    patch.object(runner.subprocess, 'Popen', side_effect=launch), \
                    patch.object(runner, 'LogDrain', return_value=Mock(overflow=False, tail=b'')), \
                    patch.object(runner, 'devtools_targets', return_value=[{'type': 'page', 'webSocketDebuggerUrl': 'mock'}]), \
                    patch.object(runner, 'WebSocket', return_value=connection), redirect_stdout(output):
                status = runner.main()
        calls = connection.call.call_args_list
        monitor = next(i for i, call in enumerate(calls) if call.args == (
            'Page.addScriptToEvaluateOnNewDocument', {
                'source': runner.GPU_HEALTH_SCRIPT.replace('ALLOW_DEVICE_DESTRUCTION', 'false')}))
        navigate = next(i for i, call in enumerate(calls) if call.args[0] == 'Page.navigate')
        self.assertLess(monitor, navigate)
        self.assertEqual(status, 1 if failure else 0)
        self.assertIn(failure or 'PASS frame count', output.getvalue())
        if failure:
            self.assertNotIn('PASS frame count', output.getvalue())

    def test_healthy_fixed_frame_passes(self):
        self.run_verdict()

    def test_device_loss_overrides_fixed_frame_pass(self):
        self.run_verdict('FAIL GPU device lost: destroyed')

    def test_uncaptured_error_overrides_raf_count_pass(self):
        self.run_verdict('FAIL uncaptured GPU error: invalid buffer', frames=False)

    def test_loss_during_screenshot_overrides_pass(self):
        self.run_verdict('FAIL GPU device lost: destroyed', during_capture=True)

    def test_error_message_containing_pass_still_fails(self):
        self.run_verdict('FAIL uncaptured GPU error: buffer labeled PASS')


if __name__ == '__main__':
    unittest.main()
