"""Offline shader target contract checks (#820)."""
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

import sys

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from check_build_warnings import run_build


class ShaderTargetTests(unittest.TestCase):
    def test_windows_guest_imports_do_not_depend_on_host_target(self):
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding="utf-8")
        self.assertNotIn('target_link_libraries(guest PRIVATE ${PROJECT_NAME})', source)
        self.assertNotIn('add_dependencies(guest ${PROJECT_NAME})', source)
        self.assertIn('add_dependencies(${PROJECT_NAME} guest)', source)
        self.assertIn('add_dependencies(guest _tc_host_import)', source)
        self.assertIn('target_link_libraries(guest PRIVATE "${_TC_HOST_IMPORT_LIB}")', source)
        command = source[source.index('set(_TC_HOST_IMPORT_LIB '):
                         source.index('add_custom_target(_tc_host_import ')]
        for setting in ('/trussc_host_import_$<CONFIG>.lib', '/DEF:${_TC_HOST_IMPORT_DEF}',
                        '/MACHINE:${CMAKE_CXX_COMPILER_ARCHITECTURE_ID}',
                        'DEPENDS _tc_gen_exports "${_TC_DEF_FILE}"'):
            self.assertIn(setting, command)
        self.assertNotIn('/IGNORE:4102', source)
        # TARGET_FILE_NAME has no target dependency (CMP0112 NEW, CMake 3.20).
        # TARGET_FILE / TARGET_LINKER_FILE would reintroduce one.
        self.assertNotIn('$<TARGET_FILE:', command)
        self.assertNotIn('$<TARGET_LINKER_FILE:', command)

    def test_windows_export_definition_excludes_deleting_destructors(self):
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding="utf-8")
        block = source[source.index('            file(WRITE "${_TC_DEF_SCRIPT}"'):
                       source.index('            # Detect the path to dumpbin.exe')]
        # Generate and run the production script, including both CMake quoting
        # layers, with a fake dumpbin public-symbol listing. Ordinary destructors,
        # vtables and similar-looking names must remain available to guests.
        symbols = ['??_GWidget@@UEAAPEAXI@Z', '??_EWidget@@UEAAPEAXI@Z',
                   '??_GOther@@QEAAPEAXI@Z', '??_EOther@@QEAAPEAXI@Z',
                   '??1Widget@@UEAA@XZ', '??_7Widget@@6B@', '?update@Widget@@QEAAXXZ',
                   'core_function', 'core_function', '??_FWidget@@QEAAXXZ',
                   '?_Ghelper@@YAXXZ', 'prefix??_EWidget', '__imp_core_function']
        expected = symbols[4:8] + symbols[9:12]
        with tempfile.TemporaryDirectory(prefix='tc exports ') as temporary:
            fixture = Path(temporary)
            (fixture / 'dumpbin.txt').write_text(
                'Archive member name at 1234: object.obj\n\n public symbols\n' +
                ''.join(f'  {index + 0xABCDEF:X} {symbol}\n'
                        for index, symbol in enumerate(symbols)), encoding="utf-8")
            (fixture / 'dumpbin.cmake').write_text(
                'execute_process(COMMAND "${CMAKE_COMMAND}" -E cat '
                '"${CMAKE_CURRENT_LIST_DIR}/dumpbin.txt")\n', encoding="utf-8")
            (fixture / 'generate.cmake').write_text('''
set(_TC_DEF_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/exports.cmake")
set(LIB_FILE "unused.lib")
set(DEF_FILE "${CMAKE_CURRENT_LIST_DIR}/exports.def")
set(DUMPBIN "${CMAKE_COMMAND};-P;${CMAKE_CURRENT_LIST_DIR}/dumpbin.cmake")
''' + block + '\ninclude("${_TC_DEF_SCRIPT}")\n', encoding="utf-8")
            run_build(['cmake', '-P', str(fixture / 'generate.cmake')], check=True,
                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            self.assertEqual((fixture / 'exports.def').read_text(encoding="utf-8"),
                             'EXPORTS\n' + ''.join(f'    {symbol}\n' for symbol in expected))

    def check_import_definition(self, config, librarian=None, readobj=None):
        # Execute the actual CMake block, including its generated script. The
        # exporting executable is deliberately never compiled or linked.
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding="utf-8")
        block = source[source.index('            set(_TC_HOST_IMPORT_LIB '):
                       source.index('            add_custom_target(_tc_host_import ')]
        with tempfile.TemporaryDirectory(prefix='tc imports ') as temporary:
            fixture = Path(temporary)
            build = fixture / 'build with spaces'
            (fixture / 'main.c').write_text('int main(void) { return 0; }\n', encoding="utf-8")
            (fixture / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.20)
project(import_probe C)
add_executable(import_probe main.c)
set_target_properties(import_probe PROPERTIES
    OUTPUT_NAME "renamed host" DEBUG_POSTFIX "_debug" SUFFIX ".exe")
