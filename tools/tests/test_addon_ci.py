"""Offline affected-addon selection, runner filtering and required-gate checks."""
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

import yaml

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
import select_addon_tests as selector

spec = importlib.util.spec_from_file_location('addon_build_all', ROOT / 'examples/build_all.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class AddonSelectionTests(unittest.TestCase):
    def test_path_selection(self):
        for paths, expected in [
            (['core/include/tc/network/tcUdpSocket.h'], sorted(selector.NETWORK_ADDONS)),
            (['addons/tcxOsc/src/tcxOsc.h'], ['tcxOsc']),
            (['docs/README.md', 'core/include/tc/types/tcColor.h'], []),
            (['addons/tcxOsc/README.md', 'addons/tcxOsc/src/new.h'], ['tcxOsc']),
            (['addons/deletedAddon/tests/src/main.cpp'], []),
            ([], []),
        ]:
            with self.subTest(paths=paths):
                self.assertEqual(selector.select_addons(paths), expected)
        available = sorted(p.name for p in (ROOT / 'addons').iterdir() if p.is_dir())
        for path in selector.RUNNERS:
            self.assertEqual(selector.select_addons([path]), available)

    def test_network_mapping_covers_every_direct_user(self):
        users = set()
        for addon in (ROOT / 'addons').iterdir():
            for source in (addon / 'src').glob('*'):
                if source.suffix in ('.h', '.cpp') and '"tc/network/' in source.read_text(encoding='utf-8'):
                    users.add(addon.name)
        self.assertEqual(users, selector.NETWORK_ADDONS)

    def test_events_and_diff_arguments(self):
        event = {'merge_group': {'base_sha': 'base', 'head_sha': 'head'},
                 'pull_request': {'base': {'sha': 'base'}, 'head': {'sha': 'head'}},
                 'before': 'base', 'after': 'head'}
        for name, comparison in [('merge_group', '..'), ('pull_request', '...'), ('push', '..')]:
            with self.subTest(event=name), patch.object(selector.subprocess, 'run') as exists, \
                    patch.object(selector.subprocess, 'check_output', return_value=b'addons/tcxOsc/old.h\0addons/tcxTls/new.h\0') as diff:
                exists.return_value.returncode = 0
                paths = selector.event_paths(event, name)
                self.assertEqual(selector.select_addons(paths), ['tcxOsc', 'tcxTls'])
                self.assertEqual(diff.call_args.args[0],
                                 ['git', 'diff', '--no-renames', '--name-only', '-z', f'base{comparison}head'])
        self.assertEqual(selector.event_paths({}, 'workflow_dispatch'), selector.RUNNERS)
        with patch.object(selector.subprocess, 'run') as exists:
            exists.return_value.returncode = 1
            self.assertEqual(selector.event_paths(event, 'merge_group'), selector.RUNNERS)
        with patch.object(selector.subprocess, 'check_output', side_effect=subprocess.CalledProcessError(1, 'git')):
            with patch.object(selector.subprocess, 'run') as exists:
                exists.return_value.returncode = 0
                with self.assertRaises(subprocess.CalledProcessError):
                    selector.event_paths(event, 'merge_group')

    def test_runner_selection_includes_daily_and_named_harnesses(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            root = Path(temp)
            for name in ('tcxOne/tests', 'tcxOne/tests-extra', 'tcxTwo/tests'):
                (root / 'addons' / name / 'src').mkdir(parents=True)
            (root / 'addons/tcxOne/tests-extra/daily-only').touch()
            found = runner.find_addon_tests(str(root), include_daily=True, addons=['tcxOne'])
            self.assertEqual([Path(p).relative_to(root).as_posix() for p in found],
                             ['addons/tcxOne/tests', 'addons/tcxOne/tests-extra'])
            self.assertEqual(runner.find_addon_tests(str(root), addons=[]), [])
            self.assertEqual(len(runner.find_addon_tests(str(root), include_daily=True)), 3)


class AddonWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(encoding='utf-8'),
                                  Loader=yaml.BaseLoader)

    def test_always_triggered_and_required(self):
        for event in ('pull_request', 'merge_group'):
            trigger = self.workflow['on'][event] or {}
            self.assertNotIn('paths', trigger)
            self.assertNotIn('paths-ignore', trigger)
        job = self.workflow['jobs']['addon-tests']
        self.assertNotIn('if', job)
        self.assertEqual(job['strategy']['matrix']['os'], ['ubuntu-latest', 'macos-latest'])
        self.assertIn('needs.changes.outputs.addons', job['strategy']['matrix']['selected'][0])
        steps = job['steps']
        self.assertEqual(steps[0]['if'], "matrix.selected == '[]'")
        for step in steps[1:]:
            self.assertIn("matrix.selected != '[]'", step['if'])
        gate = self.workflow['jobs']['ci-ok']
        self.assertIn('addon-tests', gate['needs'])
        self.assertEqual(gate['steps'][0]['env']['R_ADDONS'], '${{ needs.addon-tests.result }}')
        self.assertIn('check "addon-tests"     "$R_ADDONS"', gate['steps'][0]['run'])

    def test_addon_results_cannot_be_skipped_or_failed(self):
        script = self.workflow['jobs']['ci-ok']['steps'][0]['run']
        for event in ('pull_request', 'merge_group'):
            for docs in ('true', 'false'):
                for result in ('success', 'skipped', 'failure', 'cancelled'):
                    env = dict(os.environ, EVENT_NAME=event, DOCS_ONLY=docs,
                               **{key: 'success' for key in ('R_CHANGES', 'R_BUILD', 'R_ANDROID',
                                  'R_WEB', 'R_IOS', 'R_REF', 'R_HDR', 'R_RENDER')}, R_ADDONS=result)
                    proc = subprocess.run(['bash', '-eu', '-c', script], env=env,
                                          capture_output=True, text=True, encoding='utf-8')
                    with self.subTest(event=event, docs=docs, result=result):
                        self.assertEqual(proc.returncode == 0, result == 'success', proc.stdout)

    def test_selector_and_existing_daily_entrypoint(self):
        changes = self.workflow['jobs']['changes']
        self.assertIn('addons', changes['outputs'])
        selection = next(s for s in changes['steps'] if s.get('id') == 'addons')
        self.assertIn('tools/select_addon_tests.py', selection['run'])
        self.assertIn('--event-file "$GITHUB_EVENT_PATH" --event-name "$EVENT_NAME"', selection['run'])
        self.assertEqual(changes['steps'][0]['with']['fetch-depth'], '0')
        checks = self.workflow['jobs']['header-state-check']['steps']
        self.assertTrue(any(s.get('run') == 'python3 tools/tests/test_addon_ci.py -v'
                            and s.get('if') == 'always()' for s in checks))
        job = self.workflow['jobs']['addon-tests']
        run = next(step['run'] for step in job['steps']
                   if step.get('name') == 'Build and run affected addon tests')
        for argument in ('examples/build_all.py', '--addon-tests-only', '--include-daily', '--addons'):
            self.assertIn(argument, run)
        self.assertIn("json.loads(os.environ['SELECTED_ADDONS'])", run)
        daily = (ROOT / '.github/workflows/daily.yml').read_text(encoding='utf-8')
        self.assertIn('./examples/build_all.py --addon-tests-only --include-daily --verbose', daily)
        self.assertNotIn('--addons', daily)


if __name__ == '__main__':
    unittest.main()
