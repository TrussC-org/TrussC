"""HEAD-only binary-input drift, using offline fixture repositories.

Set TRUSSCLI to a CLI configured at this checkout's HEAD. Git writes occur
only in the temporary fixture. Run through tcbuild.sh (one unit probe compiles).
"""
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
CLI = Path(os.environ.get('TRUSSCLI', ROOT / 'tools/bin/trusscli')).resolve()
INPUTS = ('tools/src', 'tools/CMakeLists.txt', 'tools/addons.make', 'tools/icon')
NOTE = 'TrussC has changed since trusscli was built'


@unittest.skipUnless(os.environ.get('TRUSSCLI') and shutil.which('git'),
                     'set TRUSSCLI to a CLI configured at HEAD; needs git')
class DriftTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(prefix='trusscli-drift-')
        self.addCleanup(self.scratch.cleanup)
        self.root = Path(self.scratch.name)
        archive = subprocess.check_output(['git', '-C', str(ROOT), 'archive', 'HEAD', *INPUTS])
        with tarfile.open(fileobj=io.BytesIO(archive)) as source:
            source.extractall(self.root, filter='data')
        (self.root / 'core/cmake').mkdir(parents=True)
        (self.root / 'core/cmake/trussc_app.cmake').write_text('# fixture\n', encoding="utf-8")
        self.git('init', '-q')
        self.commit()
        self.env = dict(os.environ, TRUSSC_DIR=str(self.root))

    def git(self, *args):
        return subprocess.check_output(['git', '-C', str(self.root), *args], stderr=subprocess.STDOUT)

    def commit(self):
        self.git('add', '.')
        self.git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                 'commit', '-qm', 'fixture')

    def cli(self, *args, env=None):
        return subprocess.run([str(CLI), *args], cwd=self.root, env=env or self.env,
                              text=True, capture_output=True, timeout=60, encoding="utf-8")

    def sync_status(self):
        result = self.cli('doctor', '--json')
        checks = json.loads(result.stdout)
        return next(c for c in checks if c['name'] == 'Version sync')['status']

    def test_unchanged_and_unrelated_commits_and_dirty_edits(self):
        self.assertNotIn(NOTE, self.cli('version').stdout)
        self.assertEqual(self.sync_status(), 'ok')
        for name in ('core/change.txt', 'tools/tests/change.txt'):
            path = self.root / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('unrelated\n', encoding="utf-8")
            self.commit()
            self.assertNotIn(NOTE, self.cli('build', '--help').stderr)
            self.assertEqual(self.sync_status(), 'ok')
        for name in ('tools/src/main.cpp', 'core/change.txt'):
            with (self.root / name).open('a', encoding="utf-8") as file:
                file.write('// dirty\n')
            self.assertNotIn(NOTE, self.cli('version').stdout)
            self.assertNotIn(NOTE, self.cli('build', '--help').stderr)
            self.assertEqual(self.sync_status(), 'ok')

    def test_each_input_commit_warns_repeatedly(self):
        for name in INPUTS:
            with self.subTest(path=name):
                # Return only the fixture's tracked files to the original HEAD.
                self.git('reset', '--hard', '-q', 'HEAD')
                path = self.root / name
                path = path / 'drift.txt' if path.is_dir() else path
                with path.open('a', encoding="utf-8") as file:
                    file.write('# changed\n')
                self.commit()
                for command in (('build', '--help'), ('run', '--help')):
                    result = self.cli(*command)
                    self.assertIn(NOTE, result.stderr)
                    self.assertIn('trusscli upgrade', result.stderr)
                self.assertIn(NOTE, self.cli('version').stdout)
                self.assertEqual(self.sync_status(), 'warning')
                self.git('reset', '--hard', '-q', 'HEAD~1')

    def test_configure_metadata_matches_runtime_and_fails_closed(self):
        cmake = shutil.which('cmake')
        self.assertIsNotNone(cmake)
        text = (ROOT / 'tools/CMakeLists.txt').read_text(encoding="utf-8")
        start = text.index('execute_process(')
        end = text.index('string(TIMESTAMP', start)
        script = self.root / 'metadata.cmake'
        output = self.root / 'source-id'
        script.write_text('set(CMAKE_CURRENT_SOURCE_DIR "' + (self.root / 'tools').as_posix()
                          + '")\n' + text[start:end]
                          + '\nfile(WRITE "' + output.as_posix() + '" "${TRUSSCLI_SOURCE_ID_STR}")\n', encoding="utf-8")
        def configure(env=None):
            subprocess.run([cmake, '-P', str(script)], check=True, cwd=self.root, env=env or self.env)
            return output.read_text(encoding="utf-8")
        expected = self.git('rev-parse', *('HEAD:' + name for name in INPUTS)).decode().strip().replace('\n', '|')
        self.assertEqual(configure(), expected)
        empty = self.root / 'no-programs'
        empty.mkdir()
        self.assertEqual(configure(dict(self.env, PATH=str(empty))), '')
        (self.root / 'tools/addons.make').unlink()
        self.commit()
        self.assertEqual(configure(), '')
        (self.root / '.git').rename(self.root / 'fixture-git')
        self.assertEqual(configure(), '')
        self.assertNotIn(NOTE, self.cli('build', '--help').stderr)

    def test_major_version_error_requires_source_drift(self):
        self.git('tag', 'v999.0.0')
        self.assertEqual(self.sync_status(), 'ok')
        with (self.root / 'tools/src/main.cpp').open('a', encoding="utf-8") as file:
            file.write('// changed\n')
        self.commit()
        self.assertEqual(self.sync_status(), 'error')

    def test_missing_path_and_no_git_stay_silent(self):
        (self.root / 'tools/addons.make').unlink()
        self.commit()
        self.assertNotIn(NOTE, self.cli('version').stdout)
        self.assertNotIn(NOTE, self.cli('build', '--help').stderr)
        empty = self.root / 'no-programs'
        empty.mkdir()
        env = dict(self.env, PATH=str(empty))
        result = self.cli('build', '--help', env=env)
        self.assertEqual(result.stderr, '')

    def test_installed_root_not_project_pin(self):
        with tempfile.TemporaryDirectory(prefix='trusscli-pinned-') as scratch:
            selected = Path(scratch)
            (selected / 'core/cmake').mkdir(parents=True)
            (selected / 'core/cmake/trussc_app.cmake').write_text('# fixture\n', encoding="utf-8")
            project = self.root / 'project'
            (project / 'src').mkdir(parents=True)
            (project / 'CMakePresets.json').write_text(json.dumps({
                'vendor': {'trussc': {'localPath': str(selected)}}}), encoding="utf-8")
            def command(*args):
                return subprocess.run([str(CLI), *args], cwd=project, env=self.env,
                                      text=True, capture_output=True, timeout=60, encoding="utf-8")
            result = command('version')
            self.assertIn('version unknown', result.stdout)
            self.assertNotIn(NOTE, result.stdout)
            checks = json.loads(command('doctor', '--json').stdout)
            self.assertEqual(next(c for c in checks if c['name'] == 'Version sync')['status'], 'ok')
            with (self.root / 'tools/src/main.cpp').open('a', encoding="utf-8") as file:
                file.write('// changed\n')
            self.commit()
            self.assertIn(NOTE, command('version').stdout)


