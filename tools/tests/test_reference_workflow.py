"""Offline checks for API regeneration order and CI drift patches (#808)."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[2]
OUTPUTS = ['docs/FOR_AI_ASSISTANT.md', 'docs/TrussC_vs_openFrameworks.md',
           'examples/tests/AllFeaturesExample/src/coverage_generated.cpp',
           'addons/tcxLua/src/generated/*.cpp']


class ReferenceWorkflowTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name)
        self.bin = self.directory / 'bin'
        self.bin.mkdir()
        self.env = dict(os.environ, PATH=f'{self.bin}{os.pathsep}{os.environ["PATH"]}',
                        CALLS=str(self.directory / 'calls'))
        self.workflow = yaml.load((ROOT / '.github/workflows/build.yml').read_text(encoding="utf-8"),
                                  Loader=yaml.BaseLoader)
        self.job = self.workflow['jobs']['reference-check']

    def stub(self, name, text):
        script = self.bin / name
        script.write_text('#!/usr/bin/env python3\n' + text, encoding="utf-8")
        script.chmod(0o755)

    def test_ci_lanes_bootstrap_and_patch(self):
        self.assertEqual(self.workflow['on']['pull_request']['branches'], ['main'])
        self.assertIn('merge_group', self.workflow['on'])
        self.assertEqual(self.job['runs-on'], 'macos-latest')
        self.assertNotIn('if', self.job)
        steps = self.job['steps']
        runs = [step.get('run') for step in steps]
        bootstrap = runs.index('docs/reference/regenerate.sh --shaders-only')
        check = runs.index('node docs/reference/check.js --strict')
        regen = runs.index('docs/reference/regenerate.sh')
        freshness = next(s for s in steps if s.get('id') == 'freshness')
        self.assertLess(bootstrap, check)
        self.assertLess(check, regen)
        self.assertLess(regen, steps.index(freshness))
        upload = next(s for s in steps if s.get('uses', '').startswith('actions/upload-artifact@'))
        self.assertEqual(upload['if'], "failure() && steps.freshness.outcome == 'failure'")
        self.assertEqual(upload['with']['path'], 'regenerated.patch')
        self.assertEqual(upload['with']['if-no-files-found'], 'error')

        # Exercise the actual workflow shell with clean and stale diff responses.
        # No git repository is written and no Actions service is needed.
        self.stub('git', '''import json, os, sys
with open(os.environ['CALLS'], 'a', encoding='utf-8') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
if '--exit-code' in sys.argv:
    sys.exit(int(os.environ['DRIFT']))
if sys.argv[1] == 'status':
    print(os.environ['NEW'], end='')
    sys.exit(0)
if sys.argv[1] == 'add':
    sys.exit(0)
print('diff --git a/generated.cpp b/generated.cpp')
''')
        DIRS = ['addons/tcxLua/src/generated', 'examples/tests/AllFeaturesExample/src']
        status = ['status', '--porcelain', '--untracked-files=all', '--', *DIRS]
        add = ['add', '-N', '--', *DIRS]
        new_shard = '?? addons/tcxLua/src/generated/trussctype_generated_16.cpp\n'
        # (tracked drift, untracked output) -> expected calls after the first diff
        cases = [('0', '', [status]), ('1', '', [add, ['diff']]),
                 ('0', new_shard, [status, add, ['diff']])]
        for drift, new, after in cases:
            with self.subTest(drift=drift, new=bool(new)):
                (self.directory / 'calls').write_text('', encoding="utf-8")
                (self.directory / 'regenerated.patch').unlink(missing_ok=True)
                result = subprocess.run(['bash', '-euo', 'pipefail', '-c', freshness['run']],
                                        cwd=self.directory, env=dict(self.env, DRIFT=drift, NEW=new))
                failed = drift == '1' or bool(new)
                self.assertEqual(result.returncode, int(failed))
                calls = [json.loads(line) for line in (self.directory / 'calls').read_text(encoding="utf-8").splitlines()]
                self.assertEqual(calls, [['diff', '--exit-code', '--', *OUTPUTS], *after])
                if failed:
                    self.assertIn('diff --git', (self.directory / 'regenerated.patch').read_text(encoding="utf-8"))
                else:
                    self.assertFalse((self.directory / 'regenerated.patch').exists())
        tracked = subprocess.check_output(['git', 'ls-files', '--', *OUTPUTS], cwd=ROOT, text=True, encoding="utf-8")
        self.assertEqual(len(set(tracked.splitlines())), 21)
        self.assertNotIn('reference-data.json', tracked)

    def test_regenerate_order_and_fail_fast(self):
        reference = self.directory / 'docs/reference'
        reference.mkdir(parents=True)
        (self.directory / 'addons/tcxLua/src/generated').mkdir(parents=True)
        (self.directory / 'tools').mkdir()
        for name in ('check_build_warnings.py', 'build_warning_allowlist.json'):
            (self.directory / 'tools' / name).write_bytes((ROOT / 'tools' / name).read_bytes())
        script = reference / 'regenerate.sh'
        script.write_text((ROOT / 'docs/reference/regenerate.sh').read_text(encoding="utf-8"), encoding="utf-8")
        stub = '''import json, os, sys
with open(os.environ['CALLS'], 'a', encoding='utf-8') as log:
    log.write(json.dumps([os.path.basename(sys.argv[0]), *sys.argv[1:]]) + '\\n')
if any(os.path.basename(a) == os.environ.get('FAIL_AT') for a in sys.argv[1:]):
    sys.exit(1)
'''
        for name in ('cmake', 'node'):
            self.stub(name, stub)
        expected = ['structure.js', 'generate.js', 'emit-forai.js', 'emit-of.js',
                    'emit-coverage.js', 'luagen.js', 'luagen-types.js', 'emit-web.js',
                    'emit-sketch-api.js', 'emit-sketch-reference.js']
        for args, fail_at in [([], ''), (['--shaders-only'], ''), ([], 'structure.js')]:
            with self.subTest(args=args, fail_at=fail_at):
                (self.directory / 'calls').write_text('', encoding="utf-8")
                result = subprocess.run(['bash', str(script), *args], cwd=self.directory,
                                        env=dict(self.env, FAIL_AT=fail_at))
                self.assertEqual(result.returncode, 1 if fail_at else 0)
                calls = [json.loads(line) for line in (self.directory / 'calls').read_text(encoding="utf-8").splitlines()]
                self.assertEqual(calls[0][:2], ['cmake', '-S'])
                self.assertEqual(calls[1][0], 'cmake')
                self.assertIn('TrussC_shaders_gen', calls[1])
                emitters = [next(Path(a).name for a in call if a.endswith('.js')) for call in calls[2:]]
                self.assertEqual(emitters, [] if args else expected[:1] if fail_at else expected)

    def test_structure_rejects_failed_ast(self):
        # A large clang stdout must never make a fatal parse look successful,
        # including the --ids path used by check.js and cached AST export.
        runner = r"""
