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

    def test_msvc_ninja_objects_depend_on_generated_headers(self):
        shaders = (ROOT / 'core/cmake/trussc_shaders.cmake').read_text()
        self.assertIn('function(trussc_shader_object_depends _TC_CONSUMER)', shaders)
        self.assertIn('PROPERTY TC_SHADER_HEADERS ${_TC_SHADER_OUTPUTS}', shaders)
        self.assertIn('APPEND PROPERTY OBJECT_DEPENDS ${_TC_HEADERS}', shaders)
        app = (ROOT / 'core/cmake/trussc_app.cmake').read_text()
        for consumer in ('${PROJECT_NAME}', 'guest'):
            self.assertIn(f'trussc_shader_object_depends({consumer} ${{_TC_APP_SHADER_TARGETS}})', app)
        for filename, statement in (
            ('core/CMakeLists.txt', 'trussc_shader_object_depends(TrussC TrussC_shaders)'),
            ('core/cmake/use_addon.cmake',
             'trussc_shader_object_depends(${_ADDON_NAME} ${_ADDON_NAME}_shaders)'),
        ):
            with self.subTest(filename=filename):
                self.assertIn(statement, (ROOT / filename).read_text())
        # Addon authors write only the link line; the loader adds the rest.
        for addon in sorted((ROOT / 'addons').glob('*/CMakeLists.txt')):
            with self.subTest(addon=addon.parent.name):
                self.assertNotIn('trussc_shader_object_depends', addon.read_text())


if __name__ == '__main__':
    unittest.main()
