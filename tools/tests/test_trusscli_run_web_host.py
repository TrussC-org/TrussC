"""Web-run host arguments through the real CLI, without starting a server."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get('TRUSSCLI', ROOT / 'tools/bin/trusscli')).resolve()


@unittest.skipUnless(sys.platform == 'linux', 'uses POSIX command fixtures')
class WebHostTests(unittest.TestCase):
    def setUp(self):
        scratch = tempfile.TemporaryDirectory(prefix='run-web-host-', dir=ROOT)
        self.addCleanup(scratch.cleanup)
        self.base = Path(scratch.name)
        self.commands = self.base / 'commands'
        self.commands.mkdir()
        self.project = self.base / 'sampleApp'
        (self.project / 'src').mkdir(parents=True)
        (self.project / 'bin').mkdir()
        (self.project / 'CMakeLists.txt').write_text('# TRUSSC_DIR\n', encoding='utf-8')
        (self.project / 'addons.make').write_text('', encoding='utf-8')
        (self.project / 'bin/sampleApp.html').write_text('<!doctype html>', encoding='utf-8')
        (self.project / 'CMakePresets.json').write_text(json.dumps({
            'version': 3,
            'configurePresets': [{'name': name, 'binaryDir': '${sourceDir}/build-' + name}
                                 for name in ('linux', 'web')],
            'buildPresets': [{'name': name, 'configurePreset': name}
                             for name in ('linux', 'web')]}), encoding='utf-8')
        self.script('cmake', 'exit 0')
        # Deliberately exclude the real emrun, python3 and browser from PATH.
        self.script('which', 'command -v "$1"')
        self.script('python3', 'printf "%s\\n" "$@" > "$SERVER_ARGS"')
        app = self.project / 'bin/sampleApp'
        app.write_text('#!/bin/sh\necho native-launched\n', encoding='utf-8')
        app.chmod(0o755)

    def script(self, name, body):
        path = self.commands / name
        path.write_text('#!/bin/sh\n' + body + '\n', encoding='utf-8')
        path.chmod(0o755)

    def cli(self, *args):
        return subprocess.run([str(CLI), 'run', '-p', str(self.project), *args],
                              cwd=self.project, capture_output=True, text=True,
                              env=dict(os.environ, PATH=str(self.commands), TRUSSC_DIR=str(ROOT),
                                       SERVER_ARGS=str(self.base / 'args')), timeout=30, encoding='utf-8')

    def test_host_reaches_both_servers(self):
        for emrun in (True, False):
            if emrun:
                self.script('emrun', 'printf "%s\\n" "$@" > "$SERVER_ARGS"')
            else:
                (self.commands / 'emrun').unlink()
            for flags, host in (([], '127.0.0.1'), (['--host'], '0.0.0.0'),
                                (['--host', '127.0.0.1'], '127.0.0.1'),
                                (['--host', '192.0.2.10'], '192.0.2.10'),
                                (['--host', '::1'], '::1')):
                with self.subTest(emrun=emrun, flags=flags):
                    result = self.cli(*flags, '--web')
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    actual = (self.base / 'args').read_text(encoding='utf-8').splitlines()
                    expected = (['--hostname', host, str(self.project / 'bin/sampleApp.html')]
                                if emrun else ['-m', 'http.server', '8000', '--bind', host,
                                               '-d', str(self.project / 'bin')])
                    self.assertEqual(actual, expected)
                    if host in ('127.0.0.1', '::1'):
                        self.assertNotIn('Network:', result.stdout)
                    if host == '0.0.0.0':
                        # One line per usable adapter; loopback and link-local are skipped.
                        for line in result.stdout.splitlines():
                            if line.startswith('Network: http://'):
                                address = line[len('Network: http://'):].split(':')[0]
                                self.assertFalse(address.startswith(('127.', '169.254.')), line)
                    if host == '192.0.2.10':
                        port = 6931 if emrun else 8000
                        self.assertIn(f'Network: http://{host}:{port}/sampleApp.html', result.stdout)
                    if not emrun:
                        self.assertIn('Open http://localhost:8000/sampleApp.html', result.stdout)

    def test_native_warns_and_still_launches(self):
        for flags in (['--host'], ['--host', '127.0.0.1']):
            with self.subTest(flags=flags):
                result = self.cli(*flags)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('Warning: --host is only valid with --web; ignoring it.', result.stderr)
                self.assertIn('native-launched', result.stdout)


if __name__ == '__main__':
    unittest.main()
