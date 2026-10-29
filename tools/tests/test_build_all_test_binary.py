"""Offline checks for build_all's exact native test output locations."""

from contextlib import redirect_stdout
import importlib.util
import io
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('build_all', ROOT / 'examples/build_all.py')
build_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_all)


def platform_info(system, generator=None):
    return {'os': system, 'build_dir': f'build-{system}', 'cmake_generator': generator}


class TestBinaryDiscovery(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        environment = patch.dict(os.environ)
        environment.start()
        self.addCleanup(environment.stop)
        os.environ.pop('CMAKE_GENERATOR', None)

    def cache(self, directory, system, contents):
        path = self.root / directory / f'build-{system}' / 'CMakeCache.txt'
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(contents, encoding='utf-8')

    def executable(self, relative, executable=True):
        path = self.root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text('test fixture', encoding='utf-8')
        path.chmod(0o755 if executable else 0o644)
        return path

    def find(self, directory, info):
        return build_all.find_test_binary(str(self.root / directory), info)

    def test_stray_sibling_is_ignored_when_platform_binary_is_missing(self):
        self.executable('nativeWindow/build-egl/nativeWindow')
        with redirect_stdout(io.StringIO()) as output:
            self.assertIsNone(self.find('nativeWindow', platform_info('linux')))
        self.assertIn(str(self.root / 'nativeWindow/build-linux/nativeWindow'), output.getvalue())
        self.assertNotIn('build-egl', output.getvalue())

    def test_uses_platform_build_dir_even_when_stray_was_created_first(self):
        for system, filename in [('linux', 'probe'), ('macos', 'probe'), ('windows', 'probe.exe')]:
            with self.subTest(system=system):
                directory = f'{system}/probe'
                self.executable(f'{directory}/build-egl/{filename}')
                self.executable(f'{directory}/build-other/{filename}')
                expected = self.executable(f'{directory}/build-{system}/{filename}')
                self.assertEqual(self.find(directory, platform_info(system)), str(expected))

    def test_uses_build_dir_from_platform_info(self):
        native = self.executable('probe/build-linux/probe')
        self.assertEqual(self.find('probe', {'os': 'linux'}), str(native))
        expected = self.executable('probe/build-custom/probe')
        info = dict(platform_info('linux'), build_dir='build-custom')
        self.assertEqual(self.find('probe', info), str(expected))

    def test_trusscli_outputs_for_combined_own_addon_and_example_projects(self):
        directories = ['core/tests/allCoreTests', 'core/tests/nativeWindow',
                       'addons/demo/tests', 'addons/demo/tests-extra', 'examples/demo']
        for system in ('linux', 'macos', 'windows'):
            for directory in directories:
                with self.subTest(system=system, directory=directory):
                    directory = f'{system}/{directory}'
                    (self.root / directory / 'src').mkdir(parents=True)
                    name = Path(directory).name
                    filename = name + '.exe' if system == 'windows' else name
                    self.executable(f'{directory}/build-egl/{filename}')
                    self.executable(f'{directory}/build-{system}/{filename}')
                    relative = (f'{name}.app/Contents/MacOS/{name}' if system == 'macos'
                                else filename)
                    expected = self.executable(f'{directory}/bin/{relative}')
                    self.assertEqual(self.find(directory, platform_info(system)), str(expected))

    def test_multi_config_uses_release_only(self):
        for system, generator, filename in [('windows', 'Visual Studio 17 2022', 'probe.exe'),
                                            ('windows', 'Ninja Multi-Config', 'probe.exe'),
                                            ('macos', 'Xcode', 'probe')]:
            with self.subTest(generator=generator):
                directory = f'{generator}/probe'
                self.executable(f'{directory}/build-{system}/Debug/{filename}')
                self.executable(f'{directory}/build-{system}/{filename}')
                expected = self.executable(f'{directory}/build-{system}/Release/{filename}')
                self.assertEqual(self.find(directory, platform_info(system, generator)), str(expected))
                expected.unlink()
                with redirect_stdout(io.StringIO()) as output:
                    self.assertIsNone(self.find(directory, platform_info(system, generator)))
                self.assertIn(str(expected), output.getvalue())

    def test_multi_config_from_cache_uses_release_only_without_explicit_generator(self):
        for system, generator, filename in [('linux', 'Ninja Multi-Config', 'probe'),
                                            ('macos', 'Xcode', 'probe'),
                                            ('windows', 'Visual Studio 17 2022', 'probe.exe')]:
            with self.subTest(generator=generator):
                directory = f'{system}/probe'
                self.cache(directory, system, f'CMAKE_GENERATOR:INTERNAL={generator}\n')
                self.executable(f'{directory}/build-{system}/{filename}')
                self.executable(f'{directory}/build-{system}/Debug/{filename}')
                self.executable(f'{directory}/build-stale/Release/{filename}')
                expected = self.executable(f'{directory}/build-{system}/Release/{filename}')
                self.assertEqual(self.find(directory, platform_info(system)), str(expected))
                expected.unlink()
                with redirect_stdout(io.StringIO()) as output:
                    self.assertIsNone(self.find(directory, platform_info(system)))
                self.assertIn(str(expected), output.getvalue())
                self.assertNotIn('build-stale', output.getvalue())

    def test_single_config_cache_overrides_requested_and_environment_generators(self):
        os.environ['CMAKE_GENERATOR'] = 'Ninja Multi-Config'
        for generator in ('Unix Makefiles', 'Ninja'):
            with self.subTest(generator=generator):
                directory = f'{generator}/probe'
                self.cache(directory, 'linux', f'CMAKE_GENERATOR:INTERNAL={generator}\n')
                self.executable(f'{directory}/build-linux/Release/probe')
                expected = self.executable(f'{directory}/build-linux/probe')
                for requested in (None, 'Xcode'):
                    self.assertEqual(self.find(directory, platform_info('linux', requested)), str(expected))

    def test_configuration_types_cache_entry_selects_release(self):
        self.cache('probe', 'linux', 'CMAKE_CONFIGURATION_TYPES:STRING=Debug;Release;RelWithDebInfo\n')
        self.executable('probe/build-linux/probe')
        expected = self.executable('probe/build-linux/Release/probe')
        self.assertEqual(self.find('probe', platform_info('linux', 'Ninja')), str(expected))

    def test_generator_fallback_order_without_cached_generator(self):
        os.environ['CMAKE_GENERATOR'] = 'Ninja Multi-Config'
        release = self.executable('probe/build-linux/Release/probe')
        root = self.executable('probe/build-linux/probe')
        for contents in (None, 'CMAKE_BUILD_TYPE:STRING=Release\n'):
            with self.subTest(cache=contents):
                if contents is not None:
                    self.cache('probe', 'linux', contents)
                self.assertEqual(self.find('probe', platform_info('linux')), str(release))
                self.assertEqual(self.find('probe', platform_info('linux', 'Ninja')), str(root))
        self.cache('probe', 'linux', 'CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n')
        self.assertEqual(self.find('probe', platform_info('linux', 'Ninja')), str(release))

    def test_sibling_cache_does_not_select_configuration(self):
        (self.root / 'probe/build-stale').mkdir(parents=True)
        (self.root / 'probe/build-stale/CMakeCache.txt').write_text(
            'CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n', encoding='utf-8')
        self.executable('probe/build-stale/Release/probe')
        self.executable('probe/build-linux/Release/probe')
        expected = self.executable('probe/build-linux/probe')
        self.assertEqual(self.find('probe', platform_info('linux')), str(expected))

    def test_shader_ordering_reads_cache_from_build_tree_root(self):
        self.cache('shaderTargetOrdering', 'linux', 'CMAKE_GENERATOR:INTERNAL=Ninja Multi-Config\n')
        self.executable('shaderTargetOrdering/build-linux/consumer/shaderTargetOrdering')
        expected = self.executable('shaderTargetOrdering/build-linux/consumer/Release/shaderTargetOrdering')
        self.assertEqual(self.find('shaderTargetOrdering', platform_info('linux')), str(expected))

    def test_debug_suffix_fallback_and_deterministic_preference(self):
        for system, relative in [('linux', 'probe_debug'),
                                 ('macos', 'probe_debug.app/Contents/MacOS/probe_debug')]:
            with self.subTest(system=system):
                directory = f'{system}/probe'
                (self.root / directory / 'src').mkdir(parents=True)
                debug = self.executable(f'{directory}/bin/{relative}')
                self.assertEqual(self.find(directory, platform_info(system)), str(debug))
                relative = 'probe.app/Contents/MacOS/probe' if system == 'macos' else 'probe'
                normal = self.executable(f'{directory}/bin/{relative}')
                self.assertEqual(self.find(directory, platform_info(system)), str(normal))

    def test_shader_ordering_fixture_uses_its_exact_consumer_subdirectory(self):
        for system, generator, relative in [('linux', None, 'shaderTargetOrdering'),
                                            ('macos', None, 'shaderTargetOrdering'),
                                            ('windows', 'Visual Studio 17 2022',
                                             'Release/shaderTargetOrdering.exe')]:
            with self.subTest(system=system):
                directory = f'{system}/shaderTargetOrdering'
                self.executable(f'{directory}/build-egl/consumer/{relative}')
                self.executable(f'{directory}/build-{system}/{relative}')
                expected = self.executable(f'{directory}/build-{system}/consumer/{relative}')
                self.assertEqual(self.find(directory, platform_info(system, generator)), str(expected))

    def test_missing_project_binary_reports_exact_bundle_or_exe_path(self):
        for system, relative in [('linux', 'probe'), ('windows', 'probe.exe'),
                                 ('macos', 'probe.app/Contents/MacOS/probe')]:
            with self.subTest(system=system):
                directory = f'{system}/probe'
                (self.root / directory / 'src').mkdir(parents=True)
                with redirect_stdout(io.StringIO()) as output:
                    self.assertIsNone(self.find(directory, platform_info(system)))
                self.assertIn('Test binary not found', output.getvalue())
                self.assertIn(str(self.root / directory / 'bin' / relative), output.getvalue())

    def test_rejects_nested_binaries_and_directories(self):
        self.executable('probe/build-linux/nested/probe')
        self.executable('probe/build-linux/consumer/probe')
        (self.root / 'probe/build-linux/probe').mkdir()
        with redirect_stdout(io.StringIO()):
            self.assertIsNone(self.find('probe', platform_info('linux')))

    @unittest.skipIf(os.name == 'nt', 'POSIX executable permission check')
    def test_requires_executable_permission_except_on_windows(self):
        self.executable('probe/build-linux/probe', executable=False)
        with redirect_stdout(io.StringIO()):
            self.assertIsNone(self.find('probe', platform_info('linux')))
        expected = self.executable('probe/build-windows/probe.exe', executable=False)
        self.assertEqual(self.find('probe', platform_info('windows')), str(expected))


if __name__ == '__main__':
    unittest.main()
