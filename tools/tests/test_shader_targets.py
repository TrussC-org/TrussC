"""Offline shader target contract checks (#820)."""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]


class ShaderTargetTests(unittest.TestCase):
    def test_hot_reload_links_shader_requirements_to_both_consumers(self):
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text()
        for consumer in ('${PROJECT_NAME}', 'guest'):
            self.assertIn(f'target_link_libraries({consumer} PRIVATE '
                          'TrussC_shaders ${PROJECT_NAME}_shaders)', source)
            self.assertIn(f'target_link_libraries({consumer} PRIVATE '
                          '${_TC_SHADER_ADDON}_shaders)', source)
        self.assertIn('foreach(_TC_SHADER_ADDON IN LISTS _TC_LOADED_ADDONS)', source)
        self.assertNotIn('add_dependencies(guest TrussC_shaders)', source)
        self.assertNotIn('"${TRUSSC_DIR}/include"', source)

    def test_core_and_addons_publish_shader_requirements(self):
        for filename, statement in (
            ('core/CMakeLists.txt', 'target_link_libraries(TrussC PUBLIC TrussC_shaders)'),
            ('addons/tcxHap/CMakeLists.txt',
             'target_link_libraries(${ADDON_NAME} PUBLIC ${ADDON_NAME}_shaders)'),
            ('addons/tcxLut/CMakeLists.txt',
             'target_link_libraries(${ADDON_NAME} INTERFACE ${ADDON_NAME}_shaders)'),
        ):
            with self.subTest(filename=filename):
                self.assertIn(statement, (ROOT / filename).read_text())


if __name__ == '__main__':
    unittest.main()