class UnknownBuildTests(unittest.TestCase):
    @unittest.skipUnless(shutil.which("c++"), "needs a host C++ compiler")
    def test_unknown_metadata_never_queries_git(self):
        source = (ROOT / 'tools/src/main.cpp').read_text(encoding="utf-8")
        start = source.index('static bool hasTrusscliSourceDrift(')
        end = source.index('\nstatic CheckResult', start)
        helper = source[start:end]
        with tempfile.TemporaryDirectory(prefix='trusscli-unknown-') as scratch:
            path = Path(scratch)
            probe = path / 'probe.cpp'
            probe.write_text('#include <string>\n#include <cstdlib>\nusing namespace std;\n'
                             'static string queryTrusscliSourceId(const string&) { abort(); }\n'
                             + helper + '\nint main() { return hasTrusscliSourceDrift("fixture"); }\n', encoding="utf-8")
            for version, source_id in (('unknown', 'known'), ('v1.0', '')):
                subprocess.run(['c++', '-std=c++17', f'-DTRUSSCLI_VERSION="{version}"',
                                f'-DTRUSSCLI_SOURCE_ID="{source_id}"', str(probe),
                                '-o', str(path / 'probe')], check=True)
                subprocess.run([str(path / 'probe')], check=True)


if __name__ == '__main__':
    unittest.main()
