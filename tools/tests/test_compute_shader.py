"""Offline compute refusal seams and shader target separation (#1281)."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
HEADER = ROOT / 'core/include/tc/gpu/tcComputeShader.h'
PREAMBLE = r'''
#define SOKOL_DUMMY_BACKEND
#define SOKOL_IMPL
#include "sokol/sokol_gfx.h"
#include <cstring>
#include <utility>
#include <string>
#include <sstream>
namespace trussc {
struct Vec2 { float x, y; };
struct Vec3 { float x, y, z; };
struct Vec4 { float x, y, z, w; Vec4(float a,float b,float c,float d):x(a),y(b),z(c),w(d){} };
struct Color { float r,g,b,a; };
struct Texture {
    bool isAllocated() const { return false; }
    sg_view getStorageView() const { return {}; }
    sg_view getView() const { return {}; }
    sg_sampler getSampler() const { return {}; }
};
std::ostringstream warnings;
std::ostream& logWarning(const char*) { return warnings; }
namespace internal {
int suspensions = 0;
template<class T> void deferGpuDestroy(T) {}
bool suspendForImageUpload() { ++suspensions; return false; }
}
}
bool computeEnabled = true;
sg_features computeTestFeatures() { sg_features f = {}; f.compute = computeEnabled; return f; }
#define sg_query_features computeTestFeatures
#include "tc/gpu/tcStorageBuffer.h"
'''
MAIN = r'''
#undef sg_query_features
int calls = 0;
const sg_shader_desc* descriptor(sg_backend) { ++calls; return nullptr; }
int main() {
    using namespace trussc;
    {
        ComputeShader shader;
        if (shader.load(descriptor) || shader.dispatch(1) || calls || sg_isvalid()) return 1;
        if (warnings.str().find("without an initialized GPU") == std::string::npos) return 2;
    }
    warnings.str("");
    sg_desc setup = {};
    sg_setup(&setup);
    computeEnabled = false; // Models WebGL2 / a GPU without compute.
    {
        ComputeShader shader;
        if (shader.load(descriptor) || shader.dispatch(1) || calls) return 3;
        if (warnings.str().find("unsupported by this backend") == std::string::npos) return 4;
        if (internal::suspensions) return 5;
    }
    sg_shutdown();
}
'''


BINDING_MAIN = r'''
#undef sg_query_features
const sg_shader_desc* descriptor(sg_backend) {
    static sg_shader_desc d = {};
    d.compute_func.source = "dummy compute";
    d.views[0].storage_image.stage = SG_SHADERSTAGE_COMPUTE;
    d.views[0].storage_image.image_type = SG_IMAGETYPE_2D;
    d.views[0].storage_image.access_format = SG_PIXELFORMAT_RGBA8;
    d.views[0].storage_image.writeonly = true;
    d.views[1].texture.stage = SG_SHADERSTAGE_COMPUTE;
    d.views[1].texture.image_type = SG_IMAGETYPE_2D;
    d.views[1].texture.sample_type = SG_IMAGESAMPLETYPE_FLOAT;
    d.samplers[1].stage = SG_SHADERSTAGE_COMPUTE;
    d.samplers[1].sampler_type = SG_SAMPLERTYPE_FILTERING;
    d.texture_sampler_pairs[0].stage = SG_SHADERSTAGE_COMPUTE;
    d.texture_sampler_pairs[0].view_slot = 1;
    d.texture_sampler_pairs[0].sampler_slot = 1;
    return &d;
}
int errors = 0;
int main() {
    sg_desc setup = {};
    setup.logger.func = [](const char*, uint32_t level, uint32_t, const char* message, uint32_t, const char*, void*) {
        if (level <= 1) { ++errors; std::fprintf(stderr, "%s\n", message ? message : "sokol error"); }
    };
    sg_setup(&setup);
    _sg.features.compute = true;
    _sg.features.msaa_texture_bindings = true;
    _sg.formats[SG_PIXELFORMAT_RGBA8].write = true;
    sg_image_desc d = {};
    d.width = d.height = 8;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.usage.storage_image = true;
    const auto output = sg_make_image(&d);
    sg_view_desc v = {};
    v.storage_image.image = output;
    const auto storage = sg_make_view(&v);
    const sg_sampler_desc samplerDesc = {};
    const auto sampler = sg_make_sampler(&samplerDesc);
    auto sampledView = [](sg_image image) {
        sg_view_desc v = {}; v.texture.image = image; return sg_make_view(&v);
    };
    auto bind = [&](trussc::ComputeShader& shader, sg_image input) {
        return shader.load(descriptor) &&
            shader.setTexture(0, storage, {}) && shader.setTexture(1, sampledView(input), sampler);
    };
    auto refused = [&](sg_image input, const char* message) {
        trussc::ComputeShader shader;
        trussc::warnings.str("");
        const int before = trussc::internal::suspensions;
        return bind(shader, input) && !shader.dispatch(1) &&
            trussc::internal::suspensions == before &&
            trussc::warnings.str().find(message) != std::string::npos;
    };
    if (!refused(output, "separate images")) return 10;
    d.usage.storage_image = false;
    d.usage.color_attachment = true;
    d.type = SG_IMAGETYPE_CUBE;
    if (!refused(sg_make_image(&d), "image type")) return 11;
    d.type = SG_IMAGETYPE_2D;
    d.sample_count = 4;
    if (!refused(sg_make_image(&d), "multisampling")) return 12;
    d.sample_count = 1;
    const auto input = sg_make_image(&d);
    // Model an otherwise sampleable format without linear filtering.
    _sg.formats[SG_PIXELFORMAT_RGBA8].filter = false;
    if (!refused(input, "sample type")) return 13;
    _sg.formats[SG_PIXELFORMAT_RGBA8].filter = true;
    {
        trussc::ComputeShader shader;
        if (!bind(shader, input) || !shader.dispatch(1)) return 14;
    }
    if (errors) return 15;
    sg_shutdown();
}
'''


class ComputeShaderTests(unittest.TestCase):
    def compile_and_run(self, source, expected_success, main=MAIN, preamble=PREAMBLE):
        # Use the real class and sokol's dummy backend. Only TrussC's surrounding
        # drawing types and the feature query are substituted; no GPU is needed.
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            directory = Path(directory)
            code = directory / 'refusal.cpp'
            code.write_text(preamble + source + main, encoding='utf-8')
            binary = directory / 'refusal'
            command = [os.environ.get('CXX', 'c++'), '-std=c++20',
                       '-I', str(ROOT / 'core/include'), '-I', str(HEADER.parent),
                       str(code), '-o', str(binary)]
            build = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8")
            self.assertEqual(result.returncode == 0, expected_success,
                             result.stdout + result.stderr + f' exit={result.returncode}')

    def test_refusals_and_compiling_mutations(self):
        source = HEADER.read_text(encoding='utf-8').replace('#pragma once', '')
        self.compile_and_run(source, True)
        for guard in ('if (!sg_isvalid())', 'if (!sg_query_features().compute)'):
            with self.subTest(guard=guard):
                self.assertIn(guard, source)
                self.compile_and_run(source.replace(guard, 'if (false)', 1), False)

    def test_binding_refusals_and_compiling_mutations(self):
        source = HEADER.read_text(encoding='utf-8').replace('#pragma once', '')
        preamble = PREAMBLE.replace('#define SOKOL_IMPL',
                                    '#define SOKOL_DEBUG\n#define SOKOL_VALIDATE_NON_FATAL\n#define SOKOL_IMPL')
        self.compile_and_run(source, True, BINDING_MAIN, preamble)
        for guard in ('if (imageDesc.type != imageType)',
                      'if (sg_query_view_desc(bindings_.views[input]).texture.image.id == image.id)',
                      'if ((imageDesc.sample_count > 1) != required.texture.multisampled)',
                      'required.texture.sample_type == SG_IMAGESAMPLETYPE_FLOAT && !caps.filter'):
            with self.subTest(guard=guard):
                self.assertIn(guard, source)
                replacement = 'if (false)' if guard.startswith('if (') else 'false'
                self.compile_and_run(source.replace(guard, replacement, 1), False,
                                     BINDING_MAIN, preamble)

    @unittest.skipUnless(shutil.which('cmake'), 'requires CMake')
    def test_separate_compute_targets(self):
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            directory = Path(directory)
            shaders = directory / 'src'
            shaders.mkdir()
            shutil.copyfile(ROOT / 'examples/graphics/computeTextureExample/src/texture.compute.glsl',
                            shaders / 'texture.compute.glsl')
            shutil.copyfile(ROOT / 'examples/graphics/computeParticlesExample/src/particles.compute.glsl',
                            shaders / 'particles.compute.glsl')
            shutil.copyfile(ROOT / 'examples/graphics/computeParticlesExample/src/step.compute.glsl',
                            shaders / 'step.compute.glsl')
            shutil.copyfile(ROOT / 'core/tests/computeBuffer/src/pattern.compute.glsl',
                            shaders / 'buffer.compute.glsl')
            (shaders / 'graphics.glsl').write_text('''@vs graphics_vs
void main() { gl_Position = vec4(0.0); }
@end
@fs graphics_fs
layout(location=0) out vec4 color;
void main() { color = vec4(1.0); }
@end
@program graphics graphics_vs graphics_fs
''', encoding='utf-8')
            (directory / 'CMakeLists.txt').write_text(f'''cmake_minimum_required(VERSION 3.20)
project(computeTargets NONE)
include("{ROOT}/core/cmake/trussc_shaders.cmake")
trussc_compile_shaders(fixture "${{CMAKE_CURRENT_SOURCE_DIR}}/src")
''', encoding='utf-8')
            for command in (['cmake', '-S', str(directory), '-B', str(directory / 'build')],
                            ['cmake', '--build', str(directory / 'build'), '--target', 'fixture_shaders_gen']):
                result = subprocess.run(command, capture_output=True, text=True, encoding="utf-8")
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            probe = directory / 'probe.cpp'
            probe.write_text('''#include "sokol/sokol_gfx.h"
#include <cstring>
#include "graphics.glsl.h"
#include "texture.compute.glsl.h"
#include "particles.compute.glsl.h"
int main() {
    const auto graphics = graphics_shader_desc(TEST_BACKEND);
    const auto compute = generate_texture_shader_desc(TEST_BACKEND);
    const auto storage = draw_particles_shader_desc(TEST_BACKEND);
    return !graphics || !compute || !storage ||
        !storage->vertex_func.source || !storage->fragment_func.source ||
        storage->compute_func.source ||
        storage->views[0].storage_buffer.stage != SG_SHADERSTAGE_VERTEX ||
        !storage->views[0].storage_buffer.readonly;
}
''', encoding='utf-8')
            binary = directory / 'probe'
            for macro, backend in (('SOKOL_GLCORE', 'SG_BACKEND_GLCORE'),
                                   ('SOKOL_GLES3', 'SG_BACKEND_GLES3'),
                                   ('SOKOL_METAL', 'SG_BACKEND_METAL_MACOS'),
                                   ('SOKOL_METAL', 'SG_BACKEND_METAL_IOS'),
                                   ('SOKOL_METAL', 'SG_BACKEND_METAL_SIMULATOR'),
                                   ('SOKOL_D3D11', 'SG_BACKEND_D3D11'),
                                   ('SOKOL_WGPU', 'SG_BACKEND_WGPU')):
                with self.subTest(backend=backend):
                    result = subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20',
                                             '-D' + macro, '-DTEST_BACKEND=' + backend,
                                             '-I', str(ROOT / 'core/include'), '-I', str(shaders),
                                             str(probe), '-o', str(binary)], capture_output=True,
                                            text=True, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    result = subprocess.run([str(binary)], capture_output=True, text=True, encoding="utf-8")
                    self.assertEqual(result.returncode, 0, 'generated descriptor probe failed')
            graphics = (shaders / 'graphics.glsl.h').read_text(encoding='utf-8')
            compute = (shaders / 'texture.compute.glsl.h').read_text(encoding='utf-8')
            storage = (shaders / 'particles.compute.glsl.h').read_text(encoding='utf-8')
            for kernel in ('step.compute.glsl.h', 'buffer.compute.glsl.h'):
                self.assertNotIn('spvBufferSizeConstants', (shaders / kernel).read_text(encoding='utf-8'))
            self.assertIn('#version 310 es', storage)
            self.assertNotIn('#version 300 es', storage)
            self.assertIn('#version 300 es', graphics)
            self.assertNotIn('#version 310 es', graphics)
            self.assertIn('#version 310 es', compute)
            self.assertNotIn('#version 300 es', compute)
            self.assertIn('SOKOL_GLES3', compute)
            self.assertIn('SOKOL_D3D11', compute)
            self.assertIn('SOKOL_METAL', compute)
            self.assertIn('SOKOL_WGPU', compute)


if __name__ == '__main__':
    unittest.main()
