"""Offline CMake checks for dependency-only MSVC warning suppression."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_build_warnings import ALLOWLIST, load_allowlist, scan


class DailyDependencyWarningTests(unittest.TestCase):
    def test_msvc_suppression_stays_on_fetched_targets(self):
        # Execute the real addon blocks with tiny local targets. Simulating the
        # MSVC variable checks CMake scope on Linux without invoking cl.exe.
        if shutil.which('ninja'):
            generator = 'Ninja'
        elif shutil.which('make') and sys.platform != 'win32':
            generator = 'Unix Makefiles'
        else:
            self.skipTest('Ninja or Unix Makefiles is required for compile_commands.json')
        for addon, dependency in (('tcxMidi', 'libremidi'), ('tcxHap', 'snappy')):
            source = (ROOT / 'addons' / addon / 'CMakeLists.txt').read_text(encoding='utf-8')
            start = source.index('if(MSVC)')
            block = source[start:source.index('endif()', start) + len('endif()')]
            for msvc in ('ON', 'OFF'):
                with self.subTest(addon=addon, msvc=msvc), tempfile.TemporaryDirectory(
                        prefix='dependency-warnings-') as tmp:
                    fixture = Path(tmp)
                    (fixture / 'probe.cpp').write_text('int probe() { return 0; }\n', encoding='utf-8')
                    (fixture / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(dependency_warning_probe CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(MSVC {msvc})
add_library({dependency} STATIC probe.cpp)
add_library(TrussC STATIC probe.cpp)
add_library({addon} STATIC probe.cpp)
target_link_libraries({addon} PUBLIC TrussC {dependency})
add_library(example STATIC probe.cpp)
target_link_libraries(example PRIVATE {addon})
{block}
''', encoding='utf-8')
                    result = subprocess.run(
                        [os.environ.get('TC_TEST_CMAKE', 'cmake'), '-S', str(fixture),
                         '-B', str(fixture / 'build'), '-G', generator,
                         '-DFETCHCONTENT_FULLY_DISCONNECTED=ON', '--no-warn-unused-cli'],
                        capture_output=True, text=True, encoding='utf-8')
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    commands = json.loads((fixture / 'build/compile_commands.json').read_text(
                        encoding='utf-8'))
                    self.assertEqual(len(commands), 4)
                    for command in commands:
                        is_dependency = f'CMakeFiles/{dependency}.dir/' in command['command'].replace('\\', '/')
                        self.assertEqual(' /w ' in command['command'],
                                         msvc == 'ON' and is_dependency, command['command'])

    def test_gate_rejects_addon_and_vendor_diagnostics(self):
        # Fetched paths remain subject to the gate: quiet compiler output must
        # come from the target options, never from a path/allowlist exception.
        lines = [
            r'addons\tcxMidi\src\tcxMidi.h(42): warning C4101: unused variable',
            r'addons\tcxHap\src\tcxHapPlayer.cpp(42): warning C4244: conversion',
            r'addons\tcxMidi\example-basic\src\tcApp.cpp(42): warning C4701: uninitialized',
            r'.fetchcontent\libremidi-src\include\libremidi\cmidi2.hpp(42): warning C4702: unreachable',
            r'.fetchcontent\snappy-src\snappy.cc(42): warning C4267: conversion',
        ]
        self.assertEqual(len(scan(lines, load_allowlist(ALLOWLIST))), len(lines))


if __name__ == '__main__':
    unittest.main()
