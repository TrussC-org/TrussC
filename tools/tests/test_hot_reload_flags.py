"""Generate real guest/PCH commands and check configuration flag isolation."""
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from check_build_warnings import run_build

ROOT = Path(__file__).resolve().parents[2]


class HotReloadFlagsTests(unittest.TestCase):
    def test_guest_configuration_flags(self):
        if not shutil.which('cmake'):
            self.skipTest('CMake is required')
        for msvc in (False, True):
            for config in ('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel', 'Custom'):
                with self.subTest(msvc=msvc, config=config), tempfile.TemporaryDirectory() as tmp:
                    src = Path(tmp)
                    (src / 'probe.cpp').write_text('int probe() { return 42; }\n', encoding='utf-8')
                    (src / 'probe.c').write_text('int cprobe(void) { return 42; }\n', encoding='utf-8')
                    (src / 'custom.cpp').write_text('''
#if !defined(LOCAL_DEFINITION) || !defined(LOCAL_OPTION)
#error missing app directory source properties
#endif
int customized() { return 1; }
''', encoding='utf-8')
                    (src / 'pch.h').write_text('#pragma once\n', encoding='utf-8')
                    (src / 'core').mkdir()
                    (src / 'core/CMakeLists.txt').write_text(
                        'add_library(core OBJECT ../probe.c)\n', encoding='utf-8')
                    base = '/O1 /Ox /O2' if msvc else '-O1 -Ofast -O3'
                    configured = '/O2 /Zi' if msvc else '-Os -O2 -g'
                    (src / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(flag_probe C CXX)
set(MSVC {'TRUE' if msvc else 'FALSE'})
foreach(lang C CXX)
    set(CMAKE_${{lang}}_FLAGS "{base} -DKEEP_O2=1")
    set(CMAKE_${{lang}}_FLAGS_{config.upper()} "{configured} -DNDEBUG")
endforeach()
add_library(host OBJECT probe.cpp)
add_subdirectory(core)
add_library(guest SHARED probe.cpp probe.c custom.cpp)
include("{ROOT / 'core/cmake/tc_hot_reload_flags.cmake'}")
cmake_language(DEFER CALL tc_hot_reload_flags)
target_compile_options(guest PRIVATE {'/Od /Z7' if msvc else '-O0'})
target_precompile_headers(guest PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${{CMAKE_CURRENT_SOURCE_DIR}}/pch.h>")
# Same timing and directory scope as an app's local.cmake.
set_source_files_properties(custom.cpp PROPERTIES
    COMPILE_DEFINITIONS LOCAL_DEFINITION=1
    COMPILE_OPTIONS -DLOCAL_OPTION=1
    SKIP_PRECOMPILE_HEADERS ON)
''', encoding='utf-8')
                    output = run_build(['cmake', '-S', str(src), '-B', str(src / 'build'),
                                        f'-DCMAKE_BUILD_TYPE={config}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],
                                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True).stdout
                    self.assertNotIn('Warning', output)
                    commands = json.loads((src / 'build/compile_commands.json').read_text(encoding='utf-8'))
                    guest = [c for c in commands if 'guest.dir' in c['command']]
                    others = [c for c in commands if c not in guest]
                    self.assertEqual(len(guest), 4)  # C, two C++, and C++ PCH
                    self.assertEqual(len(others), 2)
                    optimization = r'(?<!\S)(?:[-/]O[12xd]|-O(?:[0-9]+|g|s|z|fast)?)(?!\S)'
                    for entry in guest:
                        flags = re.findall(optimization, entry['command'])
                        self.assertEqual(flags, ['/Od'] if msvc else ['-O0'], entry)
                        self.assertIn('-DKEEP_O2=1', entry['command'])
                        self.assertIn('-DNDEBUG', entry['command'])
                        if msvc:
                            self.assertNotIn('/Zi', entry['command'])
                            self.assertIn('/Z7', entry['command'])
                    for entry in others:
                        self.assertEqual(re.findall(optimization, entry['command']),
                                         re.findall(optimization, base + ' ' + configured), entry)
                        self.assertIn('-DKEEP_O2=1', entry['command'])
                        self.assertIn('-DNDEBUG', entry['command'])
                        if msvc:
                            self.assertIn('/Zi', entry['command'])
                    custom = next(c for c in guest if c['file'].endswith('custom.cpp'))
                    self.assertIn('-DLOCAL_DEFINITION=1', custom['command'])
                    self.assertIn('-DLOCAL_OPTION=1', custom['command'])
                    self.assertNotIn('cmake_pch', custom['command'])
                    ordinary = next(c for c in guest if c['file'].endswith('probe.cpp'))
                    self.assertIn('cmake_pch', ordinary['command'])
                    if not msvc:
                        run_build(['cmake', '--build', str(src / 'build'), '--target', 'guest', '-j2'],
                                  check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


if __name__ == '__main__':
    unittest.main()
