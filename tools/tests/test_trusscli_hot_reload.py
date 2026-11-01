"""One-step hot-reload toggles through a freshly built trusscli, without a display.

Set TRUSSCLI to opt in: this builds a real app against this checkout's core.
Run via tcbuild.sh. Temporary projects never use trusscli generate/update.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
NATIVE = {'linux': 'linux', 'darwin': 'macos', 'win32': 'windows'}[sys.platform]


@unittest.skipUnless(os.environ.get('TRUSSCLI'), 'set TRUSSCLI to a freshly built CLI')
class HotReloadToggleTests(unittest.TestCase):
    def test_toggle_and_scan_rules(self):
        with tempfile.TemporaryDirectory(prefix='trusscli-hot-reload-') as scratch:
            project = Path(scratch) / 'app with spaces'
            (project / 'src/nested').mkdir(parents=True)
            (project / 'addons.make').write_text('', encoding='utf-8')
            (project / 'src/main.cpp').write_text(
                '#include "nested/tcApp.h"\n'
                'int main() { tc::WindowSettings s; return TC_RUN_APP(tcApp, s); }\n',
                encoding='utf-8')
            (project / 'src/nested/tcApp.h').write_text(
                '#include "TrussC.h"\nclass tcApp : public tc::App {};\n', encoding='utf-8')
            app_source = project / 'src/nested/tcApp.cpp'
            base = '#include "tcApp.h"\n'
            app_source.write_text(base, encoding='utf-8')
            cmakelists = project / 'CMakeLists.txt'
            cmakelists.write_text(f'''cmake_minimum_required(VERSION 3.20)
project(toggle_probe)
set(TRUSSC_DIR "{(ROOT / 'core').as_posix()}")
include("${{TRUSSC_DIR}}/cmake/trussc_app.cmake")
trussc_app(NAME toggle_probe)
file(APPEND "${{CMAKE_BINARY_DIR}}/configure-calls" "configure\\n")
''', encoding='utf-8')
            (project / 'CMakePresets.json').write_text(json.dumps({
                'version': 3, 'configurePresets': [{'name': NATIVE,
                    'generator': 'Unix Makefiles', 'binaryDir': '${sourceDir}/build-' + NATIVE,
                    'cacheVariables': {'FETCHCONTENT_FULLY_DISCONNECTED': 'ON',
                                       'CMAKE_EXPORT_COMPILE_COMMANDS': 'ON'}}],
                'buildPresets': [{'name': NATIVE, 'configurePreset': NATIVE}]}), encoding='utf-8')
            build = project / ('build-' + NATIVE)
            cli = str(Path(os.environ['TRUSSCLI']).resolve())
            env = dict(os.environ, TRUSSC_DIR=str(ROOT))

            def command(argv, success=True):
                result = subprocess.run(argv, cwd=project, env=env, capture_output=True,
                                        text=True, encoding='utf-8', errors='replace', timeout=600)
                output = result.stdout + result.stderr
                evidence = os.environ.get('TC_HOT_RELOAD_TEST_LOG')
                if evidence:
                    with open(evidence, 'a', encoding='utf-8') as log:
                        log.write(f'$ {argv!r}\n{output}\nexit={result.returncode}\n')
                self.assertEqual(result.returncode == 0, success, output)
                return output

            def build_app(state, configure_count):
                output = command([cli, 'build', '-p', str(project), '-j2'])
                self.assertNotIn('hot reload state changed -- reconfigure required', output)
                self.assertEqual((build / '.tc_hot_reload_state').read_text(encoding="utf-8").strip(), state)
                self.assertEqual((build / 'configure-calls').read_text(encoding="utf-8").count('configure\n'),
                                 configure_count, output)
                if state == 'ON':
                    library = {'linux': 'libguest.so', 'darwin': 'libguest.dylib',
                               'win32': 'guest.dll'}[sys.platform]
                    self.assertTrue((build / library).is_file(), output)
                return output

            build_app('OFF', 1)
            build_app('OFF', 1)
            # The recursive scan accepts only .cpp and ignores lines with '/' before the marker.
            for ignored in ('// TC_HOT_RELOAD(tcApp)', 'int unused; // TC_HOT_RELOAD(tcApp)',
                            '/* TC_HOT_RELOAD(tcApp) */'):
                app_source.write_text(base + ignored + '\n', encoding='utf-8')
                build_app('OFF', 1)
            (project / 'src/nested/ignored.hpp').write_text('TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
            build_app('OFF', 1)
            app_source.write_text(base + 'TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
            build_app('ON', 2)
            commands = json.loads((build / 'compile_commands.json').read_text(encoding="utf-8"))
            host = [c['command'] for c in commands if c['file'].endswith('/src/main.cpp')]
            guest = [c['command'] for c in commands if c['file'].endswith('/src/nested/tcApp.cpp')]
            self.assertTrue(host and guest)
            self.assertTrue(all('TC_HOT_RELOAD_BUILD' in c and 'TC_HOT_RELOAD_GUEST' not in c for c in host))
            self.assertTrue(all('TC_HOT_RELOAD_GUEST' in c for c in guest))
            evidence = os.environ.get('TC_HOT_RELOAD_TEST_LOG')
            if evidence:
                Path(evidence).with_suffix('.targets.json').write_text(
                    json.dumps({'host': host, 'guest': guest}, indent=2), encoding='utf-8')
            build_app('ON', 2)
            app_source.write_text(base, encoding='utf-8')
            build_app('OFF', 3)
            build_app('OFF', 3)
            # Plain CMake must still fail once when toggled, then reconfigure next time.
            app_source.write_text(base + 'TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
            output = command(['cmake', '--build', str(build), '--parallel', '2'], success=False)
            self.assertIn('hot reload state changed -- reconfigure required', output)
            build_app('ON', 4)
            # An excluded .cpp must not influence an explicit SOURCES project.
            cmakelists.write_text(cmakelists.read_text(encoding="utf-8").replace('trussc_app(NAME toggle_probe)',
                'trussc_app(NAME toggle_probe SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp" '
                '"${CMAKE_CURRENT_SOURCE_DIR}/src/nested/tcApp.cpp" '
                '"${CMAKE_CURRENT_SOURCE_DIR}/src/nested/tcApp.h")'), encoding='utf-8')
            app_source.write_text(base, encoding='utf-8')
            build_app('OFF', 5)
            (project / 'src/excluded.cpp').write_text('TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
            build_app('OFF', 5)
            app_source.write_text(base + 'TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
            build_app('ON', 6)
            build_app('ON', 6)
            # Relative explicit SOURCES resolve against the source directory, so the
            # pre-check sees the same files from the build directory.
            cmakelists.write_text(cmakelists.read_text(encoding="utf-8").replace('"${CMAKE_CURRENT_SOURCE_DIR}/src/', '"src/'),
                                  encoding='utf-8')
            app_source.write_text(base, encoding='utf-8')
            build_app('OFF', 7)
            build_app('OFF', 7)
            app_source.write_text(base + 'TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
            build_app('ON', 8)
            build_app('ON', 8)


if __name__ == '__main__':
    unittest.main()
