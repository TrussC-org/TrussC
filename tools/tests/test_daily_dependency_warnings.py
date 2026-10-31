"""Offline CMake checks for dependency-only MSVC warning suppression."""
from pathlib import Path
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_build_warnings import ALLOWLIST, load_allowlist, scan


class DailyDependencyWarningTests(unittest.TestCase):
    def configure_probe(self, fixture, source):
        if shutil.which('ninja'):
            generator = 'Ninja'
        elif shutil.which('make') and sys.platform != 'win32':
            generator = 'Unix Makefiles'
        else:
            self.skipTest('Ninja or Unix Makefiles is required for compile_commands.json')
        (fixture / 'probe.cpp').write_text('int probe() { return 0; }\n', encoding='utf-8')
        (fixture / 'CMakeLists.txt').write_text(source, encoding='utf-8')
        result = subprocess.run(
            [os.environ.get('TC_TEST_CMAKE', 'cmake'), '-S', str(fixture),
             '-B', str(fixture / 'build'), '-G', generator,
             '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '--no-warn-unused-cli'],
            capture_output=True, text=True, encoding='utf-8')
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        return json.loads((fixture / 'build/compile_commands.json').read_text(encoding='utf-8'))

    def assert_no_warning_level_conflict(self, command):
        options = command.split()
        self.assertFalse('/W4' in options and any(flag in options for flag in ('/w', '/W0')),
                         command)

    def test_msvc_suppression_stays_on_fetched_targets(self):
        # Execute real addon settings with offline subprojects reproducing each
        # vendor's warning setup. No MSVC flags are passed to the Linux compiler.
        scratch = ROOT / '.build'
        scratch.mkdir(exist_ok=True)
        for addon, dependency in (('tcxMidi', 'libremidi'), ('tcxHap', 'snappy')):
            source = (ROOT / 'addons' / addon / 'CMakeLists.txt').read_text(encoding='utf-8')
            available = source.index(f'FetchContent_MakeAvailable({dependency})')
            start = source.index('if(MSVC)', available)
            block = source[start:source.index('endif()', start) + len('endif()')]
            settings = ''
            if dependency == 'libremidi':
                start = source.index('set(LIBREMIDI_HEADER_ONLY')
                settings = source[start:source.index('# URL (release tarball)', start)]
                self.assertLess(source.index('set(LIBREMIDI_NO_WARNINGS'), available)
            for msvc in ('ON', 'OFF'):
                for warning_scope in ('directory', 'target'):
                    with self.subTest(addon=addon, msvc=msvc, scope=warning_scope), \
                            tempfile.TemporaryDirectory(dir=scratch, prefix='dependency-warnings-') as tmp:
                        fixture = Path(tmp)
                        vendor = fixture / 'vendor'
                        vendor.mkdir()
                        # Same option declaration as libremidi v5.4.3; seed a
                        # stale OFF cache to check FORCE takes effect on reuse.
                        upstream_setup = '''
cmake_minimum_required(VERSION 3.22)
include(CMakeDependentOption)
option(LIBREMIDI_HEADER_ONLY "Header-only mode" OFF)
cmake_dependent_option(LIBREMIDI_NO_WARNINGS "Disables warnings from library compilation" OFF "NOT LIBREMIDI_HEADER_ONLY" ON)
''' if dependency == 'libremidi' else ''
                        upstream_warning = ('set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")'
                                            if warning_scope == 'directory' else
                                            f'target_compile_options({dependency} PRIVATE /W4)')
                        (vendor / 'CMakeLists.txt').write_text(f'''
{upstream_setup}
add_library({dependency} STATIC ../probe.cpp)
if(MSVC)
    if(NOT LIBREMIDI_NO_WARNINGS)
        {upstream_warning}
    endif()
endif()
''', encoding='utf-8')
                        commands = self.configure_probe(fixture, f'''
cmake_minimum_required(VERSION 3.22)
project(dependency_warning_probe CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(MSVC {msvc})
set(LIBREMIDI_NO_WARNINGS OFF CACHE BOOL "" FORCE)
{settings}
add_subdirectory(vendor)
add_library(TrussC STATIC probe.cpp)
add_library({addon} STATIC probe.cpp)
target_link_libraries({addon} PUBLIC TrussC {dependency})
add_library(example STATIC probe.cpp)
target_link_libraries(example PRIVATE {addon})
{block}
''')
                        self.assertEqual(len(commands), 4)
                        expected = {'/w'} if dependency == 'libremidi' else {
                            '/wd4244', '/wd4267', '/wd4245'}
                        for command in commands:
                            command = command['command'].replace('\\', '/')
                            is_dependency = f'CMakeFiles/{dependency}.dir/' in command
                            suppressions = set(re.findall(r'(?<!\S)/w(?:d[0-9]+)?(?!\S)|(?<!\S)/W0(?!\S)', command))
                            self.assertEqual(suppressions,
                                             expected if msvc == 'ON' and is_dependency else set(), command)
                            self.assert_no_warning_level_conflict(command)
                            self.assertEqual('/W4' in command.split(),
                                             msvc == 'ON' and is_dependency and dependency == 'snappy', command)

    def test_conflict_check_rejects_directory_and_target_warning_levels(self):
        scratch = ROOT / '.build'
        scratch.mkdir(exist_ok=True)
        for scope in ('directory', 'target'):
            for quiet in ('/w', '/W0'):
                with self.subTest(scope=scope, quiet=quiet), \
                        tempfile.TemporaryDirectory(dir=scratch, prefix='warning-conflict-') as tmp:
                    level = ('set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W4")'
                             if scope == 'directory' else 'target_compile_options(probe PRIVATE /W4)')
                    commands = self.configure_probe(Path(tmp), f'''
cmake_minimum_required(VERSION 3.20)
project(warning_conflict_probe CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_library(probe STATIC probe.cpp)
{level}
target_compile_options(probe PRIVATE {quiet})
''')
                    with self.assertRaises(AssertionError):
                        self.assert_no_warning_level_conflict(commands[0]['command'])

    def test_gate_rejects_addon_and_vendor_diagnostics(self):
        # Fetched paths remain subject to the gate: quiet compiler output must
        # come from the target options, never from a path/allowlist exception.
        lines = [
            r"cl : Command line warning D9025 : overriding '/W4' with '/w'",
            r"cl : Command line warning D9025 : overriding '/W4' with '/W0'",
            r'addons\tcxMidi\src\tcxMidi.h(42): warning C4101: unused variable',
            r'addons\tcxHap\src\tcxHapPlayer.cpp(42): warning C4244: conversion',
            r'addons\tcxMidi\example-basic\src\tcApp.cpp(42): warning C4701: uninitialized',
            r'.fetchcontent\libremidi-src\include\libremidi\cmidi2.hpp(42): warning C4702: unreachable',
            r'.fetchcontent\snappy-src\snappy.cc(42): warning C4267: conversion',
        ]
        self.assertEqual(len(scan(lines, load_allowlist(ALLOWLIST))), len(lines))


if __name__ == '__main__':
    unittest.main()
