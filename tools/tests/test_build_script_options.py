"""Exercise build-script options with stub commands, without configuring a build."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class BuildScriptOptionsTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.scratch.cleanup)
        self.base = Path(self.scratch.name)
        self.bin = self.base / 'stubs'
        self.bin.mkdir()
        self.home = self.base / 'home'
        self.home.mkdir()
        self.calls = self.base / 'calls'
        self.env = dict(os.environ, PATH=f'{self.bin}:{os.environ["PATH"]}',
                        HOME=str(self.home), SHELL='/bin/bash', CALLS=str(self.calls))
        # A read is a failure even when stdin is already at EOF. Exported
        # answers must not cause optional installation in unattended mode.
        hook = self.base / 'hook'
        hook.write_text('read() { echo read >> "$CALLS"; return 1; }\n', encoding='utf-8')
        for lang in ('C', 'CXX', 'OBJC', 'OBJCXX'):
            self.env.pop(f'CMAKE_{lang}_COMPILER_LAUNCHER', None)
        self.env.update(BASH_ENV=str(hook), INSTALL_DESKTOP='yes',
                        INSTALL_PATH_LINK='yes', INSTALL_COMPLETION='yes')
        self.stub('cmake', 'echo "cmake $*" >> "$CALLS"\n'
                  'case "$*" in --build*) exit "${BUILD_RC:-0}";; '
                  '*) exit "${CONFIGURE_RC:-0}";; esac\n')
        self.stub('sudo', 'echo sudo >> "$CALLS"; exit 1\n')
        self.stub('open', 'echo launch >> "$CALLS"\n')
        self.stub('sysctl', 'echo 3\n')

    def stub(self, name, body):
        path = self.bin / name
        path.write_text('#!/bin/sh\n' + body, encoding='utf-8')
        path.chmod(0o755)

    def run_script(self, platform, args, *, host=None, **env):
        name = 'build_linux.sh' if platform == 'Linux' else 'build_mac.command'
        script = self.base / name
        shutil.copyfile(ROOT / 'tools' / name, script)
        deps = self.base / 'install_dependencies_linux.sh'
        deps.write_text('#!/bin/sh\necho deps >> "$CALLS"\n'
                        'cat >> "$CALLS"\nexit 1\n', encoding='utf-8')
        deps.chmod(0o755)
        self.stub('uname', f'echo {host or platform}\n')
        return subprocess.run(['bash', str(script), *args], env=dict(self.env, **env),
                              input='yes\nyes\nyes\n', text=True, encoding="utf-8", capture_output=True,
                              timeout=10)

    def test_help_and_invalid_arguments(self):
        for platform in ('Linux', 'Darwin'):
            for flag in ('-h', '--help'):
                result = self.run_script(platform, [flag])
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                for option in ('--jobs', '--no-deps', '--no-launch', '--non-interactive'):
                    self.assertIn(option, result.stdout)
            for args in (['--unknown'], ['--jobs'], ['--jobs', '0'], ['--jobs', '-1'],
                         ['--jobs', 'abc'], ['--jobs', '1.5']):
                result = self.run_script(platform, args)
                self.assertEqual(result.returncode, 1)
                self.assertIn('Usage:', result.stdout)
        self.assertFalse(self.calls.exists())

    def test_unattended_build(self):
        for platform in ('Linux', 'Darwin'):
            with self.subTest(platform=platform):
                self.calls.unlink(missing_ok=True)
                result = self.run_script(platform, ['--non-interactive', '--no-launch',
                                         '--no-deps', '--jobs', '2'],
                                         CMAKE_BUILD_PARALLEL_LEVEL='7')
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                calls = self.calls.read_text(encoding='utf-8').splitlines()
                self.assertEqual(calls, ['cmake .. -DCMAKE_BUILD_TYPE=MinSizeRel',
                    'cmake --build . --config MinSizeRel --parallel 2'])
                self.assertEqual(list(self.home.iterdir()), [])

    def test_os_check_skips_wait_in_unattended_mode(self):
        for platform, host in (('Linux', 'Darwin'), ('Darwin', 'Linux')):
            result = self.run_script(platform, ['--non-interactive'], host=host)
            self.assertEqual(result.returncode, 1)
            self.assertIn('ERROR: This script is for', result.stdout)
        self.assertFalse(self.calls.exists())

    def test_launch_remains_enabled_by_default(self):
        binary = self.base / 'bin' / 'trusscli'
        binary.parent.mkdir()
        binary.write_text('#!/bin/sh\necho launch >> "$CALLS"\n', encoding='utf-8')
        binary.chmod(0o755)
        for platform in ('Linux', 'Darwin'):
            self.calls.unlink(missing_ok=True)
            result = self.run_script(platform, ['--non-interactive', '--no-deps', '--jobs', '2'])
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(self.calls.read_text(encoding='utf-8').splitlines()[-1], 'launch')

    def test_failure_paths_do_not_read(self):
        for platform in ('Linux', 'Darwin'):
            for failure in ('CONFIGURE_RC', 'BUILD_RC'):
                self.calls.unlink(missing_ok=True)
                result = self.run_script(platform, ['--non-interactive', '--no-launch',
                                         '--no-deps', '--jobs', '2'], **{failure: '1'})
                self.assertEqual(result.returncode, 1)
                self.assertNotIn('read', self.calls.read_text(encoding='utf-8'))
                self.assertNotIn('launch', self.calls.read_text(encoding='utf-8'))

    def test_dependency_prompt_is_declined(self):
        result = self.run_script('Linux', ['--non-interactive', '--no-launch'])
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.calls.read_text(encoding='utf-8'), 'deps\nn\n')

    def test_macos_default_jobs_and_launchers(self):
        for configured, expected in (('', '3'), ('7', '7')):
            self.calls.unlink(missing_ok=True)
            result = self.run_script('Darwin', ['--non-interactive', '--no-launch'],
                                     CMAKE_BUILD_PARALLEL_LEVEL=configured,
                                     CMAKE_CXX_COMPILER_LAUNCHER='cache wrapper')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(f'--parallel {expected}', self.calls.read_text(encoding='utf-8'))
            self.assertIn('-DCMAKE_CXX_COMPILER_LAUNCHER=cache wrapper', self.calls.read_text(encoding='utf-8'))

    def test_workflows_use_options_without_rewriting_scripts(self):
        workflow = (ROOT / '.github/workflows/build.yml').read_text(encoding='utf-8')
        self.assertIn('run: python3 tools/tests/test_build_script_options.py -v', workflow)
        for path in (ROOT / '.github/workflows').glob('*.yml'):
            for line in path.read_text(encoding='utf-8').splitlines():
                if 'tools/build_linux.sh' in line or 'tools/build_mac.command' in line:
                    self.assertNotIn('sed ', line)
                    if './tools/build_' in line:
                        self.assertIn('--non-interactive --no-launch', line)


if __name__ == '__main__':
    unittest.main()
