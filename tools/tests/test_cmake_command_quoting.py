"""Offline production POST_BUILD quoting regression; run through tcbuild.sh."""

import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


@unittest.skipUnless(sys.platform == 'linux', 'POSIX command fixtures')
class CommandQuotingTests(unittest.TestCase):
    def test_shader_generation_in_special_character_folder(self):
        recipe = ROOT / 'core/cmake/trussc_shaders.cmake'
        host = 'linux_arm64' if platform.machine() == 'aarch64' else 'linux'
        pin = re.search(r'set\(_TC_SOKOL_SHDC_SHA256_' + host + r' "([a-f0-9]+)"\)',
                        recipe.read_text(encoding='utf-8')).group(1)
        compiler = ROOT / 'core/tools/sokol-shdc/sokol-shdc'
        # Never let this test trigger the pinned compiler download.
        if not compiler.is_file() or hashlib.sha256(compiler.read_bytes()).hexdigest() != pin:
            self.skipTest('needs the pinned sokol-shdc at core/tools/sokol-shdc')
        for generator in ('Unix Makefiles', 'Ninja'):
            if generator == 'Ninja' and not shutil.which('ninja'):
                continue
            with self.subTest(generator=generator), \
                    tempfile.TemporaryDirectory(prefix='tc-shader-quoting-') as scratch:
                project = Path(scratch) / "+ ( ) & '"
                project.mkdir()
                shutil.copyfile(ROOT / 'core/shaders/shadowDepth.glsl', project / 'test.glsl')
                (project / 'CMakeLists.txt').write_text(
                    'cmake_minimum_required(VERSION 3.20)\nproject(quoting NONE)\n'
                    + f'include("{recipe}")\n'
                    + 'trussc_compile_shaders(probe "${CMAKE_CURRENT_SOURCE_DIR}" '
                      'OUTPUT_DIR "${CMAKE_BINARY_DIR}/headers")\n', encoding='utf-8')
                build = project / 'build'
                for command in (['cmake', '-S', str(project), '-B', str(build), '-G', generator,
                                 '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'],
                                ['cmake', '--build', str(build), '--target', 'probe_shaders_gen']):
                    result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('shadow_depth', (build / 'headers/test.glsl.h').read_text(encoding='utf-8'))

    def test_macos_post_build_arguments_and_failure_policy(self):
        source = (ROOT / 'core/CMakeLists.txt').read_text(encoding='utf-8')
        blocks = re.findall(r'    add_custom_command\(TARGET \$\{TARGET_NAME\} POST_BUILD\n.*?\n    \)',
                            source, re.S)
        self.assertEqual(len(blocks), 2)
        for generator in ('Unix Makefiles', 'Ninja'):
            if generator == 'Ninja' and not shutil.which('ninja'):
                continue
            for add_status in (0, 7):
                with self.subTest(generator=generator, add_status=add_status), \
                        tempfile.TemporaryDirectory(prefix='tc-cmake-quoting-') as scratch:
                    project = Path(scratch) / "+ ( ) & '"
                    project.mkdir()
                    mock = project / 'mock-tool'
                    log = project / 'args.jsonl'
                    mock.write_text(f'#!{sys.executable}\n' + '''import json, os, sys
from pathlib import Path
with open(os.environ['TC_QUOTING_LOG'], 'a') as log:
    log.write(json.dumps(sys.argv[1:]) + '\\n')
if sys.argv[1:3] == ['-c', 'Delete :CFBundleIconName']:
    print('ignored delete failure', file=sys.stderr)
    sys.exit(3)
if sys.argv[1:2] == ['-c']:
    sys.exit(int(os.environ['TC_QUOTING_ADD_STATUS']))
''', encoding='utf-8')
                    mock.chmod(0o755)
                    commands = '\n'.join(blocks)
                    # Only platform tools and bundle location are substituted;
                    # production COMMAND arguments and VERBATIM remain intact.
                    commands = commands.replace('sh -c "/usr/libexec/PlistBuddy',
                                                'sh -c "' + r'\"${MOCK}\"')
                    commands = commands.replace('COMMAND /usr/libexec/PlistBuddy',
                                                'COMMAND "${MOCK}"')
                    commands = commands.replace('COMMAND xattr', 'COMMAND "${MOCK}"')
                    commands = commands.replace('COMMAND touch', 'COMMAND "${MOCK}" touch')
                    commands = commands.replace('TARGET_BUNDLE_DIR:', 'TARGET_FILE_DIR:')
                    (project / 'main.c').write_text('int main(void) { return 0; }\n', encoding='utf-8')
                    (project / 'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.20)
project(quoting C)
add_executable(probe main.c)
set(TARGET_NAME probe)
set(ICON_NAME "Test icon")
''' + f'set(MOCK "{mock}")\n' + commands, encoding='utf-8')
                    build = project / 'build'
                    env = dict(os.environ, TC_QUOTING_LOG=str(log),
                               TC_QUOTING_ADD_STATUS=str(add_status))
                    configured = subprocess.run(['cmake', '-S', str(project), '-B', str(build),
                                                 '-G', generator, '-DFETCHCONTENT_FULLY_DISCONNECTED=ON'],
                                                capture_output=True, text=True, encoding='utf-8', env=env)
                    self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
                    result = subprocess.run(['cmake', '--build', str(build)],
                                            capture_output=True, text=True, encoding='utf-8', env=env)
                    self.assertEqual(result.returncode == 0, add_status == 0,
                                     result.stdout + result.stderr)
                    self.assertNotIn('ignored delete failure', result.stderr)
                    self.assertEqual([json.loads(line) for line in log.read_text(encoding='utf-8').splitlines()], [
                        ['-cr', str(build)], ['touch', str(build)],
                        ['-c', 'Delete :CFBundleIconName', str(build / 'Contents/Info.plist')],
                        ['-c', 'Add :CFBundleIconName string Test icon', str(build / 'Contents/Info.plist')],
                    ])


if __name__ == '__main__':
    unittest.main()
