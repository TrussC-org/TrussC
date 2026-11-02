"""Offline parity checks for standalone and combined core-test recipes (#1036)."""
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools"))
from check_build_warnings import run_build
# Whole-target options need review even though the runner starts a fresh process
# per entry: linking/compiling an interceptor affects every entry in that binary.
# Keep exact operations here, not a blanket exemption for a test directory.
REVIEWED_TARGET_OPTIONS = {
    # HEAD's legacy define has no consumer in the merged production sources;
    # it enables no interception. Preserve the existing per-test recipe.
    ('eglWindowSurfaces', 'target_compile_definitions',
     ('TrussC', 'PRIVATE', 'SOKOL_TC_TEST_HOOKS')),
    # The GLES-only wrapper increments a private counter and unconditionally
    # forwards all four original arguments to __real_eglMakeCurrent. It never
    # changes the return value, binding or failure policy for another entry.
    ('eglWindowSurfaces', 'target_link_options',
     ('allCoreTests', 'PRIVATE', '-Wl,--wrap=eglMakeCurrent')),
    # mallocProbe.cpp starts unarmed (thread-local), observes/rejects only while
    # explicitly armed by audioDecodeBounds, and otherwise calls __real_malloc.
    ('audioDecodeBounds', 'target_link_options',
     ('allCoreTests', 'PRIVATE', '-Wl,--wrap=malloc')),
    # Enables C++ catching on Web; does not replace or intercept any function.
    ('saveContract', 'target_compile_options',
     ('allCoreTests', 'PRIVATE', '-fexceptions')),
    ('saveContract', 'target_link_options',
     ('allCoreTests', 'PRIVATE', '-fexceptions')),
}
spec = importlib.util.spec_from_file_location('build_all', ROOT / 'examples/build_all.py')
build_all = importlib.util.module_from_spec(spec)
spec.loader.exec_module(build_all)


