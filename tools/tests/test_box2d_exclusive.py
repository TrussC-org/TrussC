"""Configure-only addon loader checks; stubs never fetch or build Box2D."""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MESSAGE = 'tcxBox2d and tcxBox2d3 cannot be used together; choose one in addons.make.'


class Box2dExclusiveTests(unittest.TestCase):
    def configure(self, order, mode, conflict=True):
        with tempfile.TemporaryDirectory(dir=ROOT) as tmp:
            source = Path(tmp)
            (source / 'core').mkdir()
            addons = source / 'addons'
            for addon in order:
                directory = addons / addon
                directory.mkdir(parents=True, exist_ok=True)
                (directory / 'CMakeLists.txt').write_text(
                    f'add_library({addon} INTERFACE)\n', encoding='utf-8')
            first, second = order
            if mode in ('transitive', 'transitive-before-target'):
                # The outer addon is still loading (not yet in the loaded list).
                lines = [f'add_library({first} INTERFACE)\n', f'use_addon(consumer {second})\n']
                if mode == 'transitive-before-target':
                    lines.reverse()
                (addons / first / 'CMakeLists.txt').write_text(''.join(lines), encoding='utf-8')
                load = f'use_addon(consumer {first})\n'
            elif mode == 'hot-reload':
                load = f'_tc_load_addon({first})\n_tc_load_addon({second})\n'
            else:
                (source / 'addons.make').write_text(f'{first}\n{second}\n', encoding='utf-8')
                load = 'apply_addons(consumer)\n'
            if not conflict:
                load = f'use_addon(consumer {first})\nuse_addon(consumer {first})\n'
            (source / 'CMakeLists.txt').write_text(
                'cmake_minimum_required(VERSION 3.22)\nproject(exclusivity C)\n'
                f'set(TRUSSC_DIR "{source}/core")\n'
                f'include("{ROOT}/core/cmake/use_addon.cmake")\n'
                'file(WRITE "${CMAKE_BINARY_DIR}/dummy.c" "int dummy;\\n")\n'
                'add_library(consumer STATIC ${CMAKE_BINARY_DIR}/dummy.c)\n' + load, encoding='utf-8')
            command = ['cmake', '-S', str(source), '-B', str(source / 'build')]
            for attempt in range(2):  # repeat configure against the persisted cache
                result = subprocess.run(command, capture_output=True, text=True, encoding='utf-8')
                output = ' '.join((result.stdout + result.stderr).split())
                if conflict:
                    self.assertNotEqual(result.returncode, 0, output)
                    self.assertIn(MESSAGE, output)
                else:
                    self.assertEqual(result.returncode, 0, output)

    def test_conflict_both_orders_all_paths(self):
        for order in [('tcxBox2d', 'tcxBox2d3'), ('tcxBox2d3', 'tcxBox2d')]:
            for mode in ['addons.make', 'hot-reload', 'transitive', 'transitive-before-target']:
                with self.subTest(order=order, mode=mode):
                    self.configure(order, mode)

    def test_each_addon_can_be_loaded_repeatedly(self):
        for order in [('tcxBox2d', 'tcxBox2d3'), ('tcxBox2d3', 'tcxBox2d')]:
            with self.subTest(order=order):
                self.configure(order, 'addons.make', conflict=False)


if __name__ == '__main__':
    unittest.main()
