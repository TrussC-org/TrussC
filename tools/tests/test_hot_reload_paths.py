"""Run the production source collection and generated check from another cwd."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools'))
from check_build_warnings import run_build


class HotReloadPathTests(unittest.TestCase):
    def test_generated_check_paths_and_file_sets(self):
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding='utf-8')
        # Exercise the actual writer in the macro, without building the framework.
        block = source[source.index('    # Source files (auto-collect'):
                       source.index('    # Clean up old guest libraries')]
        # Relative paths require the same drive as the checkout on Windows.
        scratch = ROOT / '.build'
        scratch.mkdir(exist_ok=True)
        for spelling in ('backslashes', 'relative'):
            for layout in ('nested', 'explicit', 'explicit_without_macro'):
                with self.subTest(spelling=spelling, layout=layout), tempfile.TemporaryDirectory(
                        dir=scratch, prefix='tc paths $literal ') as temporary:
                    project = Path(temporary).resolve()
                    src = project / 'src/app'
                    src.mkdir(parents=True)
                    main = project / 'src/main.cpp'
                    main.write_text('int main() { return 0; }\n', encoding='utf-8')
                    app = src / 'tcApp.cpp'
                    app.write_text('TC_HOT_RELOAD(tcApp)\n', encoding='utf-8')
                    explicit = ('set(_TC_APP_SOURCES src/main.cpp src\\\\app\\\\tcApp.cpp)'
                                if layout == 'explicit' else
                                'set(_TC_APP_SOURCES src/main.cpp)' if layout == 'explicit_without_macro' else '')
                    core = str(ROOT / 'core').replace('\\', '/')
                    raw_core = (core.replace('/', '\\') if spelling == 'backslashes' else
                                os.path.relpath(ROOT / 'core', project).replace('\\', '/'))
                    (project / 'CMakeLists.txt').write_text(f'''
cmake_minimum_required(VERSION 3.20)
project(path_probe NONE)
# core/CMakeLists.txt supplies the normalized absolute TC_ROOT in a real app.
set(TC_ROOT [==[{ROOT.as_posix()}]==])
set(TRUSSC_DIR [==[{raw_core}]==])
{explicit}
{block}
file(WRITE "${{CMAKE_BINARY_DIR}}/verdict.txt" "${{_TC_HOT_RELOAD}}")
''', encoding='utf-8')
                    build = project / 'build'
                    other = project / 'other-cwd'
                    other.mkdir()
                    # Configure from outside the app as well as executing -P there.
                    run_build(['cmake', '-S', str(project), '-B', str(build)], cwd=other, check=True)
                    expected = 'OFF' if layout == 'explicit_without_macro' else 'ON'
                    self.assertEqual((build / 'verdict.txt').read_text(), expected)
                    script = build / '_tc_check_hot_reload.cmake'
                    result = subprocess.run(['cmake', '-P', str(script)], cwd=other,
                                            capture_output=True, text=True)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    # The check must still detect a change, and honor explicit SOURCES.
                    changed = main if layout == 'explicit_without_macro' else app
                    changed.write_text('TC_HOT_RELOAD(tcApp)\n' if expected == 'OFF' else '// no macro\n',
                                       encoding='utf-8')
                    result = subprocess.run(['cmake', '-P', str(script)], cwd=other,
                                            capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('reconfigure required', result.stderr)


if __name__ == '__main__':
    unittest.main()