const assert = require('assert/strict'), fs = require('fs'), vm = require('vm');
const file = process.argv[1], realRequire = require('module').createRequire(file);
for (const failure of [
    { status: 1, stderr: 'fatal error: missing.glsl.h' },
    { status: 2, stderr: 'ordinary compiler error' },
    { status: 3, stderr: '' },
    { code: 'ENOBUFS', message: 'spawnSync /bin/sh ENOBUFS', stderr: '' },
]) for (const args of [['--ids'], ['--json', 'output'], ['--save-ast', 'output']]) {
    let status, stderr = '', writes = [];
    const stopped = new Error('exit');
    try {
        vm.runInNewContext(fs.readFileSync(file, 'utf8'), {
            require: name => name === 'child_process' ? {
                execSync: () => { throw { stdout: 'x'.repeat(200000), ...failure }; },
            } : name === 'fs' ? { writeFileSync: name => writes.push(name) } : realRequire(name),
            __dirname: require('path').dirname(file),
            process: { argv: ['node', file, ...args], env: { CXX: 'clang++' },
                stderr: { write: text => { stderr += text; } },
                exit: code => { status = code; throw stopped; } },
        }, { filename: file });
    } catch (error) { if (error !== stopped) throw error; }
    assert.equal(status, 1);
    if (failure.stderr.startsWith('fatal error:')) {
        assert.match(stderr, /hint:.*core\/include\/tc\/gpu\/shaders\/\*\.glsl\.h.*generate the shaders first/);
    }
    if (failure.code) {
        assert.match(stderr, /AST dump could not be read\/parsed: clang output exceeded the 1073741824-byte capture limit/);
        assert.doesNotMatch(stderr, /clang AST dump FAILED/);
    } else {
        assert.match(stderr, /clang AST dump FAILED/);
    }
    if (failure.stderr) assert.ok(stderr.includes('compiler stderr:\n' + failure.stderr));
    else assert.doesNotMatch(stderr, /compiler stderr/);
    assert.doesNotMatch(stderr, /using.*PARTIAL AST/);
    assert.equal(writes.length, 1); // probe input only, no exported AST/structure
}
"""
        subprocess.run(['node', '-e', runner, str(ROOT / 'docs/reference/structure.js')], check=True)

    def test_join_sorts_input_ids(self):
        entries = {'zzFixture': {'kind': 'func', 'name': 'zzFixture', 'signatures': []},
                   'aaFixture': {'kind': 'func', 'name': 'aaFixture', 'signatures': []}}
        outputs = []
        for data in (entries, dict(reversed(list(entries.items())))):
            structure = self.directory / 'structure.json'
            structure.write_text(json.dumps(data), encoding="utf-8")
            output = self.directory / 'reference.json'
            # The join uses no authored prose for these fixture ids. Mock the
            # TOML parser so this workflow test needs no npm installation.
            runner = """
