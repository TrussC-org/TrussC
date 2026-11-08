#!/usr/bin/env python3
"""Offline tests for #821 orchestration and the required CI gate."""
import importlib.util
import json
import os
import shutil
import sys
from pathlib import Path
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import yaml
from test_web_window_controls import body

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
        # The recorded path uses the platform's separators.
        modes = [c for c in calls if Path(c[0]).name == 'binary']
        self.assertEqual([c[1] for c in modes], ['--self-check','--record','--compare'])
        self.assertIn('/base/binary',Path(modes[1][0]).as_posix())
        self.assertIn('/head/binary',Path(modes[2][0]).as_posix())
        self.assertEqual(modes[1][2], modes[2][2])
        removes = [c for c in calls if c[:3] == ['git','worktree','remove']]
        self.assertEqual([c[-1] for c in removes], [c[-2] for c in reversed(adds)])

    def test_first_batch_only_checks_head(self):
        calls = self.exercise(has_base=False)
        self.assertEqual([c[1] for c in calls if Path(c[0]).name == 'binary'], ['--self-check'])
        self.assertEqual(sum(c[:3] == ['git','worktree','add'] for c in calls),1)
        self.assertEqual(sum(c[:3] == ['git','worktree','remove'] for c in calls),1)

    def test_comparison_failure_cleans_both_worktrees(self):
        calls = self.exercise(fail_compare=True)
        self.assertEqual(sum(c[:3] == ['git','worktree','remove'] for c in calls),2)

    def test_trusscli_platform_paths(self):
        for platform, paths, expected in (
                ('darwin', ['trusscli.app/Contents/MacOS/trusscli'], 'trusscli.app/Contents/MacOS/trusscli'),
                ('darwin', ['trusscli', 'trusscli.app/Contents/MacOS/trusscli'], 'trusscli'),
                ('linux', ['trusscli'], 'trusscli'),
                ('win32', ['trusscli.exe'], 'trusscli.exe')):
            with self.subTest(platform=platform, paths=paths), tempfile.TemporaryDirectory(dir=ROOT) as temp:
                checkout = Path(temp)/'checkout'
                for path in paths:
                    binary = checkout/'tools/bin'/path
                    binary.parent.mkdir(parents=True, exist_ok=True); binary.touch()
                with patch.object(driver.sys, 'platform', platform), patch.object(driver, 'ROOT', checkout), \
                     patch.object(driver, 'commit', return_value='sha'), patch.object(driver, 'run', return_value='sha'), \
                     patch.object(driver, 'has_harness', return_value=False), \
                     patch.object(driver, 'build', return_value=checkout/'binary') as build:
                    driver.compare_refs('base', 'head', Path(temp)/'output')
                    build.assert_called_once()
                    self.assertEqual(build.call_args.args[1], checkout/'tools/bin'/expected)

    def test_missing_trusscli_names_searched_paths(self):
        for platform, paths in (
                ('darwin', ['trusscli', 'trusscli.app/Contents/MacOS/trusscli']),
                ('linux', ['trusscli']), ('win32', ['trusscli.exe'])):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory(dir=ROOT) as temp:
                checkout = Path(temp)/'checkout'; checkout.mkdir()
                with patch.object(driver.sys, 'platform', platform), patch.object(driver, 'ROOT', checkout), \
                     patch.object(driver, 'commit', return_value='sha'), patch.object(driver, 'run', return_value='sha'), \
                     patch.object(driver, 'build') as build:
                    with self.assertRaises(RuntimeError) as error:
                        driver.compare_refs('base', 'head', Path(temp)/'output')
                    for path in paths:
                        self.assertIn(str(checkout/'tools/bin'/path), str(error.exception))
                    build.assert_not_called()

    def test_builds_share_cache_with_checkout_relative_paths(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            # Same per-platform binary path as render_regression.build().
            tree = Path(temp); binary = tree/driver.HARNESS/'bin'/('renderRegression.exe' if driver.sys.platform == 'win32' else 'renderRegression')
            if driver.sys.platform == 'darwin':
                binary = tree/driver.HARNESS/'bin/renderRegression.app/Contents/MacOS/renderRegression'
            binary.parent.mkdir(parents=True); binary.touch()
            with patch.dict(driver.os.environ, CMAKE_C_COMPILER_LAUNCHER='ccache',
                            CMAKE_CXX_COMPILER_LAUNCHER='ccache'), patch.object(driver, 'run') as run:
                driver.build(tree, Path('/trusscli'))
            self.assertEqual(run.call_count, 3)
            for call in run.call_args_list:
                env = call.kwargs['env']
                self.assertEqual(env['CCACHE_BASEDIR'],str(tree))
                self.assertEqual(env['CMAKE_C_COMPILER_LAUNCHER'],'ccache')
                self.assertEqual(env['CMAKE_CXX_COMPILER_LAUNCHER'],'ccache')

    def test_nonempty_output_is_preserved(self):
        with tempfile.TemporaryDirectory(dir=ROOT) as temp:
            keep = Path(temp)/'keep'; keep.write_text('existing', encoding="utf-8")
            checkout = Path(temp)/'checkout'; (checkout/'tools/bin').mkdir(parents=True)
            (checkout/'tools/bin'/('trusscli.exe' if driver.sys.platform == 'win32' else 'trusscli')).touch()
            with patch.object(driver,'ROOT',checkout), patch.object(driver,'commit',return_value='sha'), patch.object(driver,'run',return_value='sha'):
                with self.assertRaisesRegex(RuntimeError,'Output must be empty'):
                    driver.compare_refs('base','head',Path(temp))
            self.assertEqual(keep.read_text(encoding="utf-8"),'existing')


class CaptureTests(unittest.TestCase):
    """Run the harness's loader and capture body with in-memory pixel I/O, no GPU."""
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('CXX') or shutil.which('c++')
        if not compiler:
            raise unittest.SkipTest('requires a C++ compiler (c++ on PATH, or set CXX)')
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        cls.folder = Path(cls.temp.name)
        source = (ROOT/'core/tests/renderRegression/src/main.cpp').read_text(encoding='utf-8')
        code = r'''#include <nlohmann/json.hpp>
#include <array>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <vector>
namespace fs = std::filesystem;
using Json = nlohmann::json;
using Rgba = std::array<int,4>;
fs::path baseline, output;
std::string mode = "--compare";
std::set<std::string> baselineScenes;
int failures = 0;
''' + body(source, 'void check(') + body(source, 'std::set<std::string> loadBaselineScenes(') + r'''
// Only the image I/O and app/GPU metadata are stand-ins. Comparison and JSON
// loading below are the actual harness code, including assertions/save order.
struct Status { bool value; bool ok() const { return value; } };
struct Pixels {
    int w = 1, h = 1;
    std::vector<unsigned char> data = {0,0,0,255};
    int getWidth() const { return w; }
    int getHeight() const { return h; }
    unsigned char* getData() { return data.data(); }
    void allocate(int width, int height, int) { w=width; h=height; data.resize(w*h*4); }
    Status save(const fs::path& path) { std::ofstream f(path); f << w << ' ' << h; return {f.good()}; }
    Status load(const fs::path& path) { std::ifstream f(path); return {bool(f >> w >> h)}; }
};
Rgba pixel(const Pixels&, int, int) { return {0,0,0,255}; }
struct GraphicsBackend { static std::string name() { return "test"; } };
struct Target { int sampleCount = 1; int getSampleCount() const { return sampleCount; } };
namespace internal {
    bool pixelPerfectMode() { return false; }
    struct Context { Target swapchainTarget; };
    Context& currentWindowContext() { static Context c; return c; }
}
struct Scene { int tolerance = 0; } scenes[1];
struct App {
    std::string sceneName;
    bool selfCheck = true;
    Json samples_ = Json::object(), scissorEvidence_ = Json::object();
    int logicalW_ = 1, logicalH_ = 1, scene_ = 0;
    std::set<std::string> captured_;
    std::string gpu_ = "test";
    Target target_;
    std::string name() const { return sceneName; }
    bool is(const char* text) const { return name() == text; }
    float probeScale(bool) const { return 1; }
    Rgba at(const Pixels& p, int x, int y, float) { return pixel(p,x,y); }
    void assertions(const Pixels&, bool) { check(name()+" self-check", selfCheck); }
''' + body(source, 'void capture(') + r'''
};
int main(int argc, char** argv) {
    baseline = argv[1]; output = argv[2];
    try {
        baselineScenes = loadBaselineScenes(baseline);
        fs::create_directories(output);
        App app; app.sceneName = argv[3]; app.selfCheck = argc == 4;
        Pixels px; app.capture(px, "screen", false);
    } catch (const std::exception& e) { check(e.what(), false); }
    return failures ? 1 : 0;
}
'''
        cpp = cls.folder/'capture.cpp'; cls.binary = cls.folder/'capture'
        cpp.write_text(code, encoding='utf-8')
        subprocess.run([compiler, '-std=c++20', '-Wall', '-Wextra',
                        '-I'+str(ROOT/'core/include'), str(cpp), '-o', str(cls.binary)], check=True)

    def exercise(self, manifest, scene='newScene', images=None, self_check=True):
        with tempfile.TemporaryDirectory(dir=self.folder) as temp:
            baseline = Path(temp)/'base'; baseline.mkdir()
            if manifest is not None:
                (baseline/'samples.json').write_text(manifest, encoding='utf-8')
            for key, contents in (images or {}).items():
                (baseline/(key+'.png')).write_text(contents, encoding='utf-8')
            output = Path(temp)/'head'
            args = [str(self.binary), str(baseline), str(output), scene]
            if not self_check:
                args.append('fail-self-check')
            proc = subprocess.run(args, capture_output=True, text=True, encoding='utf-8')
            files = {p.name for p in output.iterdir()} if output.exists() else set()
            return proc, files

    def test_new_scene_self_checks_saves_and_passes(self):
        # Difference overlays are not baseline captures; a shared prefix is
        # also a different scene.
        proc, files = self.exercise(json.dumps({'newSceneOther.screen': {}}), images={
            'newSceneOther.screen': '1 1', 'newScene.screen.diff': 'overlay'})
        self.assertEqual(proc.returncode, 0, proc.stdout)
        self.assertIn('PASS newScene self-check', proc.stdout)
        self.assertIn('NEW newScene.screen: no baseline in base recording (self-check only)', proc.stdout)
        self.assertNotIn(' baseline\n', proc.stdout)
        self.assertEqual(files, {'newScene.screen.png'})

    def test_new_scene_still_fails_its_self_check(self):
        proc, files = self.exercise(json.dumps({'oldScene.screen': {}}), self_check=False)
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn('FAIL newScene self-check', proc.stdout)
        self.assertIn('NEW newScene.screen:', proc.stdout)
        self.assertIn('newScene.screen.png', files)

    def test_empty_manifest_fails_before_capture(self):
        proc, files = self.exercise('{}')
        self.assertEqual(proc.returncode, 1, proc.stdout)
        self.assertIn('Invalid baseline samples.json', proc.stdout)
        self.assertNotIn('NEW ', proc.stdout)
        self.assertEqual(files, set())

    def test_manifest_omitting_scene_with_image_fails_before_capture(self):
        manifest = json.dumps({'text.screen': {}})
        for key in ('blendModes.screen', 'blendModes.fbo',
                    'blendModes.256x192.logical.fbo', 'blendModes'):
            for contents in ('corrupt', '1 1', '2 1'):
                with self.subTest(key=key, contents=contents):
                    proc, files = self.exercise(manifest, 'blendModes', {key: contents})
                    self.assertEqual(proc.returncode, 1, proc.stdout)
                    self.assertIn('Baseline manifest and images disagree', proc.stdout)
                    self.assertIn(key+'.png', proc.stdout)
                    self.assertIn('baseline samples.json', proc.stdout)
                    self.assertNotIn('NEW ', proc.stdout)
                    self.assertEqual(files, set())

    def test_existing_scene_missing_unreadable_or_wrong_size_image_fails(self):
        # A different target/variant in the manifest establishes scene presence.
        manifest = json.dumps({'oldScene.256x192.logical.fbo': {}})
        for contents in (None, 'corrupt', '2 1'):
            with self.subTest(contents=contents):
                images = {} if contents is None else {'oldScene.screen': contents}
                proc, files = self.exercise(manifest, 'oldScene', images)
                self.assertEqual(proc.returncode, 1, proc.stdout)
                self.assertIn('FAIL oldScene.screen baseline', proc.stdout)
                self.assertNotIn('NEW ', proc.stdout)
                self.assertEqual(files, {'oldScene.screen.png', 'oldScene.screen.diff.png'})

    def test_existing_scene_matching_image_passes(self):
        proc, _ = self.exercise(json.dumps({'oldScene.fbo': {}}), 'oldScene', {'oldScene.screen': '1 1'})
        self.assertEqual(proc.returncode, 0, proc.stdout)
        self.assertIn('PASS oldScene.screen baseline', proc.stdout)
        self.assertNotIn('NEW ', proc.stdout)

    def test_missing_or_corrupt_manifest_fails_before_capture(self):
        for manifest in (None, '{broken', '[]', 'null', '{"badKey": {}}', '{"oldScene.screen": null}'):
            with self.subTest(manifest=manifest):
                proc, files = self.exercise(manifest)
                self.assertEqual(proc.returncode, 1, proc.stdout)
                self.assertIn('baseline samples.json', proc.stdout)
                self.assertNotIn('NEW ', proc.stdout)
                self.assertEqual(files, set())


class WorkflowTests(unittest.TestCase):
    def test_main_only_gate_includes_pr_and_merge_queue(self):
        workflow = yaml.load((ROOT/'.github/workflows/build.yml').read_text(encoding="utf-8"),Loader=yaml.BaseLoader)
        self.assertEqual(workflow['on']['pull_request']['branches'],['main'])
        self.assertIn('merge_group',workflow['on'])
        jobs = workflow['jobs']; job = jobs['render-regression']
        self.assertEqual(job['needs'],'changes')
        self.assertEqual(job['if'],"needs.changes.outputs.docs_only != 'true' && (github.event_name == 'pull_request' || github.event_name == 'merge_group')")
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

    @unittest.skipIf(sys.platform == 'win32', 'runs Linux workflow steps through bash')
    def test_aggregate_gate_for_each_event_and_result(self):
        workflow = yaml.load((ROOT/'.github/workflows/build.yml').read_text(encoding="utf-8"),Loader=yaml.BaseLoader)
        script = workflow['jobs']['ci-ok']['steps'][0]['run']
        for event in ('push','workflow_dispatch','pull_request','merge_group'):
            for result in ('success','skipped','failure','cancelled'):
                for docs in ('true','false'):
                    env = dict(driver.os.environ, EVENT_NAME=event, DOCS_ONLY=docs,
                               **{key:'success' for key in ('R_CHANGES','R_BUILD','R_ADDONS','R_ANDROID','R_WEB','R_IOS','R_REF','R_HDR')},
                               R_RENDER=result)
                    proc = subprocess.run(['bash','-eu','-c',script],env=env,capture_output=True,text=True, encoding="utf-8")
                    ok = result == 'success' or (result == 'skipped' and
                         (docs == 'true' or event not in ('pull_request','merge_group')))
                    with self.subTest(event=event,result=result,docs=docs):
                        self.assertEqual(proc.returncode == 0,ok,proc.stdout)

    def test_display_marker_and_tolerance_inventory(self):
        self.assertEqual((ROOT/'core/tests/renderRegression/display-test').read_text(encoding="utf-8").strip(),'{test} --self-check')
        source = (ROOT/'core/tests/renderRegression/src/main.cpp').read_text(encoding="utf-8")
        for name in ('blendModes','text','premultImage','fboClearLoad','msaaResolve','transformPbr',
                     'easyCamFbo','scissorFbo','fullscreenShader','pbrShadowFbo','pointMeshTransform',
                     'resizePixelPerfect','userShaderOtherFormat','readbackRoundTrip','midFrameFlush','midPassClear',
                     'sglFboInterrupt','textureImmediate','msaaInterrupted','depthStencil','imguiWindowFbo'):
            self.assertRegex(source,r'\{"'+name+r'", [02]\}')


if __name__ == '__main__':
    unittest.main()
