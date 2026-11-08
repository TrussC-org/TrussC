"""Native GL stage capacity, pre-sokol Shader refusal and compiling mutation."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import test_compute_shader as compute_tests

ROOT = Path(__file__).resolve().parents[2]

PREAMBLE = r'''
#define SOKOL_DUMMY_BACKEND
#define SOKOL_TRACE_HOOKS
#define SOKOL_IMPL
#include "sokol/sokol_gfx.h"
#include <limits>
#include <memory>
#include <sstream>
int driverLimit = 0, queries = 0, made = 0;
constexpr int GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS = 0x90D6;
void glGetIntegerv(int name, int* value) {
    if (name != GL_MAX_VERTEX_SHADER_STORAGE_BLOCKS) std::abort();
    ++queries;
    *value = driverLimit;
}
namespace trussc {
std::ostringstream warnings;
std::ostream& logWarning(const char*) { return warnings; }
std::ostream& logError(const char*) { std::abort(); }
namespace internal {
template<class T> void deferGpuDestroy(T) {}
}
}
'''

LOAD_SEAM = r'''
namespace trussc {
class ShaderProbe {
public:
    sg_shader shader{};
    sg_buffer vertexBuffer{}, indexBuffer{};
    sg_pipeline_desc recipe_{};
    std::shared_ptr<int> stream_;
    bool loaded = false, hasStorageBuffers_ = false, hasVertexAttributes_ = false;
    void clear() {
        sg_destroy_shader(shader);
        sg_destroy_buffer(vertexBuffer);
        shader = {}; vertexBuffer = {};
        loaded = hasStorageBuffers_ = hasVertexAttributes_ = false;
    }
    sg_pipeline_desc createPipelineDesc() { return {}; }
    void createVertexBuffer() {
        float vertices[4]{};
        sg_buffer_desc desc{}; desc.data = SG_RANGE(vertices);
        vertexBuffer = sg_make_buffer(&desc);
    }
'''

MAIN = r'''
int stageCount = 1;
sg_shader_stage stage = SG_SHADERSTAGE_VERTEX;
const sg_shader_desc* descriptor(sg_backend) {
    static sg_shader_desc desc;
    // Use the existing generated @vs storage-buffer shader, without editing it.
    desc = *draw_particles_shader_desc(SG_BACKEND_GLCORE);
    desc.views[0] = {};
    for (int i = SG_MAX_VIEW_BINDSLOTS - stageCount; i < SG_MAX_VIEW_BINDSLOTS; ++i) {
        desc.views[i].storage_buffer.stage = stage;
        desc.views[i].storage_buffer.readonly = true;
    }
    return &desc;
}
int main() {
    using namespace trussc;
    sg_desc setup{};
    sg_setup(&setup);
    _sg.features.compute = true;
    sg_trace_hooks hooks{};
    hooks.make_shader = [](const sg_shader_desc*, sg_shader, void*) { ++made; };
    sg_install_trace_hooks(&hooks);
    internal::initializeVertexStorageBufferLimit(); // Simulate Pi: vertex capacity 0.
    ShaderProbe shader;
    for (int attempt = 0; attempt < 2; ++attempt) {
        warnings.str("");
        if (shader.load(descriptor) || shader.loaded || shader.shader.id || made) return 1;
        if (warnings.str() != "this GPU can't read storage buffers in vertex shaders (requested: 1, supported: 0)") return 2;
    }
    // Fragment storage remains usable at vertex capacity 0.
    stage = SG_SHADERSTAGE_FRAGMENT;
    warnings.str("");
    if (!shader.load(descriptor) || made != 1 || !warnings.str().empty()) return 3;
    stage = SG_SHADERSTAGE_VERTEX;
    driverLimit = 1;
    if (internal::vertexStorageBufferLimit() != 0 || queries != 1) return 4;
    internal::initializeVertexStorageBufferLimit(); // Next GPU initialization.
    if (!shader.load(descriptor) || made != 2 || !warnings.str().empty()) return 5;
    stageCount = 2;
    if (shader.load(descriptor) || made != 2) return 6;
    if (warnings.str() != "this GPU can't read storage buffers in vertex shaders (requested: 2, supported: 1)") return 7;
    shader.clear();
    sg_shutdown();
}
'''

CACHE_MAIN = r'''
int main() {
    using namespace trussc::internal;
    initializeVertexStorageBufferLimit();
    if (queries || vertexStorageBufferLimit() != std::numeric_limits<int>::max()) return 1;
    sg_desc desc{}; sg_setup(&desc);
    _sg.features.compute = false;
    initializeVertexStorageBufferLimit();
    if (queries || vertexStorageBufferLimit() != std::numeric_limits<int>::max()) return 2;
    _sg.features.compute = true;
    initializeVertexStorageBufferLimit();
    if (TEST_NATIVE) {
        if (queries != 1 || vertexStorageBufferLimit() != 0) return 3;
        driverLimit = 4;
        if (vertexStorageBufferLimit() != 0 || queries != 1) return 4;
        initializeVertexStorageBufferLimit();
        if (queries != 2 || vertexStorageBufferLimit() != 4) return 5;
    } else if (queries || vertexStorageBufferLimit() != std::numeric_limits<int>::max()) return 6;
    _sg.features.compute = false;
    initializeVertexStorageBufferLimit();
    if (vertexStorageBufferLimit() != std::numeric_limits<int>::max()) return 7;
    sg_shutdown();
}
'''


def limit_source(backend):
    text = (ROOT / 'core/include/tc/app/tcGlobal.cpp').read_text(encoding='utf-8')
    start = text.index('namespace internal {')
    end = text.index('\nnamespace {\nthread_local', start)
    # Define the platform only after sokol's dummy implementation is compiled.
    defines = ''.join(f'#define {name}\n' for name in backend)
    undefs = ''.join(f'#undef {name}\n' for name in backend)
    return defines + 'namespace trussc {\n' + text[start:end] + '\n}\n}\n' + undefs


class VertexStorageLimitTests(unittest.TestCase):
    compile_and_run = compute_tests.ComputeShaderTests.compile_and_run

    def test_load_and_compiling_guard_mutant(self):
        text = (ROOT / 'core/include/tc/gpu/tcShader.h').read_text(encoding='utf-8')
        start = text.index('    bool load(')
        load = text[start:text.index('\n    void clear()', start)]
        source = limit_source(['SOKOL_GLCORE']) + LOAD_SEAM + load + '\n};\n}\n'
        # Generate into scratch so a clean checkout needs no prebuilt app headers.
        compiler = ROOT / 'core/tools/sokol-shdc' / ('sokol-shdc.exe' if os.name == 'nt' else 'sokol-shdc')
        self.assertTrue(compiler.is_file(), 'requires the locally installed sokol-shdc; no downloads')
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as temporary:
            generated = Path(temporary) / 'particles.glsl.h'
            shader = ROOT / 'examples/graphics/computeParticlesExample/src/particles.compute.glsl'
            result = subprocess.run([str(compiler), '-i', str(shader), '-o', str(generated),
                                     '-l', 'glsl430', '--ifdef'],
                                    capture_output=True, text=True, encoding='utf-8')
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            preamble = PREAMBLE + f'#define SOKOL_GLCORE\n#include "{generated}"\n#undef SOKOL_GLCORE\n'
            self.compile_and_run(source, True, MAIN, preamble)
            guard = 'if (vertexStorageBuffers > vertexStorageLimit)'
            self.assertIn(guard, source)
            self.compile_and_run(source.replace(guard, 'if (false)', 1), False, MAIN, preamble)

    def test_cache_and_backend_selection(self):
        for backend, native in [(['SOKOL_GLCORE'], 1), (['SOKOL_GLES3'], 1),
                                (['SOKOL_GLES3', '__EMSCRIPTEN__'], 0), ([], 0)]:
            with self.subTest(backend=backend):
                self.compile_and_run(limit_source(backend), True,
                                     f'#define TEST_NATIVE {native}\n' + CACHE_MAIN, PREAMBLE)


if __name__ == '__main__':
    unittest.main()