set(_TC_DEF_FILE "${CMAKE_BINARY_DIR}/core.def")
file(WRITE "${_TC_DEF_FILE}" "EXPORTS\\n core_function\\n core_data DATA\\n")
add_custom_target(_tc_gen_exports)
set(CMAKE_CXX_COMPILER_ARCHITECTURE_ID x64)
''' + block, encoding="utf-8")
            def run(*command):
                runner = subprocess.run if command[0] == readobj else run_build
                return runner(command, check=True, text=True,
                                      stdout=subprocess.PIPE,
                                      stderr=subprocess.STDOUT, encoding="utf-8", errors="replace").stdout

            run('cmake', '-S', str(fixture), '-B', str(build),
                f'-DCMAKE_BUILD_TYPE={config}')
            run('cmake', '-P', str(build / f'_tc_host_import_{config}.cmake'))
            definition = build / f'trussc_host_import_{config}.def'
            module = 'renamed host' + ('_debug' if config == 'Debug' else '') + '.exe'
            self.assertEqual(definition.read_text(encoding="utf-8"),
                             f'NAME "{module}"\nEXPORTS\n core_function\n core_data DATA\n')
            if librarian:
                for arch in ('x64', 'ARM64'):
                    with self.subTest(config=config, arch=arch):
                        library = build / f'imports_{arch}.lib'
                        run(librarian, '/NOLOGO', f'/DEF:{definition}',
                            f'/OUT:{library}', f'/MACHINE:{arch}')
                        records = run(readobj, '--coff-imports', str(library))
                        # For short COFF import objects, readobj displays the
                        # imported module as File. Check every import record,
                        # including DATA, rather than just archive strings.
                        imports = re.findall(r'File: ([^\n]+)\nFormat: COFF-import-file-',
                                             records)
                        self.assertEqual(imports, [module, module], records)
                        self.assertIn('Export name: core_function', records)
                        self.assertIn('Type: data', records)
                        self.assertIn('Export name: core_data', records)
            self.assertFalse(any(build.rglob('renamed host*.exe')))

    def test_generated_import_definition_names_host(self):
        for config in ('Debug', 'Release'):
            with self.subTest(config=config):
                self.check_import_definition(config)

    def test_llvm_import_records_name_host(self):
        def find_llvm_tool(name):
            return next((path for candidate in
                         [name] + [f'{name}-{version}' for version in range(24, 13, -1)]
                         if (path := shutil.which(candidate))), None)

        librarian = find_llvm_tool('llvm-lib')
        readobj = find_llvm_tool('llvm-readobj')
        if not librarian or not readobj:
            self.skipTest('llvm-lib and llvm-readobj are required for COFF import inspection')
        for config in ('Debug', 'Release'):
            with self.subTest(config=config):
                self.check_import_definition(config, librarian, readobj)

    def test_hot_reload_links_shader_requirements_to_both_consumers(self):
        source = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding="utf-8")
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
                self.assertIn(statement, (ROOT / filename).read_text(encoding="utf-8"))

    def test_msvc_ninja_objects_depend_on_generated_headers(self):
        shaders = (ROOT / 'core/cmake/trussc_shaders.cmake').read_text(encoding="utf-8")
        self.assertIn('function(trussc_shader_object_depends _TC_CONSUMER)', shaders)
        self.assertIn('PROPERTY TC_SHADER_HEADERS ${_TC_SHADER_OUTPUTS}', shaders)
        self.assertIn('APPEND PROPERTY OBJECT_DEPENDS ${_TC_HEADERS}', shaders)
        app = (ROOT / 'core/cmake/trussc_app.cmake').read_text(encoding="utf-8")
        for consumer in ('${PROJECT_NAME}', 'guest'):
            self.assertIn(f'trussc_shader_object_depends({consumer} ${{_TC_APP_SHADER_TARGETS}})', app)
        # Hot reload: the host executable never gets them (a guest build on
        # Windows would relink the running host).
        host_call = app.index('trussc_shader_object_depends(${PROJECT_NAME}')
        self.assertIn('if(NOT TARGET guest)', app[host_call - 400:host_call])
        for filename, statement in (
            ('core/CMakeLists.txt', 'trussc_shader_object_depends(TrussC TrussC_shaders)'),
            ('core/cmake/use_addon.cmake',
             'trussc_shader_object_depends(${_ADDON_NAME} ${_ADDON_NAME}_shaders)'),
        ):
            with self.subTest(filename=filename):
                self.assertIn(statement, (ROOT / filename).read_text(encoding="utf-8"))
        # Addon authors write only the link line; the loader adds the rest.
        for addon in sorted((ROOT / 'addons').glob('*/CMakeLists.txt')):
            with self.subTest(addon=addon.parent.name):
                self.assertNotIn('trussc_shader_object_depends', addon.read_text(encoding="utf-8"))


if __name__ == '__main__':
    unittest.main()
