"""Offline checks that Build caches never save on merge_group (#352).

Only boolean expressions and known context comparisons are supported; unfamiliar
conditions fail closed so they require review instead of silently passing.
"""
import ast
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]


CONTEXT_TERM = re.compile(
    r"(?:matrix\.\w+|steps\.[\w-]+\.outcome)\s*(?:==|!=)\s*(?:\d+|'[^']*')")


def on_merge_group(expression):
    """Whether the expression can be true on merge_group. Matrix comparisons
    and step outcomes can go either way, so every
    combination of their values is tried; any true one counts."""
    expression = expression.strip().removeprefix('${{').removesuffix('}}').strip()
    terms = sorted(set(CONTEXT_TERM.findall(expression)))
    for i, term in enumerate(terms):
        expression = expression.replace(term, f'_m{i}')
    expression = expression.replace('github.event_name', "'merge_group'")
    expression = re.sub(r'\btrue\b', 'True', expression)
    expression = re.sub(r'\bfalse\b', 'False', expression)
    expression = re.sub(r'!(?!=)', ' not ', expression).replace('&&', ' and ').replace('||', ' or ')
    tree = ast.parse(expression.strip(), mode='eval')
    allowed = (ast.Expression, ast.BoolOp, ast.And, ast.Or, ast.UnaryOp, ast.Not,
               ast.Compare, ast.Eq, ast.NotEq, ast.Constant, ast.Name, ast.Load)
    names = {f'_m{i}' for i in range(len(terms))}
    if any(not isinstance(node, allowed) or (isinstance(node, ast.Name) and node.id not in names)
           for node in ast.walk(tree)):
        raise ValueError('unsupported cache condition')
    code = compile(tree, '<cache condition>', 'eval')
    for bits in range(1 << len(terms)):
        values = {f'_m{i}': bool(bits >> i & 1) for i in range(len(terms))}
        result = eval(code, {'__builtins__': {}}, values)
        if not isinstance(result, bool):
            raise ValueError('cache condition must evaluate to a boolean')
        if result:
            return True
    return False


def can_save_on_merge_group(step):
    if not on_merge_group(step.get('if', 'true')):
        return False
    # Only ccache-action has a `save` input; actions/cache ignores one, so for
    # it (and cache/save) the step's `if:` alone decides.
    if step.get('uses', '').startswith('hendrikmuhs/ccache-action@'):
        return on_merge_group(step.get('with', {}).get('save', 'true'))
    return True


