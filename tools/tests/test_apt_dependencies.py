"""Offline installer checks: fake package queries and sudo, never invoke apt."""
import json
import os
import shlex
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == 'linux', 'Linux dependency installer')
class AptDependenciesTests(unittest.TestCase):
    def setUp(self):
        release = subprocess.check_output(
            ['bash', '-c', '. /etc/os-release; echo "$ID $ID_LIKE"'], text=True, encoding="utf-8")
        if not any(name in release.split() for name in ('debian', 'ubuntu')):
            self.skipTest('requires a Debian/Ubuntu host')
        self.scratch = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name)
        self.log = self.directory / 'calls.jsonl'
        self.env = dict(os.environ, PATH=f'{self.directory}:{os.environ["PATH"]}',
                        APT_TEST_LOG=str(self.log))
        self.env.pop('TRUSSC_APT_ARCHIVES', None)
        self.stub('dpkg', '#!/bin/bash\nexit 1\n')
        self.stub('sudo', f'#!{sys.executable}\n'
                  'import json, os, sys\n'
                  'with open(os.environ["APT_TEST_LOG"], "a", encoding="utf-8") as log:\n'
                  '    log.write(json.dumps(sys.argv[1:]) + "\\n")\n')

    def stub(self, name, contents):
        path = self.directory / name
        path.write_text(contents, encoding="utf-8")
        path.chmod(0o755)

    def run_installer(self):
        subprocess.run([str(ROOT / 'tools/install_dependencies_linux.sh'), '-y'],
                       env=self.env, check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")
        return [json.loads(line) for line in self.log.read_text(encoding="utf-8").splitlines()]

    def test_cache_option_is_optional_and_preserves_arguments(self):
        baseline = self.run_installer()
        options = ['apt-get', '-o', 'Acquire::http::Timeout=30',
                   '-o', 'Acquire::Retries=3']
        self.assertEqual(baseline[0], options + ['update'])
        self.assertEqual(baseline[1][:7], options + ['install', '-y'])
        self.assertGreater(len(baseline[1]), 7)
        self.assertIn("libjpeg-dev", baseline[1])
        for archive in ('', str(self.directory / 'archives'),
                        str(self.directory / 'archives with spaces')):
            with self.subTest(archive=archive):
                self.log.unlink()
                self.env['TRUSSC_APT_ARCHIVES'] = archive
                cache = ['-o', f'Dir::Cache::Archives={archive}'] if archive else []
                expected = [options + cache + call[len(options):] for call in baseline]
                self.assertEqual(self.run_installer(), expected)

    def test_ci_jpeg_dependency(self):
        action = (ROOT / '.github/actions/apt-deps/action.yml').read_text(encoding='utf-8')
        release = (ROOT / '.github/workflows/release.yml').read_text(encoding='utf-8')
        self.assertIn('./tools/install_dependencies_linux.sh -y', action)
        installs = [shlex.split(line.strip()) for line in release.replace('\\\n', ' ').splitlines()
                    if 'sudo apt-get install -y' in line]
        self.assertTrue(any('libjpeg-dev' in arguments for arguments in installs))

    def test_installed_packages_do_not_invoke_sudo(self):
        self.stub('dpkg', '#!/bin/bash\nexit 0\n')
        self.env['TRUSSC_APT_ARCHIVES'] = str(self.directory / 'archives')
        result = subprocess.run(
            [str(ROOT / 'tools/install_dependencies_linux.sh'), '-y'],
            env=self.env, check=True, capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertIn('All required packages are already installed.', result.stdout)
        self.assertFalse(self.log.exists())

class CiAptTests(unittest.TestCase):
    def setUp(self):
        import yaml
        self.action = yaml.load((ROOT / '.github/actions/apt-deps/action.yml').read_text(encoding="utf-8"),
                                Loader=yaml.BaseLoader)
        self.install = self.action['runs']['steps'][-1]
        self.workflows = {p.name: yaml.load(p.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
                          for p in (ROOT / '.github/workflows').glob('*.yml')}
        self.release = next(s for s in self.workflows['release.yml']['jobs']['linux']['steps']
                            if s.get('name') == 'Install dependencies')

    def test_default_packages_match_installer(self):
        source = (ROOT / 'tools/install_dependencies_linux.sh').read_text(encoding="utf-8")
        packages = source.split('REQUIRED_PACKAGES_DEBIAN=(')[1].split(')')[0].split()
        self.assertEqual(self.action['inputs']['packages']['default'].split(), packages)

    def test_all_apt_steps_are_cached_and_bounded(self):
        callers = []
        for filename, workflow in self.workflows.items():
            for name, job in workflow['jobs'].items():
                for step in job.get('steps', []):
                    cached = step.get('uses') == './.github/actions/apt-deps'
                    apt = 'apt-get ' in step.get('run', '')
                    if not (cached or apt):
                        continue
                    with self.subTest(workflow=filename, job=name, step=step.get('name')):
                        self.assertEqual(step['timeout-minutes'], '10')
                        self.assertGreaterEqual(int(job.get('timeout-minutes', '360')), 30)
                        if apt:
                            self.assertEqual((filename, name), ('release.yml', 'linux'))
                            self.assertTrue(any(s.get('with', {}).get('path') == '~/.apt-archives'
                                                for s in job['steps']))
                        else:
                            callers.append((filename, name))
        for name in ('build-web', 'build-android', 'render-regression'):
            self.assertIn(('build.yml', name), callers)
        for name in ('sweep', 'sweep-web'):
            self.assertIn(('daily.yml', name), callers)
        extras = {(filename, name, step['name']): step['with']['packages'].split()
                  for filename, workflow in self.workflows.items()
                  for name, job in workflow['jobs'].items()
                  for step in job.get('steps', [])
                  if step.get('uses') == './.github/actions/apt-deps' and 'with' in step}
        self.assertEqual(extras, {
            ('build.yml', 'build', 'Install xrdb for native DPI checks'): ['x11-xserver-utils'],
            ('build.yml', 'render-regression', 'Install software display'):
                ['xvfb', 'libgl1-mesa-dri', 'mesa-utils'],
            ('daily.yml', 'sweep', 'Install software display'):
                ['xvfb', 'xauth', 'libgl1-mesa-dri', 'libglx-mesa0', 'libegl-mesa0']})

    @unittest.skipIf(sys.platform == 'win32', 'CI bash scripts')
    def test_actual_apt_scripts_retry_and_propagate_failure(self):
        for script in (self.install['run'], self.release['run']):
            for failures, install_status, expected, update_calls in (
                    (0, 0, 0, 1), (1, 0, 0, 2), (2, 0, 0, 3),
                    (3, 0, 1, 3), (0, 100, 100, 1), (1, 124, 124, 2)):
                with self.subTest(release=script == self.release['run'], failures=failures,
                                  install_status=install_status), tempfile.TemporaryDirectory(dir=ROOT) as scratch:
                    directory = Path(scratch)
                    def stub(name, body):
                        path = directory / name
                        path.write_text(body, encoding="utf-8")
                        path.chmod(0o755)
                    stub('dpkg', '#!/bin/bash\nexit 1\n')
                    stub('sleep', '#!/bin/bash\nexit 0\n')
                    stub('sudo', f'#!{sys.executable}\n' + '''import json, os, pathlib, sys
root = pathlib.Path(os.environ['APT_TEST_ROOT'])
args = sys.argv[1:]
with (root / 'calls').open('a', encoding='utf-8') as log:
    log.write(json.dumps(args) + '\\n')
if args[0] == 'tee':
    (root / 'config').write_text(sys.stdin.read(), encoding='utf-8')
if args[0] == 'timeout':
    assert args[1] == '--kill-after=5s'
    if args[-1] == 'update':
        assert args[2] == '60s'
        assert 'APT::Update::Error-Mode=any' in args
        count_file = root / 'count'
        count = int(count_file.read_text(encoding="utf-8")) if count_file.exists() else 0
        count_file.write_text(str(count + 1), encoding='utf-8')
        sys.exit(124 if count < int(os.environ['FAILURES']) else 0)
    assert args[2] == '300s'
    sys.exit(int(os.environ['INSTALL_STATUS']))
''')
                    env = dict(os.environ, PATH=f'{directory}:{os.environ["PATH"]}',
                               HOME=scratch, APT_TEST_ROOT=scratch, FAILURES=str(failures),
                               INSTALL_STATUS=str(install_status), APT_PACKAGES='one\ntwo',
                               EXTRA_APT_PACKAGES='extra-one\nextra-two')
                    result = subprocess.run(['bash', '-eo', 'pipefail', '-c', script],
                                            cwd=ROOT, env=env, capture_output=True, text=True, encoding="utf-8", errors="replace")
                    self.assertEqual(result.returncode, expected, result.stderr)
                    calls = [json.loads(line) for line in (directory / 'calls').read_text(encoding="utf-8").splitlines()]
                    self.assertEqual(int((directory / 'count').read_text(encoding="utf-8")), update_calls)
                    config = (directory / 'config').read_text(encoding="utf-8")
                    for option in ('Retries "3"', 'http::Timeout "30"',
                                   'https::Timeout "30"', 'ftp::Timeout "30"'):
                        self.assertIn('Acquire::' + option + ';', config)
                    installs = [c for c in calls if 'install' in c]
                    self.assertEqual(len(installs), int(failures < 3))
                    if installs:
                        self.assertIn(f'Dir::Cache::Archives={scratch}/.apt-archives', installs[0])
                        if script == self.release['run']:
                            self.assertEqual(installs[0][installs[0].index('install') + 2:], [
                                'build-essential', 'cmake', 'libgl1-mesa-dev', 'libx11-dev',
                                'libxrandr-dev', 'libxinerama-dev', 'libxcursor-dev', 'libxi-dev',
                                'libgtk-3-dev', 'libavcodec-dev', 'libavformat-dev', 'libswscale-dev',
                                'libavutil-dev', 'libgstreamer1.0-dev',
                                'libgstreamer-plugins-base1.0-dev', 'extra-one', 'extra-two'])
                        else:
                            self.assertEqual(installs[0][-2:], ['one', 'two'])
                    self.assertEqual(calls[-2][0:2], ['rm', '-f'])
                    self.assertEqual(calls[-1][0:2], ['chown', '-R'])

    @unittest.skipIf(sys.platform == 'win32', 'CI bash scripts')
    def test_web_build_retries_once_and_preserves_final_failure(self):
        for filename, job_name, step_names in (
                ('build.yml', 'build-web', ['Build AllFeaturesExample (Web)']),
                ('daily.yml', 'sweep-web', ['Build every example (Web)',
                                         'Build and run core tests (Web, under node)'])):
            steps = self.workflows[filename]['jobs'][job_name]['steps']
            for name in step_names:
                step = next(s for s in steps if s.get('name') == name)
                self.assertNotIn('continue-on-error', step)
                inner = step['run'].split("<<'TC_BUILD'\n")[1].split('\nTC_BUILD')[0]
                for failures in (0, 1, 2):
                    with self.subTest(step=name, failures=failures), tempfile.TemporaryDirectory(dir=ROOT) as scratch:
                        directory = Path(scratch)
                        (directory / 'examples').mkdir()
                        build = directory / 'examples/build_all.py'
                        build.write_text(f'#!{sys.executable}\n' + '''import os, pathlib, sys
count_file = pathlib.Path('count')
count = int(count_file.read_text(encoding="utf-8")) if count_file.exists() else 0
count_file.write_text(str(count + 1), encoding='utf-8')
sys.exit(1 if count < int(os.environ['FAILURES']) else 0)
''', encoding='utf-8')
                        build.chmod(0o755)
                        result = subprocess.run(['bash', '-eo', 'pipefail', '-c', inner],
                                                cwd=directory, env=dict(os.environ, FAILURES=str(failures)),
                                                capture_output=True, text=True, encoding="utf-8", errors="replace")
                        self.assertEqual(result.returncode, int(failures == 2), result.stderr)
                        self.assertEqual(int((directory / 'count').read_text(encoding="utf-8")), 1 if failures == 0 else 2)


if __name__ == '__main__':
    unittest.main()