@unittest.skipUnless(shutil.which('cmake'), 'requires CMake')
class CoreTestBuildStepsTests(unittest.TestCase):
    def setUp(self):
        self.scratch = tempfile.TemporaryDirectory(dir=ROOT)
        self.addCleanup(self.scratch.cleanup)
        self.scratch_root = Path(self.scratch.name)
        self.tests = ROOT / 'core/tests'
        self.combined = sorted(
            Path(test) for test in build_all.find_core_tests(ROOT, include_daily=True)
            if build_all.is_combined_core_test(test))
        self.counter = 0

    def configure(self, source_dir, recipe, selected=None, web=False, system=None):
        self.counter += 1
        fixture = self.scratch_root / str(self.counter)
        fixture.mkdir()
        (fixture / 'dummy.cpp').write_text('int main() { return 0; }\n', encoding='utf-8')
        platform = '' if system is None else f'''
set(CMAKE_SYSTEM_NAME "{system}")
set(APPLE {'TRUE' if system == 'Darwin' else 'FALSE'})
set(WIN32 {'TRUE' if system == 'Windows' else 'FALSE'})
set(UNIX {'FALSE' if system == 'Windows' else 'TRUE'})
set(LINUX {'TRUE' if system == 'Linux' else 'FALSE'})
set(ANDROID FALSE)
'''
        # Configure the real recipes against ordinary CMake targets. No core,
        # shader compiler, generated headers or graphics hardware are needed.
        (fixture / 'CMakeLists.txt').write_text('''
cmake_minimum_required(VERSION 3.20)
project(allCoreTests LANGUAGES CXX)
add_executable(allCoreTests dummy.cpp)
add_library(TrussC STATIC dummy.cpp)
function(trussc_compile_shaders name source_dir)
    add_library(${name}_shaders INTERFACE)
    file(APPEND "${CMAKE_BINARY_DIR}/shaders.txt" "${name}|${source_dir}\\n")
endfunction()
function(trussc_shader_object_depends consumer)
endfunction()
''' + f'''
set(TRUSSC_DIR "{(ROOT / 'core').as_posix()}")
set(CMAKE_CURRENT_SOURCE_DIR "{source_dir.as_posix()}")
set(EMSCRIPTEN {'TRUE' if web else 'FALSE'})
{platform}
include("{recipe.as_posix()}")
''', encoding='utf-8')
        command = ['cmake', '--trace-expand', '--trace-format=json-v1',
                   '-S', str(fixture), '-B', str(fixture / 'build')]
        if selected is not None:
            command.append('-DTC_CORE_TEST_SELECT=' + ';'.join(selected))
        records = []
        def observe(line):
            if line.startswith('{'):
                entry = json.loads(line)
                if 'cmd' in entry:
                    records.append(entry)
        result = run_build(command, capture_output=True, text=True, on_line=observe,
                           encoding='utf-8', errors='replace', timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout)
        shader_log = fixture / 'build/shaders.txt'
        shaders = []
        if shader_log.exists():
            for line in shader_log.read_text(encoding='utf-8').splitlines():
                name, directory = line.split('|', 1)
                shaders.append((name, Path(directory).resolve()))
        return records, shaders

    def assert_build_steps(self, selected=None, web=False):
        records, shaders = self.configure(
            self.tests / 'allCoreTests', self.tests / 'allCoreTests/local.cmake',
            selected=selected, web=web)
        tests = [test for test in self.combined
                 if selected is None or test.name in selected]
        # Use build_all.py's inventory as the oracle for both source selection
        # and automatic src/ shaders, including tests without local.cmake.
        self.assertEqual({name for name, _ in shaders if name in
                          {test.name for test in self.combined}},
                         {test.name for test in tests})
        source_args = {Path(arg).resolve() for entry in records
                       if entry['cmd'] == 'target_sources'
                       for argument in entry['args'][2:]
                       for arg in argument.split(';')}
        for test in tests:
            self.assertIn((test.name, (test / 'src').resolve()), shaders)
            for source in test.joinpath('src').rglob('*'):
                if source.suffix in ('.cpp', '.c'):
                    self.assertIn(source.resolve(), source_args)

        def operations(trace):
            # Compare evaluated build operations, not copied text: path scopes,
            # platform conditions, nested includes and source properties count.
            commands = {'target_sources', 'target_link_libraries',
                        'target_compile_definitions', 'target_compile_options',
                        'target_link_options', 'target_include_directories',
                        'set_property', 'set_source_files_properties'}
            return {(str(Path(entry['file']).resolve()), entry['cmd'], tuple(entry['args']))
                    for entry in trace if entry['cmd'] in commands
                    and Path(entry['file']).is_relative_to(self.tests)
                    and Path(entry['file']) != self.tests / 'allCoreTests/local.cmake'}

        expected = set()
        for test in tests:
            local = test / 'local.cmake'
            if local.exists():
                standalone, local_shaders = self.configure(test, local, web=web)
                expected.update(operations(standalone))
                for shader in local_shaders:
                    self.assertIn(shader, shaders)
        self.assertEqual(operations(records), expected)
        # The shared LUT/Hap outputs must have only one producer, even when
        # pipelineSelection and renderTargetState are selected together.
        producers = {}
        for name, directory in shaders:
            for source in Path(directory).rglob('*.glsl'):
                self.assertNotIn(source, producers,
                                 f'{source}: {name} duplicates {producers.get(source)}')
                producers[source] = name

    def test_all_combined_build_steps_match_standalone(self):
        self.assert_build_steps()

    def test_shards_keep_their_own_build_steps(self):
        for selected in (['pipelineSelection'], ['renderTargetState'],
                         ['pipelineSelection', 'renderTargetState'],
                         ['trusscliPresets', 'captureSubmission', 'audioDecodeBounds']):
            with self.subTest(selected=selected):
                self.assert_build_steps(selected)

    def test_web_conditional_steps_are_included(self):
        self.assert_build_steps(['asyncLoadsWeb', 'asyncScreenshotMicWeb', 'saveContract'], web=True)

    def test_combined_target_options_require_isolation_review(self):
        # Evaluate the full combined recipe (including nested includes and any
        # manual special cases in allCoreTests), on each recipe platform branch.
        # Unknown whole-target options/definitions must be isolated or reviewed.
        commands = {'target_link_options', 'target_compile_options',
                    'target_compile_definitions', 'add_link_options',
                    'add_compile_options', 'add_definitions', 'link_libraries'}
        for system, web in [('Linux', False), ('Darwin', False),
                            ('Windows', False), ('Emscripten', True)]:
            records, _ = self.configure(
                self.tests / 'allCoreTests', self.tests / 'allCoreTests/local.cmake',
                web=web, system=system)
            for entry in records:
                command, args = entry['cmd'], tuple(entry['args'])
                # Also reject property-based interception; ordinary source
                # properties (TC_CORE_TEST_NAME, private includes) are scoped.
                target_property = command in {'set_property', 'set_target_properties'} and (
                    command == 'set_target_properties' or args[0] in {'TARGET', 'DIRECTORY'})
                # Shader interface libraries only carry generated-header paths
                # and dependencies. Any other library may interpose symbols.
                nonshader_link = command == 'target_link_libraries' and any(
                    not arg.endswith('_shaders')
                    for argument in args[2:] for arg in argument.split(';'))
                global_flags = command == 'set' and args and (
                    args[0].startswith('CMAKE_') and 'FLAGS' in args[0])
                if (command not in commands and not target_property
                        and not nonshader_link and not global_flags):
                    continue
                recipe = Path(entry['file']).resolve()
                if not recipe.is_relative_to(self.tests):
                    continue
                owner = recipe.relative_to(self.tests).parts[0]
                self.assertIn((owner, command, args), REVIEWED_TARGET_OPTIONS,
                              f'{system}: {recipe}: unreviewed whole-target operation; '
                              'use own-binary or verify inactive forwarding')

    @unittest.skipUnless(sys.platform == 'linux' and shutil.which(os.environ.get('CXX', 'c++')),
                         'requires native Linux C++ compiler')
    def test_reviewed_malloc_wrapper_forwards_when_inactive(self):
        # Compile the real wrapper with a recording __real_malloc, so a missing
        # forwarding path fails without allocating gigabytes or building TrussC.
        source = self.scratch_root / 'probe.cpp'
        source.write_text('''
#include <cstddef>
void audioMallocProbeArm();
size_t audioMallocProbeDisarm();
extern "C" void* __wrap_malloc(size_t);
size_t requested;
int sentinel;
extern "C" void* __real_malloc(size_t bytes) { requested = bytes; return &sentinel; }
int main() {
    constexpr size_t large = 2 * 1024 * 1024;
    if (__wrap_malloc(large) != &sentinel || requested != large) return 1;
    audioMallocProbeArm();
    requested = 0;
    if (__wrap_malloc(large) != nullptr || requested != 0) return 2;
    if (audioMallocProbeDisarm() != large) return 3;
    if (__wrap_malloc(large) != &sentinel || requested != large) return 4;
}
''', encoding='utf-8')
        binary = self.scratch_root / 'probe'
        result = run_build([
            os.environ.get('CXX', 'c++'), str(source),
            str(self.tests / 'audioDecodeBounds/src/mallocProbe.cpp'), '-o', str(binary)],
            capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)


if __name__ == '__main__':
    unittest.main()