const fs = require('fs'), vm = require('vm');
const file = process.argv[1], realRequire = require('module').createRequire(file);
process.argv = ['node', file, ...process.argv.slice(2)];
vm.runInNewContext(fs.readFileSync(file, 'utf8'), {
    require: name => name.endsWith('/node_modules/smol-toml') ? { parse: () => ({}) } : realRequire(name),
    __dirname: require('path').dirname(file), process, console,
}, { filename: file });
"""
            subprocess.run(['node', '-e', runner, str(ROOT / 'docs/reference/generate.js'), '--structure', str(structure),
                            '--out-json', str(output), '--out-md', str(self.directory / 'index.md')],
                           check=True, stdout=subprocess.PIPE)
            outputs.append(output.read_bytes())
        self.assertEqual(outputs[0], outputs[1])
        self.assertEqual(list(json.loads(outputs[0])), sorted(entries))
        # Old partial caches cannot bypass the failure via the former override.
        structure.write_text(json.dumps({'__meta': {'partialAst': True}}), encoding="utf-8")
        output.unlink()
        result = subprocess.run(['node', '-e', runner, str(ROOT / 'docs/reference/generate.js'),
                                 '--structure', str(structure), '--out-json', str(output),
                                 '--out-md', str(self.directory / 'index.md')],
                                env=dict(os.environ, TC_ALLOW_PARTIAL_AST='1'),
                                capture_output=True, text=True, encoding="utf-8", errors="replace")
        self.assertEqual(result.returncode, 1)
        self.assertIn('REFUSING', result.stderr)
        self.assertFalse(output.exists())


class StructureAstTests(unittest.TestCase):
    """Synthetic cached dumps; no clang or npm dependencies."""
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory()
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name)
        self.ast = self.directory / 'ast.json'
        self.output = self.directory / 'structure.json'

    def run_structure(self, *args):
        return subprocess.run(['node', str(ROOT / 'docs/reference/structure.js'), '--ast', str(self.ast), *args],
                              capture_output=True, text=True, encoding='utf-8')

    def test_many_objects_and_cached_export(self):
        # Braces, quotes, backslashes and UTF-8 inside strings must not affect
        # byte-level framing. Assert every symbol so skipped objects cannot pass.
        nodes = []
        for index in range(1000):
            nodes.append(json.dumps({
                'kind': 'NamespaceDecl', 'name': 'trussc',
                'loc': {'file': '/fixture/core/include/TrussC.h'},
                'note': '日本語 { } " \\ }',
                'inner': [{'kind': 'FunctionDecl', 'name': f'fixture{index}',
                           'type': {'qualType': 'void ()'}}],
            }, ensure_ascii=False))
        self.ast.write_text(' \t\r\n' + '\n'.join(nodes) + '\n', encoding='utf-8')
        result = self.run_structure('--json', str(self.output))
        self.assertEqual(result.returncode, 0, result.stderr)
        structure = json.loads(self.output.read_text(encoding='utf-8'))
        self.assertEqual({key for key in structure if key.startswith('fixture')},
                         {f'fixture{i}' for i in range(1000)})
        result = self.run_structure('--ids')
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), sorted(structure))
        exported = self.directory / 'exported.json'
        result = self.run_structure('--save-ast', str(exported))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(exported.read_bytes(), self.ast.read_bytes())

    def test_bad_dumps_fail_loudly(self):
        for data, cause in [('', 'no top-level JSON objects'),
                            ('{}\ntrailing', "expected '{' at byte 3"),
                            ('{}\n{"kind":', 'unterminated object starting at byte 3'),
                            ('{}\n{"kind": invalid}', 'object starting at byte 3:')]:
            for args in [('--ids',), ('--json', str(self.output))]:
                with self.subTest(data=data, args=args):
                    self.ast.write_text(data, encoding='utf-8')
                    result = self.run_structure(*args)
                    self.assertEqual(result.returncode, 1, result.stderr)
                    self.assertIn('AST dump could not be read/parsed:', result.stderr)
                    self.assertIn(cause, result.stderr)
                    self.assertNotIn('compiler stderr', result.stderr)
                    self.assertNotIn('clang AST dump FAILED', result.stderr)
                    self.assertFalse(self.output.exists())

    def test_missing_cache_reports_read_error(self):
        result = self.run_structure('--ids')
        self.assertEqual(result.returncode, 1)
        self.assertIn('AST dump could not be read/parsed:', result.stderr)
        self.assertIn('ENOENT', result.stderr)
        self.assertIn(str(self.ast), result.stderr)
        self.assertNotIn('compiler stderr', result.stderr)


if __name__ == '__main__':
    unittest.main()
