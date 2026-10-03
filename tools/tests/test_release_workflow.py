"""Offline checks of the release workflow's actual scripts (requires PyYAML).

Optional Linux integration: set RELEASE_TEST_EXECUTABLE to a RelWithDebInfo
ELF built through the normal TrussC build. Platform tool mocks verify staging
and signing order; they do not claim macOS or Windows device coverage.
"""
import binascii
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import tarfile
import tempfile
import unittest
import zipfile

import yaml

ROOT = Path(__file__).resolve().parents[2]
WORKFLOW = yaml.load((ROOT / '.github/workflows/release.yml').read_text(), Loader=yaml.BaseLoader)


def step(job, name):
    return next(s for s in WORKFLOW['jobs'][job]['steps'] if s.get('name') == name)


def cmake_script_writer(variable):
    """Exercise the production writer without configuring/building TrussC."""
    source = (ROOT / 'core/cmake/trussc_app.cmake').read_text()
    start = source.index(f'file(WRITE "${{{variable}}}"')
    end = source.index('\n")', start) + len('\n")')
    return source[start:end]


class ReleaseWorkflowTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.project = self.root / 'project with spaces'
        self.runner = self.root / 'runner'
        self.commands = self.root / 'commands'
        for path in (self.project, self.runner, self.commands):
            path.mkdir()
        self.env = dict(os.environ, RUNNER_TEMP=str(self.runner),
                        GITHUB_WORKSPACE=str(self.root), GITHUB_REF_NAME='v1.2.3',
                        GITHUB_OUTPUT=str(self.root / 'output'), GITHUB_ENV=str(self.root / 'env'),
                        PATH=str(self.commands) + os.pathsep + os.environ['PATH'])
        self.data = self.project / 'bin/data'
        self.data.mkdir(parents=True)
        (self.data / '日本語 sub').mkdir()
        (self.data / '日本語 sub/asset.txt').write_text('packaged asset\n')
        (self.data / '.hidden').write_text('hidden asset\n')
        (self.data / 'lookup.pdb').write_text('asset, not build symbols\n')
        (self.data / 'lookup.debug').write_text('asset, not build symbols\n')
        (self.data / 'empty').mkdir()

    def mock(self, name, body):
        path = self.commands / name
        path.write_text('#!/usr/bin/env python3\n' + body)
        path.chmod(0o755)

    def run_step(self, job, name, **env):
        script = step(job, name)['run']
        self.assertNotIn('${{', script, 'tested scripts must take inputs through environment variables')
        return subprocess.run(['bash', '-e', '-o', 'pipefail', '-c', script],
                              cwd=self.project, env=dict(self.env, **env),
                              text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def ok(self, result):
        self.assertEqual(result.returncode, 0, result.stdout + (result.stderr or ''))

    def test_workflow_shell_syntax_and_contract(self):
        for job_name, job in WORKFLOW['jobs'].items():
            for item in job['steps']:
                if 'run' not in item or (job_name == 'windows' and item.get('shell') != 'bash'):
                    continue
                script = re.sub(r'\$\{\{.*?\}\}', 'TEST_VALUE', item['run'])
                result = subprocess.run(['bash', '-n'], input=script, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, f"{job_name}/{item.get('name')}: {result.stderr}")
        self.assertEqual(WORKFLOW['on']['workflow_call']['inputs']['publish']['default'], 'true')
        self.assertIn('inputs.publish', WORKFLOW['jobs']['release']['if'])
        weekly = yaml.load((ROOT / '.github/workflows/release-check.yml').read_text(), Loader=yaml.BaseLoader)
        inputs = weekly['jobs']['release-check']['with']
        self.assertEqual(inputs['publish'], 'false')
        self.assertEqual(inputs['sign-identity'], '-')
        self.assertTrue((ROOT / inputs['project-path'] / 'bin/data').is_dir())
        for job, runner in [('macos', 'macos-15'), ('windows', 'windows-2025'), ('linux', 'ubuntu-24.04')]:
            self.assertEqual(WORKFLOW['jobs'][job]['runs-on'], runner)
            self.assertEqual(WORKFLOW['jobs'][job]['needs'], 'version')
            self.assertIn('RelWithDebInfo', step(job, 'Configure (CMake)')['run'])
            self.assertEqual(step(job, 'Upload symbols')['with']['if-no-files-found'], 'error')
        plist = (ROOT / 'core/resources/Info.plist.in').read_bytes()
        self.assertEqual(plistlib.loads(plist)['LSMinimumSystemVersion'], '${CMAKE_OSX_DEPLOYMENT_TARGET}')

    def test_version_precedence_and_latest_failure(self):
        self.mock('gh', "import os, sys\nfrom pathlib import Path\n"
                        "Path(os.environ['GH_CALL_LOG']).write_text(' '.join(sys.argv[1:]))\n"
                        "print(os.environ.get('LATEST_TAG', 'v0.7.5'))\n"
                        "sys.exit(int(os.environ.get('GH_EXIT', '0')))\n")
        log = self.root / 'gh-call'
        env = dict(REQUESTED_REF='explicit-commit', GH_REPO='TrussC-org/TrussC', GH_CALL_LOG=str(log))
        pin = self.project / '.trussc-version'
        pin.write_text('v0.7.4\n')
        self.ok(self.run_step('version', 'Resolve TrussC version', **env))
        self.assertEqual((self.root / 'output').read_text(), 'ref=explicit-commit\n')
        self.assertFalse(log.exists())
        (self.root / 'output').unlink()
        env['REQUESTED_REF'] = ''
        self.ok(self.run_step('version', 'Resolve TrussC version', **env))
        self.assertEqual((self.root / 'output').read_text(), 'ref=v0.7.4\n')
        self.assertFalse(log.exists())
        pin.unlink()
        (self.root / 'output').unlink()
        self.ok(self.run_step('version', 'Resolve TrussC version', **env))
        self.assertEqual((self.root / 'output').read_text(), 'ref=v0.7.5\n')
        self.assertEqual(log.read_text(), 'api repos/TrussC-org/TrussC/releases/latest --jq .tag_name')
        for override in [{'LATEST_TAG': 'null'}, {'GH_EXIT': '1'}]:
            self.assertNotEqual(self.run_step('version', 'Resolve TrussC version', **env, **override).returncode, 0)

    def test_revision_and_release_notes_with_single_platform(self):
        self.mock('git', "import sys\nprint('0123456789abcdef' if 'rev-parse' in sys.argv else 'v0.7.5')\n")
        self.ok(self.run_step('linux', 'Record TrussC revision', TRUSSC_REF='v0.7.5', RUNNER_OS='Linux'))
        revision = (self.runner / 'revision/trussc-Linux.md').read_text()
        self.assertIn('tag `v0.7.5`, commit `0123456789abcdef`', revision)
        artifacts = self.project / 'artifacts'
        for name in ['trussc-revision-linux', 'linux-tar', 'linux-symbols']:
            (artifacts / name).mkdir(parents=True)
        (artifacts / 'trussc-revision-linux/revision.md').write_text(revision)
        (artifacts / 'linux-tar/app.tar.gz').write_text('package')
        (artifacts / 'linux-symbols/app-debug.tar.gz').write_text('symbols')
        self.mock('gh', "import json, os, sys\nfrom pathlib import Path\n"
                        "Path(os.environ['GH_CALL_LOG']).write_text(json.dumps(sys.argv[1:]))\n")
        self.ok(self.run_step('release', 'Create GitHub Release', GH_CALL_LOG=str(self.root / 'gh-call')))
        args = json.loads((self.root / 'gh-call').read_text())
        self.assertIn('artifacts/linux-tar/app.tar.gz', args)
        self.assertIn('artifacts/linux-symbols/app-debug.tar.gz', args)
        self.assertFalse(any('*' in arg or 'revision.md' in arg for arg in args))
        self.assertEqual((self.project / 'release-notes.md').read_text(), revision)

    def test_hot_reload_guard_on_off_crlf_and_missing(self):
        (self.project / 'build').mkdir()
        state = self.project / 'build/.tc_hot_reload_state'
        for job in ['macos', 'windows', 'linux']:
            for value in ['ON', 'ON\r\n']:
                state.write_bytes(value.encode())
                self.assertNotEqual(self.run_step(job, 'Reject hot reload').returncode, 0)
            for value in ['OFF', 'OFF\r\n']:
                state.write_bytes(value.encode())
                self.ok(self.run_step(job, 'Reject hot reload'))
            state.unlink()
            self.assertNotEqual(self.run_step(job, 'Reject hot reload').returncode, 0)

    def test_generated_hot_reload_check_with_backslash_paths(self):
        src = self.project / 'src/nested'
        src.mkdir(parents=True)
        source = src / 'app.cpp'
        source.write_text('TC_HOT_RELOAD(App)\n')
        cmakelists = self.project / 'CMakeLists.txt'
        cmakelists.touch()
        state = self.project / '.tc_hot_reload_state'
        generated = self.root / '_tc_check_hot_reload.cmake'
        generator = self.root / 'generate.cmake'
        native = lambda path: str(path).replace('/', '\\')
        writer = cmake_script_writer('_TC_HR_CHECK_SCRIPT')
        # Apply modern CMake policies even on older Linux installations.
        driver = self.root / 'run-check.cmake'
        driver.write_text(f'cmake_minimum_required(VERSION 3.20)\ninclude([==[{generated}]==])\n')
        check_command = ['cmake', '-P', str(driver)]
        for explicit in [False, True]:
            with self.subTest(explicit=explicit):
                values = {
                    '_TC_HR_CHECK_SCRIPT': str(generated),
                    '_TC_HR_STATE_FILE': str(state),
                    '_TC_HR_SRC_DIR': str(self.project / 'src'),
                    '_TC_HR_CMAKELISTS': str(cmakelists),
                    '_TC_HR_PLATFORM_SUPPORTED': 'TRUE',
                    '_TC_HR_EXPLICIT_SOURCES': str(source) if explicit else '',
                    'TRUSSC_DIR': native(ROOT / 'core'),
                }
                generator.write_text('cmake_minimum_required(VERSION 3.20)\n' + ''.join(
                    f'set({key} [==[{value}]==])\n' for key, value in values.items()) + writer)
                self.ok(subprocess.run(['cmake', '-P', str(generator)], text=True, capture_output=True))
                source.write_text('TC_HOT_RELOAD(App)\n')
                state.write_text('ON')
                self.ok(subprocess.run(check_command, text=True, capture_output=True))
                source.write_text('int x;\n')
                result = subprocess.run(check_command, text=True, stderr=subprocess.STDOUT,
                                        stdout=subprocess.PIPE)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertIn('hot reload state changed', result.stdout)
                state.write_text('OFF')
                self.ok(subprocess.run(check_command, text=True, capture_output=True))

        # Drive paths from the failing runner must also survive the CMake parser.
        scanner = self.root / 'D:/a/TrussC/TrussC/TrussC/core/cmake/tc_hot_reload_scan.cmake'
        scanner.parent.mkdir(parents=True)
        shutil.copy2(ROOT / 'core/cmake/tc_hot_reload_scan.cmake', scanner)
        values.update(_TC_HR_PLATFORM_SUPPORTED='FALSE',
                      TRUSSC_DIR=r'D:\a\TrussC\TrussC/TrussC/core',
                      _TC_HR_SRC_DIR=r'D:\a\TrussC\TrussC\src',
                      _TC_HR_CMAKELISTS=r'D:\a\TrussC\TrussC\CMakeLists.txt',
                      _TC_HR_EXPLICIT_SOURCES=r'D:\a\app with spaces\one.cpp;D:\a\app\two.cpp')
        generator.write_text('cmake_minimum_required(VERSION 3.20)\n' + ''.join(
            f'set({key} [==[{value}]==])\n' for key, value in values.items()) + writer)
        self.ok(subprocess.run(['cmake', '-P', str(generator)], text=True, capture_output=True))
        with generated.open('a') as out:
            for key, value in [('SRC_DIR', values['_TC_HR_SRC_DIR']),
                               ('CMAKELISTS', values['_TC_HR_CMAKELISTS']),
                               ('EXPLICIT_SOURCES', values['_TC_HR_EXPLICIT_SOURCES'])]:
                out.write(f'\nif(NOT "${{{key}}}" STREQUAL [==[{value}]==])\n'
                          f'    message(FATAL_ERROR "{key} changed during parsing")\nendif()\n')
        self.ok(subprocess.run(check_command, cwd=self.root, text=True, capture_output=True))

    def test_generated_windows_exports_uses_runtime_paths(self):
        # The other generated CMake script takes paths through -D at execution,
        # rather than baking them into quoted source. Guard that distinction.
        generated = self.root / '_tc_gen_exports.cmake'
        generator = self.root / 'generate-exports.cmake'
        generator.write_text(f'set(_TC_DEF_SCRIPT [==[{generated}]==])\n' +
                             cmake_script_writer('_TC_DEF_SCRIPT'))
        self.ok(subprocess.run(['cmake', '-P', str(generator)], text=True, capture_output=True))
        library = r'D:\a\app with spaces\TrussC.lib'
        self.mock('dumpbin', "import os, sys\n"
                            "assert sys.argv[1:] == ['/LINKERMEMBER:1', os.environ['LIBRARY_PATH']]\n"
                            "print('  000001 ?fixture@@YAXXZ')\n")
        exports = self.project / 'exports.def'
        result = subprocess.run(['cmake', f'-DLIB_FILE={library}', f'-DDEF_FILE={exports}',
                                 f'-DDUMPBIN={self.commands / "dumpbin"}', '-P', str(generated)],
                                env=dict(self.env, LIBRARY_PATH=library), text=True, capture_output=True)
        self.ok(result)
        self.assertEqual(exports.read_text(), 'EXPORTS\n    ?fixture@@YAXXZ\n')

    def test_windows_archive_layout_and_separate_symbols(self):
        self.mock('7z', "import pathlib, sys, zipfile\n"
                        "args=sys.argv[1:]\n"
                        "if args[0] == 'a':\n"
                        "    with zipfile.ZipFile(args[1], 'w') as z:\n"
                        "        for p in pathlib.Path('.').rglob('*'):\n"
                        "            z.write(p, p.as_posix())\n"
                        "else:\n"
                        "    with zipfile.ZipFile(args[1]) as z:\n"
                        "        z.extractall(args[2][2:])\n")
        for layout in ['bin', 'bin/RelWithDebInfo']:
            with self.subTest(layout=layout):
                dist = self.project / layout
                dist.mkdir(exist_ok=True)
                (dist / 'Fixture.exe').write_bytes(b'fixture executable')
                (dist / 'dependency.dll').write_bytes(b'fixture DLL')
                (dist / 'Fixture.pdb').write_bytes(b'fixture symbols')
                (self.project / 'build').mkdir(exist_ok=True)
                self.ok(self.run_step('windows', 'Package (zip)'))
                with zipfile.ZipFile(self.runner / 'Fixture-v1.2.3-windows.zip') as z:
                    names = z.namelist()
                    self.assertIn('Fixture.exe', names)
                    self.assertIn('dependency.dll', names)
                    self.assertIn('data/日本語 sub/asset.txt', names)
                    self.assertIn('data/.hidden', names)
                    self.assertIn('data/empty/', names)
                    self.assertFalse(any(n.endswith('.pdb') and '/' not in n for n in names))
                with zipfile.ZipFile(self.runner / 'Fixture-PDB.zip') as z:
                    self.assertEqual(z.read('Fixture.pdb'), b'fixture symbols')
                for name in ['Fixture.exe', 'dependency.dll', 'Fixture.pdb']:
                    (dist / name).unlink()
                for child in self.runner.iterdir():
                    if child.is_dir():
                        shutil.rmtree(child)
                    else:
                        child.unlink()

    def test_mac_staging_and_older_plist_minimum(self):
        app = self.project / 'bin/Fixture.app'
        (app / 'Contents/MacOS').mkdir(parents=True)
        (app / 'Contents/MacOS/Fixture').write_text('fixture executable')
        info = app / 'Contents/Info.plist'
        for minimum in [None, '12.0']:
            metadata = {'CFBundleExecutable': 'Fixture'}
            if minimum is not None:
                metadata['LSMinimumSystemVersion'] = minimum
            info.write_bytes(plistlib.dumps(metadata))
            self.ok(self.run_step('macos', 'Set minimum macOS version', APP_PATH=str(app)))
            result = plistlib.loads(info.read_bytes())
            self.assertEqual(result['LSMinimumSystemVersion'], '14.0')
            self.assertEqual(result['CFBundleExecutable'], 'Fixture')
        self.mock('ditto', "import shutil, sys\nshutil.copytree(sys.argv[1], sys.argv[2], dirs_exist_ok=True)\n")
        self.mock('dsymutil', "import pathlib, sys\n"
                             "out = pathlib.Path(sys.argv[sys.argv.index('-o') + 1]) / 'Contents/Resources/DWARF/Fixture'\n"
                             "out.parent.mkdir(parents=True)\nout.write_bytes(b'mock dSYM')\n")
        self.ok(self.run_step('macos', 'Bundle data and extract symbols', APP_PATH=str(app), APP_NAME='Fixture'))
        resources = app / 'Contents/Resources/data'
        self.assertEqual((resources / '日本語 sub/asset.txt').read_bytes(), (self.data / '日本語 sub/asset.txt').read_bytes())
        self.assertTrue((resources / '.hidden').is_file())
        self.assertTrue((resources / 'empty').is_dir())
        self.assertFalse(list(app.rglob('*.dSYM')))
        with tarfile.open(self.runner / 'Fixture-dSYM.tar.gz') as archive:
            self.assertEqual(archive.extractfile('./Fixture.dSYM/Contents/Resources/DWARF/Fixture').read(), b'mock dSYM')

    def test_mac_signing_order_defaults_and_replacement(self):
        app = self.project / 'bin/Fixture.app'
        (app / 'Contents/MacOS').mkdir(parents=True)
        (app / 'Contents/MacOS/Fixture').write_text('main executable')
        framework = app / 'Contents/Frameworks/Nested.framework'
        (framework / 'Versions/A').mkdir(parents=True)
        (framework / 'Versions/A/Nested.dylib').write_text('nested dylib')
        helper = framework / 'Helpers/Helper.app'
        (helper / 'Contents/MacOS').mkdir(parents=True)
        (helper / 'Contents/MacOS/Helper').write_text('helper executable')
        self.mock('file', "import sys\nprint('Mach-O dynamically linked shared library' if sys.argv[-1].endswith('.dylib') else ('Mach-O executable' if '/MacOS/' in sys.argv[-1] else 'data'))\n")
        self.mock('plutil', "import plistlib, sys\nplistlib.load(open(sys.argv[-1], 'rb'))\n")
        self.mock('codesign', "import json, os, sys\n"
                             "with open(os.environ['SIGN_LOG'], 'a') as out: out.write(json.dumps(sys.argv[1:])+'\\n')\n")
        log = self.root / 'sign-log'
        custom = self.project / 'custom.entitlements'
        custom.write_bytes(plistlib.dumps({'com.apple.security.device.camera': True}))
        for replacement, identity in [('', '-'), ('custom.entitlements', 'Developer ID Application: Fixture')]:
            with self.subTest(replacement=replacement):
                self.ok(self.run_step('macos', 'Code Sign', APP_PATH=str(app),
                                      ENTITLEMENTS_INPUT=replacement, SIGN_IDENTITY=identity, SIGN_LOG=str(log)))
                calls = [json.loads(line) for line in log.read_text().splitlines()]
                signed = [call for call in calls if '--sign' in call]
                targets = [call[-1] for call in signed]
                self.assertLess(targets.index(str(helper)), targets.index(str(framework)))
                self.assertLess(targets.index(str(framework)), targets.index(str(app)))
                for call in signed:
                    self.assertNotIn('--deep', call)
                    self.assertIn('--options', call)
                    self.assertIn('--timestamp=none' if identity == '-' else '--timestamp', call)
                    if call[-1].endswith('.dylib'):
                        self.assertNotIn('--entitlements', call)
                    else:
                        entitlements = Path(call[call.index('--entitlements') + 1])
                        if not entitlements.is_absolute():
                            entitlements = self.project / entitlements
                        expected = plistlib.loads(custom.read_bytes() if replacement else (ROOT / 'core/resources/macos.entitlements').read_bytes())
                        self.assertEqual(plistlib.loads(entitlements.read_bytes()), expected)
                log.unlink()

    def test_mac_entitlements_xml_output_and_failures(self):
        self.mock('plutil', "print('14.0')\n")
        self.mock('xcrun', "print('    minos 14.0')\n")
        self.mock('lipo', "print('arm64')\n")
        self.mock('codesign', "import os, sys\nfrom pathlib import Path\n"
                             "assert '--xml' in sys.argv\n"
                             "sys.stderr.write('Executable=Fixture.app/Contents/MacOS/Fixture\\n')\n"
                             "sys.stdout.buffer.write(Path(os.environ['CODESIGN_OUTPUT']).read_bytes())\n"
                             "sys.exit(int(os.environ.get('CODESIGN_EXIT', '0')))\n")
        # Data staging has its own coverage; this test isolates entitlements.
        shutil.rmtree(self.data)
        output = self.root / 'codesign-output'
        env = dict(APP_PATH='Fixture.app', APP_NAME='Fixture', SIGN_IDENTITY='-',
                   ENTITLEMENTS_INPUT='', CODESIGN_OUTPUT=str(output))
        entitlements = {'com.apple.security.device.camera': True,
                        'com.apple.security.device.audio-input': True}
        xml = plistlib.dumps(entitlements)
        for payload in [xml, b'Executable=Fixture.app\n' + xml,
                        b'Fixture.app: warning: diagnostic\n' + xml + b'\ntrailing diagnostic\n']:
            with self.subTest(payload=payload):
                output.write_bytes(payload)
                self.ok(self.run_step('macos', 'Verify macOS bundle', **env))
        for invalid in [{}, {'com.apple.security.device.camera': True},
                        {'com.apple.security.device.audio-input': True},
                        dict(entitlements, **{'com.apple.security.device.camera': False}),
                        dict(entitlements, **{'com.apple.security.device.audio-input': False})]:
            with self.subTest(entitlements=invalid):
                output.write_bytes(plistlib.dumps(invalid))
                self.assertNotEqual(self.run_step('macos', 'Verify macOS bundle', **env).returncode, 0)
        for payload in [b'', b'not an XML plist', b'<plist><dict>', b'<plist>invalid</plist>']:
            with self.subTest(payload=payload):
                output.write_bytes(payload)
                self.assertNotEqual(self.run_step('macos', 'Verify macOS bundle', **env).returncode, 0)
        output.write_bytes(xml)
        self.assertNotEqual(self.run_step('macos', 'Verify macOS bundle', **env, CODESIGN_EXIT='1').returncode, 0)

    @unittest.skipUnless(os.environ.get('RELEASE_TEST_EXECUTABLE'), 'set RELEASE_TEST_EXECUTABLE for real ELF packaging')
    def test_linux_real_elf_archive_and_symbols(self):
        exe = Path(os.environ['RELEASE_TEST_EXECUTABLE']).resolve()
        shutil.copy2(exe, self.project / 'bin/Fixture')
        self.ok(self.run_step('linux', 'Package (tar.gz)'))
        with tarfile.open(self.runner / 'Fixture-v1.2.3-linux.tar.gz') as t:
            names = t.getnames()
            self.assertIn('./data/日本語 sub/asset.txt', names)
            self.assertIn('./data/.hidden', names)
            self.assertIn('./data/empty', names)
            self.assertFalse(any(n.endswith('.debug') and n.count('/') == 1 for n in names))
        packaged = self.runner / 'verify/Fixture'
        sections = subprocess.check_output(['readelf', '-S', str(packaged)], text=True)
        self.assertNotIn('.debug_info', sections)
        self.assertIn('.gnu_debuglink', sections)
        with tarfile.open(self.runner / 'Fixture-debug.tar.gz') as t:
            self.assertIn('./Fixture.debug', t.getnames())
        debug = self.runner / 'symbols/Fixture.debug'
        self.assertIn('.debug_info', subprocess.check_output(['objdump', '-h', str(debug)], text=True))
        link = self.root / 'debuglink'
        subprocess.run(['objcopy', '--dump-section', f'.gnu_debuglink={link}', str(packaged)], check=True)
        payload = link.read_bytes()
        name, _ = payload.split(b'\0', 1)
        self.assertEqual(name, b'Fixture.debug')
        self.assertEqual(int.from_bytes(payload[-4:], 'little'), binascii.crc32(debug.read_bytes()))
        # No-data apps must also package cleanly.
        shutil.rmtree(self.data)
        for folder in ['package', 'symbols', 'verify']:
            shutil.rmtree(self.runner / folder)
        self.ok(self.run_step('linux', 'Package (tar.gz)'))


if __name__ == '__main__':
    unittest.main()
