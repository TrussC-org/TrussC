"""Offline installer checks: fake package queries and sudo, never invoke apt."""
import json
import os
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
            ['bash', '-c', '. /etc/os-release; echo "$ID $ID_LIKE"'], text=True)
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
                  'with open(os.environ["APT_TEST_LOG"], "a") as log:\n'
                  '    log.write(json.dumps(sys.argv[1:]) + "\\n")\n')

    def stub(self, name, contents):
        path = self.directory / name
        path.write_text(contents)
        path.chmod(0o755)

    def run_installer(self):
        subprocess.run([str(ROOT / 'tools/install_dependencies_linux.sh'), '-y'],
                       env=self.env, check=True, capture_output=True, text=True)
        return [json.loads(line) for line in self.log.read_text().splitlines()]

    def test_cache_option_is_optional_and_preserves_arguments(self):
        baseline = self.run_installer()
        options = ['apt-get', '-o', 'Acquire::http::Timeout=30',
                   '-o', 'Acquire::Retries=3']
        self.assertEqual(baseline[0], options + ['update'])
        self.assertEqual(baseline[1][:7], options + ['install', '-y'])
        self.assertGreater(len(baseline[1]), 7)
        for archive in ('', str(self.directory / 'archives'),
                        str(self.directory / 'archives with spaces')):
            with self.subTest(archive=archive):
                self.log.unlink()
                self.env['TRUSSC_APT_ARCHIVES'] = archive
                cache = ['-o', f'Dir::Cache::Archives={archive}'] if archive else []
                expected = [options + cache + call[len(options):] for call in baseline]
                self.assertEqual(self.run_installer(), expected)

    def test_installed_packages_do_not_invoke_sudo(self):
        self.stub('dpkg', '#!/bin/bash\nexit 0\n')
        self.env['TRUSSC_APT_ARCHIVES'] = str(self.directory / 'archives')
        result = subprocess.run(
            [str(ROOT / 'tools/install_dependencies_linux.sh'), '-y'],
            env=self.env, check=True, capture_output=True, text=True)
        self.assertIn('All required packages are already installed.', result.stdout)
        self.assertFalse(self.log.exists())


if __name__ == '__main__':
    unittest.main()
