#!/usr/bin/env python3
"""Offline tests for #821 orchestration and the required CI gate."""
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import yaml

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('render_regression', ROOT/'tools/render_regression.py')
driver = importlib.util.module_from_spec(spec)
spec.loader.exec_module(driver)


class DriverTests(unittest.TestCase):
    def exercise(self, has_base=True, fail_compare=False):
        calls = []

        def run(command, **kwargs):
            words = list(map(str, command))
            calls.append(words)
            if words[:2] == ['git', 'merge-base']:
                return 'merge-base-sha'
            if '--compare' in words and fail_compare:
                raise subprocess.CalledProcessError(1, words)

        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            checkout = Path(temp)/'checkout'; (checkout/'tools/bin').mkdir(parents=True)
            (checkout/'tools/bin'/('trusscli.exe' if driver.sys.platform == 'win32' else 'trusscli')).touch()
            with patch.object(driver, 'ROOT', checkout), patch.object(driver, 'commit', side_effect=['base-sha','head-sha']), \
                 patch.object(driver, 'run', side_effect=run), \
                 patch.object(driver, 'has_harness', return_value=has_base) as harness, \
                 patch.object(driver, 'build', side_effect=lambda tree, cli: tree/'binary'):
                if fail_compare:
                    with self.assertRaises(subprocess.CalledProcessError):
                        driver.compare_refs('base','head',Path(temp)/'output')
                else:
                    driver.compare_refs('base','head',Path(temp)/'output')
                harness.assert_called_once_with('merge-base-sha')
        return calls

    def test_merge_base_record_head_compare_and_self_check(self):
        calls = self.exercise()
        self.assertEqual(calls[0], ['git','merge-base','base-sha','head-sha'])
        adds = [c for c in calls if c[:3] == ['git','worktree','add']]
        self.assertEqual([c[-1] for c in adds], ['head-sha','merge-base-sha'])
        modes = [c for c in calls if c[0].endswith('/binary')]
        self.assertEqual([c[1] for c in modes], ['--self-check','--record','--compare'])
        self.assertIn('/base/binary',modes[1][0])
        self.assertIn('/head/binary',modes[2][0])
        self.assertEqual(modes[1][2], modes[2][2])
        removes = [c for c in calls if c[:3] == ['git','worktree','remove']]
        self.assertEqual([c[-1] for c in removes], [c[-2] for c in reversed(adds)])

    def test_first_batch_only_checks_head(self):
        calls = self.exercise(has_base=False)
        self.assertEqual([c[1] for c in calls if c[0].endswith('/binary')], ['--self-check'])
        self.assertEqual(sum(c[:3] == ['git','worktree','add'] for c in calls),1)
        self.assertEqual(sum(c[:3] == ['git','worktree','remove'] for c in calls),1)

    def test_comparison_failure_cleans_both_worktrees(self):
        calls = self.exercise(fail_compare=True)
        self.assertEqual(sum(c[:3] == ['git','worktree','remove'] for c in calls),2)

    def test_nonempty_output_is_preserved(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            keep = Path(temp)/'keep'; keep.write_text('existing')
            checkout = Path(temp)/'checkout'; (checkout/'tools/bin').mkdir(parents=True)
            (checkout/'tools/bin'/('trusscli.exe' if driver.sys.platform == 'win32' else 'trusscli')).touch()
            with patch.object(driver,'ROOT',checkout), patch.object(driver,'commit',return_value='sha'), patch.object(driver,'run',return_value='sha'):
                with self.assertRaisesRegex(RuntimeError,'Output must be empty'):
                    driver.compare_refs('base','head',Path(temp))
            self.assertEqual(keep.read_text(),'existing')


class WorkflowTests(unittest.TestCase):
    def test_main_only_gate_includes_pr_and_merge_queue(self):
        workflow = yaml.load((ROOT/'.github/workflows/build.yml').read_text(),Loader=yaml.BaseLoader)
        self.assertEqual(workflow['on']['pull_request']['branches'],['main'])
        self.assertIn('merge_group',workflow['on'])
        jobs = workflow['jobs']; job = jobs['render-regression']
        self.assertEqual(job['needs'],'changes')
        self.assertEqual(job['if'],"needs.changes.outputs.docs_only != 'true'")
        self.assertEqual(job['env']['LIBGL_ALWAYS_SOFTWARE'],'1')
        self.assertTrue(job['env']['__EGL_VENDOR_LIBRARY_FILENAMES'].endswith('50_mesa.json'))
        checkout = job['steps'][0]
        self.assertEqual(checkout['with']['fetch-depth'],'0')
        compare = next(s for s in job['steps'] if s.get('name','').startswith('Compare '))
        self.assertIn('xvfb-run',compare['run'])
        self.assertIn('tools/render_regression.py "$BASE_REF" "$HEAD_REF"',compare['run'])
        self.assertIn('pull_request.base.sha',compare['env']['BASE_REF'])
        self.assertIn('merge_group.base_sha',compare['env']['BASE_REF'])
        self.assertIn('pull_request.head.sha',compare['env']['HEAD_REF'])
        self.assertIn('merge_group.head_sha',compare['env']['HEAD_REF'])
        artifact = next(s for s in job['steps'] if s.get('uses','').startswith('actions/upload-artifact@'))
        self.assertEqual(artifact['if'],'always()')
        self.assertIn('**/*.png',artifact['with']['path'])
        self.assertIn('**/samples.json',artifact['with']['path'])
        self.assertIn('render-regression',jobs['ci-ok']['needs'])
        gate = jobs['ci-ok']['steps'][0]
        self.assertEqual(gate['env']['R_RENDER'],'${{ needs.render-regression.result }}')
        self.assertIn('check "render-regression" "$R_RENDER" skippable',gate['run'])
        self.assertIn({'name':'render regression workflow and driver checks','if':'always()',
                       'run':'python3 tools/tests/test_render_regression.py -v'},
                      jobs['header-state-check']['steps'])

    def test_display_marker_and_tolerance_inventory(self):
        self.assertEqual((ROOT/'core/tests/renderRegression/display-test').read_text().strip(),'{test} --self-check')
        source = (ROOT/'core/tests/renderRegression/src/main.cpp').read_text()
        for name in ('blendModes','text','premultImage','fboClearLoad','msaaResolve','transformPbr',
                     'easyCamFbo','scissorFbo','fullscreenShader','pbrShadowFbo','pointMeshTransform',
                     'resizePixelPerfect','userShaderOtherFormat','readbackRoundTrip','midFrameFlush'):
            self.assertRegex(source,r'\{"'+name+r'", [02]\}')


if __name__ == '__main__':
    unittest.main()
