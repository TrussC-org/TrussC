"""Offline CMake checks for shared FetchContent storage (#737)."""
import importlib.util
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_all', ROOT / 'examples/build_all.py')
build_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_all)


class FetchContentCacheTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.scratch.cleanup)
        self.directory = Path(self.scratch.name)
        self.source = self.directory / 'project'
        self.source.mkdir()
        self.env = dict(os.environ)
        for name in ('TRUSSC_FETCHCONTENT_DIR', 'TRUSSC_FETCHCONTENT_UPDATES_DISCONNECTED'):
            self.env.pop(name, None)
        # A deterministic local download fixture: it records each call,
        # generates a tiny dependency, and never invokes git or the network.
        (self.source / 'download.cmake').write_text('''
file(APPEND "${LOG}" "download\\n")
file(MAKE_DIRECTORY "${DEST}")
file(WRITE "${DEST}/CMakeLists.txt"
    "cmake_minimum_required(VERSION 3.20)\\nproject(fixture C)\\nadd_library(fixture STATIC fixture.c)\\n")
file(WRITE "${DEST}/fixture.c" "int fixture(void) { return 42; }\\n")
''')
        (self.source / 'main.c').write_text(
            'int fixture(void);\nint main(void) { return fixture() == 42 ? 0 : 1; }\n')
        self.project = f'''
cmake_minimum_required(VERSION 3.20)
project(cache_fixture C)
include("{ROOT.as_posix()}/core/cmake/trussc_app.cmake")
file(WRITE "${{CMAKE_BINARY_DIR}}/hook-settings"
    "${{FETCHCONTENT_BASE_DIR}}\\n${{FETCHCONTENT_UPDATES_DISCONNECTED}}\\n")
include(FetchContent)
FetchContent_Declare(fixture
    DOWNLOAD_COMMAND "${{CMAKE_COMMAND}}" "-DLOG={self.directory.as_posix()}/calls"
        "-DDEST=<SOURCE_DIR>" -P "${{CMAKE_CURRENT_SOURCE_DIR}}/download.cmake")
FetchContent_MakeAvailable(fixture)
add_executable(probe main.c)
target_link_libraries(probe PRIVATE fixture)
enable_testing()
add_test(NAME probe COMMAND probe)
file(WRITE "${{CMAKE_BINARY_DIR}}/settings"
    "${{FETCHCONTENT_BASE_DIR}}\\n${{FETCHCONTENT_UPDATES_DISCONNECTED}}\\n${{fixture_SOURCE_DIR}}\\n")
'''
        (self.source / 'CMakeLists.txt').write_text(self.project)

    def command(self, command, cwd=None):
        result = subprocess.run(command, cwd=cwd, env=self.env,
                                capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return result.stdout + result.stderr

    def configure(self, name, *options):
        build = self.directory / name
        # Exercise the same native configure command builder as build_all.py.
        command = build_all.cmake_config_cmd(str(build), {'cmake_generator': None})
        self.command(command + list(options), cwd=self.source)
        return build, (build / 'settings').read_text().splitlines()

    def calls(self):
        return (self.directory / 'calls').read_text().splitlines()

    def test_unset_uses_independent_default_directories(self):
        for name in ('first', 'second'):
            build, settings = self.configure(name)
            self.assertEqual(settings, [str(build / '_deps'), 'OFF',
                                        str(build / '_deps/fixture-src')])
        self.assertEqual(self.calls().count('download'), 2)

    def test_shared_directory_reuses_download_with_disconnected_hit(self):
        shared = self.directory / 'shared deps'
        self.env['TRUSSC_FETCHCONTENT_DIR'] = str(shared)
        first, settings = self.configure('first')
        self.assertEqual(settings, [str(shared), 'OFF', str(shared / 'fixture-src')])
        initial = self.calls()
        self.assertEqual(initial.count('download'), 1)
        # Later examples in the same cold job share the download too.
        self.configure('second-cold')
        self.assertEqual(self.calls(), initial)
        self.command(['cmake', '--build', str(first)])
        self.command(['ctest', '--test-dir', str(first), '--output-on-failure'])
        self.env['TRUSSC_FETCHCONTENT_UPDATES_DISCONNECTED'] = 'ON'
        second, settings = self.configure('second')
        self.assertEqual(settings, [str(shared), 'ON', str(shared / 'fixture-src')])
        self.assertEqual(self.calls(), initial)
        self.command(['cmake', '--build', str(second)])
        self.command(['ctest', '--test-dir', str(second), '--output-on-failure'])
        # Cached sources also remain usable on another configure of that app.
        self.configure('second')
        self.assertEqual(self.calls(), initial)

    def test_disconnected_updates_still_populate_missing_dependencies(self):
        self.env['TRUSSC_FETCHCONTENT_DIR'] = str(self.directory / 'partial cache')
        self.env['TRUSSC_FETCHCONTENT_UPDATES_DISCONNECTED'] = 'ON'
        _, settings = self.configure('missing')
        self.assertEqual(settings[1], 'ON')
        self.assertEqual(self.calls(), ['download'])

    def test_explicit_cache_values_override_environment_and_persist(self):
        explicit = self.directory / 'explicit deps'
        self.env['TRUSSC_FETCHCONTENT_DIR'] = str(self.directory / 'env deps')
        self.env['TRUSSC_FETCHCONTENT_UPDATES_DISCONNECTED'] = 'ON'
        _, settings = self.configure('explicit', f'-DFETCHCONTENT_BASE_DIR={explicit}',
                                     '-DFETCHCONTENT_UPDATES_DISCONNECTED=OFF')
        self.assertEqual(settings[:2], [str(explicit), 'OFF'])
        _, settings = self.configure('explicit')
        self.assertEqual(settings[:2], [str(explicit), 'OFF'])
        self.assertFalse((self.directory / 'env deps').exists())

    def test_normal_cmake_variables_override_environment(self):
        explicit = self.directory / 'normal deps'
        self.env['TRUSSC_FETCHCONTENT_DIR'] = str(self.directory / 'env deps')
        self.env['TRUSSC_FETCHCONTENT_UPDATES_DISCONNECTED'] = 'ON'
        (self.source / 'CMakeLists.txt').write_text(self.project.replace(
            'include("', f'set(FETCHCONTENT_BASE_DIR "{explicit.as_posix()}")\n'
            'set(FETCHCONTENT_UPDATES_DISCONNECTED OFF)\ninclude("', 1))
        build, _ = self.configure('normal')
        self.assertEqual((build / 'hook-settings').read_text().splitlines(),
                         [str(explicit), 'OFF'])
        self.assertFalse((self.directory / 'env deps').exists())


if __name__ == '__main__':
    unittest.main()
