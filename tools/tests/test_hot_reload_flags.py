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
                    (src / 'pch.h').write_text('#pragma once\n', encoding='utf-8')
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
add_library(core OBJECT probe.c)
set(_TC_GUEST_SOURCES probe.cpp probe.c)
add_subdirectory("{ROOT / 'core/cmake/hot_reload_guest'}" guest)
target_precompile_headers(guest PRIVATE "$<$<COMPILE_LANGUAGE:CXX>:${{CMAKE_CURRENT_SOURCE_DIR}}/pch.h>")
''', encoding='utf-8')
                    output = run_build(['cmake', '-S', str(src), '-B', str(src / 'build'),
                                        f'-DCMAKE_BUILD_TYPE={config}', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON'],
                                       check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                       text=True).stdout
                    self.assertNotIn('Warning', output)
                    commands = json.loads((src / 'build/compile_commands.json').read_text(encoding='utf-8'))
                    guest = [c for c in commands if 'guest.dir' in c['command']]
                    others = [c for c in commands if c not in guest]
                    self.assertEqual(len(guest), 3)  # C, C++, and C++ PCH
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
                        self.assertIn(base, entry['command'])
                        self.assertIn(configured, entry['command'])
                    if not msvc:
                        run_build(['cmake', '--build', str(src / 'build'), '--target', 'guest', '-j2'],
                                  check=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)


if __name__ == '__main__':
    unittest.main()