class BuildWorkflowCacheTests(unittest.TestCase):
    def setUp(self):
        self.workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(encoding="utf-8"),
                                  Loader=yaml.BaseLoader)

    def test_cache_steps_cannot_save_on_merge_group(self):
        for job_name, job in self.workflow['jobs'].items():
            for step in job.get('steps', []):
                if step.get('uses', '').startswith(('actions/cache@', 'actions/cache/save@',
                                                    'hendrikmuhs/ccache-action@')):
                    with self.subTest(job=job_name, step=step.get('name', step['uses'])):
                        self.assertFalse(can_save_on_merge_group(step))

    def test_windows_compiler_cache_is_optional_in_every_workflow(self):
        # Scan release/daily workflows too, so a new Windows cache cannot
        # silently reintroduce a fatal setup step.
        windows_jobs = []
        for path in sorted((ROOT / '.github/workflows').glob('*.yml')):
            workflow = yaml.load(path.read_text(encoding="utf-8"), Loader=yaml.BaseLoader)
            for job_name, job in workflow['jobs'].items():
                matrix = job.get('strategy', {}).get('matrix', {})
                runs_on = job.get('runs-on', '')
                has_windows = 'windows' in (str(matrix) + str(runs_on)).lower()
                for cache in job.get('steps', []):
                    if not cache.get('uses', '').startswith('hendrikmuhs/ccache-action@'):
                        continue
                    if not has_windows:
                        continue
                    with self.subTest(workflow=path.name, job=job_name):
                        windows_jobs.append((path.name, job_name))
                        self.assertEqual(cache['continue-on-error'], "${{ runner.os == 'Windows' }}")
                        self.assertEqual(cache['id'], 'compiler_cache')
                        steps = job['steps']
                        launcher = next(s for s in steps if s.get('name') == 'Enable compiler launcher')
                        self.assertEqual(launcher['if'], "steps.compiler_cache.outcome == 'success'")
                        self.assertLess(steps.index(cache), steps.index(launcher))
                        for step in steps:
                            name = step.get('name', '')
                            if name.startswith('Cache build directories') or name == 'Verify cached warning replay (MSVC)':
                                self.assertIn("steps.compiler_cache.outcome == 'success'", step['if'])
                            # Build/test correctness must still fail the job.
                            if name.startswith(('Build ', 'Build &', 'Run ')):
                                self.assertNotIn('continue-on-error', step)
        self.assertIn(('build.yml', 'build'), windows_jobs)
        self.assertIn(('daily.yml', 'sweep'), windows_jobs)

    @unittest.skipIf(sys.platform == 'win32', 'runs workflow launcher scripts through bash')
    def test_windows_cache_failure_leaves_no_launcher_or_restored_cmake_cache(self):
        from test_daily_workflow import should_run

        for filename, job_name in [('build.yml', 'build'), ('daily.yml', 'sweep')]:
            workflow = yaml.load((ROOT / '.github/workflows' / filename).read_text(encoding="utf-8"),
                                 Loader=yaml.BaseLoader)
            steps = workflow['jobs'][job_name]['steps']
            launcher = next(s for s in steps if s.get('name') == 'Enable compiler launcher')
            build = next(s for s in steps if s.get('name') == 'Build trusscli')
            self.assertTrue(should_run(build, {}))
            dependent = [s for s in steps if s.get('name', '').startswith('Cache build directories')
                         or s.get('name') == 'Verify cached warning replay (MSVC)']
            for outcome in ('success', 'failure'):
                for event in ('pull_request', 'merge_group'):
                    with self.subTest(workflow=filename, outcome=outcome, event=event), tempfile.TemporaryDirectory(dir=ROOT) as scratch:
                        env_file = Path(scratch) / 'github-env'
                        env_file.touch()
                        for step in [launcher] + dependent:
                            condition = step.get('if', 'true').replace('runner.os', "'Windows'")
                            condition = condition.replace('github.event_name', repr(event))
                            # continue-on-error makes the setup conclusion successful,
                            # while its raw outcome remains failure for the guards.
                            condition = condition.replace('steps.compiler_cache.outcome', repr(outcome))
                            enabled = should_run({'if': condition}, {})
                            if outcome == 'failure':
                                self.assertFalse(enabled, step['name'])
                            if step is launcher and enabled:
                                script = step['run'].replace('${{ matrix.launcher }}', 'sccache')
                                subprocess.run(['bash', '-eu', '-c', script], check=True,
                                               env=dict(os.environ, GITHUB_ENV=str(env_file)))
                        lines = env_file.read_text(encoding="utf-8").splitlines()
                        if outcome == 'failure':
                            self.assertEqual(lines, [])
                        else:
                            for lang in ('C', 'CXX', 'OBJC', 'OBJCXX'):
                                self.assertIn(f'CMAKE_{lang}_COMPILER_LAUNCHER=sccache', lines)

    def test_render_cache_matches_linux_setup_and_shares_refs(self):
        job = self.workflow['jobs']['render-regression']
        cache = next(s for s in job['steps'] if s.get('name') == 'Setup compiler cache')
        self.assertEqual(cache['uses'],'hendrikmuhs/ccache-action@v1.2')
        self.assertEqual(cache['with']['variant'],'ccache')
        self.assertEqual(cache['with']['save'],"${{ github.event_name != 'merge_group' }}")
        self.assertNotIn('sha',cache['with']['key'])
        setup = next(s for s in job['steps'] if s.get('name') == 'Enable compiler launcher')['run']
        self.assertIn('CMAKE_C_COMPILER_LAUNCHER=ccache',setup)
        self.assertIn('CMAKE_CXX_COMPILER_LAUNCHER=ccache',setup)
        self.assertIn('CCACHE_COMPILERCHECK=content',setup)

    def test_sokol_cache_twins(self):
        for job_name in ('build', 'build-android', 'build-web', 'build-ios'):
            with self.subTest(job=job_name):
                steps = self.workflow['jobs'][job_name]['steps']
                saves = [step for step in steps if step.get('name') == 'Cache sokol-shdc']
                restores = [step for step in steps if step.get('name') ==
                            'Cache sokol-shdc (restore only, merge queue)']
                self.assertEqual(len(saves), 1)
                self.assertEqual(len(restores), 1)
                save, restore = saves[0], restores[0]
                self.assertEqual(steps.index(restore), steps.index(save) + 1)
                self.assertEqual(save['uses'], 'actions/cache@v5')
                self.assertEqual(save['if'], "github.event_name != 'merge_group'")
                self.assertEqual(restore['uses'], 'actions/cache/restore@v5')
                self.assertEqual(restore['if'], "github.event_name == 'merge_group'")
                expected = {'path': 'core/tools/sokol-shdc',
                            'key': 'sokol-shdc-v1-${{ runner.os }}'}
                self.assertEqual(save['with'], expected)
                self.assertEqual(restore['with'], expected)

    def test_guard_evaluation(self):
        CCACHE = 'hendrikmuhs/ccache-action@v1.2'
        for step, expected in [
            ({}, True),
            ({'if': "github.event_name != 'merge_group'"}, False),
            ({'if': "${{ github.event_name != 'merge_group' }}"}, False),
            ({'uses': CCACHE, 'with': {'save': "${{ github.event_name != 'merge_group' }}"}}, False),
            ({'uses': CCACHE, 'with': {'save': 'false'}}, False),
            ({'if': "github.event_name == 'merge_group'"}, True),
            ({'if': "github.event_name != 'merge_group' || true"}, True),
            ({'uses': CCACHE, 'with': {'save': "${{ github.event_name == 'merge_group' }}"}}, True),
            # Matrix terms go either way: only the merge_group guard decides.
            ({'uses': CCACHE, 'with': {'save': "${{ github.event_name != 'merge_group' && matrix.shard == 1 }}"}}, False),
            ({'uses': CCACHE, 'with': {'save': "${{ matrix.shard == 1 }}"}}, True),
            ({'if': "github.event_name == 'merge_group' || matrix.shard != 1"}, True),
            ({'if': "github.event_name != 'merge_group' && matrix.preset == 'linux'"}, False),
            # actions/cache has no `save` input: it would still save.
            ({'uses': 'actions/cache@v5', 'with': {'save': 'false'}}, True),
            ({'uses': 'actions/cache/save@v5', 'with': {'save': 'false'}}, True),
        ]:
            with self.subTest(step=step):
                self.assertEqual(can_save_on_merge_group(step), expected)
        with self.assertRaises(ValueError):
            on_merge_group('unknown.context')
        with self.assertRaises(ValueError):
            on_merge_group('matrix.shard')   # only comparisons are understood

    def test_fetchcontent_cache_twins_and_environment(self):
        for filename, jobs in [('build.yml', ('build', 'build-android', 'build-web', 'build-ios')),
                               ('daily.yml', ('sweep', 'sweep-web'))]:
            workflow = yaml.load((ROOT / '.github/workflows' / filename).read_text(encoding="utf-8"),
                                 Loader=yaml.BaseLoader)
            for job_name in jobs:
                with self.subTest(workflow=filename, job=job_name):
                    steps = workflow['jobs'][job_name]['steps']
                    save = next(s for s in steps if s.get('id') == 'fetchcontent')
                    restore = next(s for s in steps if s.get('id') == 'fetchcontent-restore')
                    setup = next(s for s in steps if s.get('name') == 'Configure shared FetchContent')
                    self.assertEqual(save['uses'], 'actions/cache@v5')
                    self.assertEqual(save['if'], "github.event_name != 'merge_group'")
                    self.assertEqual(restore['uses'], 'actions/cache/restore@v5')
                    self.assertEqual(restore['if'], "github.event_name == 'merge_group'")
                    self.assertFalse(can_save_on_merge_group(save))
                    expected = {'path': '${{ github.workspace }}/.fetchcontent/*-src\n'
                                        '${{ github.workspace }}/.fetchcontent/*-subbuild\n',
                                'key': "fetchcontent-${{ runner.os }}-${{ github.job }}-${{ hashFiles('addons/*/CMakeLists.txt') }}"}
                    self.assertEqual(save['with'], expected)
                    self.assertEqual(restore['with'], expected)
                    self.assertEqual(steps.index(restore), steps.index(save) + 1)
                    self.assertEqual(steps.index(setup), steps.index(restore) + 1)
                    self.assertLess(steps.index(setup), next(i for i, s in enumerate(steps)
                                                          if s.get('name', '').startswith('Build ')))
                    self.assertEqual(setup['shell'], 'bash')
                    self.assertEqual(setup['env']['FETCHCONTENT_CACHE_HIT'],
                                     "${{ steps.fetchcontent.outputs.cache-hit == 'true' || "
                                     "steps.fetchcontent-restore.outputs.cache-hit == 'true' }}")

    @unittest.skipIf(sys.platform == 'win32', 'runs Linux workflow steps through bash')
    def test_fetchcontent_environment_shell(self):
        for filename, jobs in [('build.yml', ('build', 'build-android', 'build-web', 'build-ios')),
                               ('daily.yml', ('sweep', 'sweep-web'))]:
            workflow = yaml.load((ROOT / '.github/workflows' / filename).read_text(encoding="utf-8"),
                                 Loader=yaml.BaseLoader)
            for job_name in jobs:
                setup = next(s for s in workflow['jobs'][job_name]['steps']
                             if s.get('name') == 'Configure shared FetchContent')
                # Execute the actual shell on hit/miss, including a path
                # with spaces, without invoking a cache service.
                with tempfile.TemporaryDirectory(dir=ROOT) as scratch:
                    env_file = Path(scratch) / 'github-env'
                    workspace = str(Path(scratch) / 'workspace with spaces')
                    for hit in ('true', 'false', ''):
                        env_file.write_text('', encoding="utf-8")
                        subprocess.run(['bash', '-eu', '-c', setup['run']], check=True,
                                       env=dict(os.environ, GITHUB_ENV=str(env_file),
                                                GITHUB_WORKSPACE=workspace, FETCHCONTENT_CACHE_HIT=hit))
                        expected_lines = [f'TRUSSC_FETCHCONTENT_DIR={workspace}/.fetchcontent']
                        if hit == 'true':
                            expected_lines.append('TRUSSC_FETCHCONTENT_UPDATES_DISCONNECTED=ON')
                        self.assertEqual(env_file.read_text(encoding="utf-8").splitlines(), expected_lines)

    def test_ccache_prune_runs_only_after_main_builds(self):
        # #776: one compiler-cache save per platform. The prune job deletes
        # Actions caches, so it must run only on main pushes, after every job
        # that saves a ccache, and must not gate merges.
        jobs = self.workflow['jobs']
        prune = jobs['ccache-prune']
        self.assertEqual(prune['if'],
                         "always() && github.event_name == 'push' && github.ref == 'refs/heads/main'")
        self.assertEqual(prune['permissions'], {'actions': 'write'})
        savers = sorted(name for name, job in jobs.items()
                        if any(step.get('uses', '').startswith('hendrikmuhs/ccache-action@')
                               and step.get('with', {}).get('save', 'true') != 'false'
                               for step in job.get('steps', [])))
        self.assertEqual(sorted(prune['needs']), savers)
        self.assertNotIn('ccache-prune', jobs['ci-ok']['needs'])

    def test_regression_runs_in_pr_ci(self):
        matches = [step for step in self.workflow['jobs']['header-state-check']['steps']
                   if step.get('run') == 'python3 tools/tests/test_build_workflow_caches.py -v']
        self.assertEqual(len(matches), 1)
        self.assertEqual(matches[0]['if'], 'always()')


if __name__ == '__main__':
    unittest.main()
