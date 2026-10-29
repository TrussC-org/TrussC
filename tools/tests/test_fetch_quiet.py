"""Configure the shared FetchContent helper with caller diagnostic states."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from check_build_warnings import run_build


class FetchQuietTests(unittest.TestCase):
    def test_caller_state_and_dependency_warning(self):
        for state in ('absent', 'cache', 'normal', 'shadow', 'uninitialized'):
            with self.subTest(state=state), tempfile.TemporaryDirectory() as tmp:
                src = Path(tmp)
                dep = src / 'dep'
                dep.mkdir()
                (dep / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.5)
project(old_dependency NONE)
''', encoding='utf-8')
                setup = {
                    'absent': '',
                    'cache': 'set(CMAKE_WARN_DEPRECATED ON CACHE STRING "user help")',
                    'normal': 'set(CMAKE_WARN_DEPRECATED ON)',
                    'shadow': 'set(CMAKE_WARN_DEPRECATED OFF CACHE BOOL "user help")\nset(CMAKE_WARN_DEPRECATED ON)',
                    'uninitialized': 'set(CMAKE_WARN_DEPRECATED ON CACHE UNINITIALIZED "user help")',
                }[state]
                cached = state in ('cache', 'shadow', 'uninitialized')
                check = '''
if(NOT CMAKE_WARN_DEPRECATED STREQUAL "ON")
    message(FATAL_ERROR "normal or visible cache value changed")
endif()
''' if state != 'absent' else '''
if(DEFINED CMAKE_WARN_DEPRECATED)
    message(FATAL_ERROR "normal binding leaked")
endif()
'''
                if cached:
                    value = 'OFF' if state == 'shadow' else 'ON'
                    kind = {'cache': 'STRING', 'shadow': 'BOOL', 'uninitialized': 'UNINITIALIZED'}[state]
                    check += f'''
get_property(value CACHE CMAKE_WARN_DEPRECATED PROPERTY VALUE)
get_property(kind CACHE CMAKE_WARN_DEPRECATED PROPERTY TYPE)
get_property(help CACHE CMAKE_WARN_DEPRECATED PROPERTY HELPSTRING)
if(NOT value STREQUAL "{value}" OR NOT kind STREQUAL "{kind}" OR NOT help STREQUAL "user help")
    message(FATAL_ERROR "cache state changed")
endif()
'''
                else:
                    check += '''
if(DEFINED CACHE{CMAKE_WARN_DEPRECATED})
    message(FATAL_ERROR "cache entry leaked")
endif()
'''
                # Unsetting a caller normal binding must reveal its original cache.
                check += 'unset(CMAKE_WARN_DEPRECATED)\n'
                if not cached:
                    check += '''
if(DEFINED CMAKE_WARN_DEPRECATED)
    message(FATAL_ERROR "hidden cache entry leaked")
endif()
'''
                (src / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(fetch_quiet_probe NONE)
if(POLICY CMP0218)
    cmake_policy(SET CMP0218 NEW)
endif()
# CMake 4.4 initializes an INTERNAL entry; explicitly exercise absence too.
unset(CMAKE_WARN_DEPRECATED CACHE)
{setup}
include("{ROOT}/core/cmake/tc_fetch_quiet.cmake")
FetchContent_Declare(old_dependency SOURCE_DIR "{dep}")
tc_fetch_quiet(old_dependency)
{check}
''', encoding='utf-8')
                result = run_build([os.environ.get('TC_TEST_CMAKE', 'cmake'), '-S', str(src),
                                    '-B', str(src / 'build')], stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True, encoding='utf-8')
                self.assertEqual(result.returncode, 0, result.stdout)
                self.assertNotIn('Warning', result.stdout)
                self.assertNotIn('compatibility', result.stdout.lower())


if __name__ == '__main__':
    unittest.main()
