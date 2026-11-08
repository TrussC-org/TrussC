"""Storage buffer ownership, reflection refusals and compiling mutations (#1281)."""
from pathlib import Path
import unittest
import test_compute_shader as compute_tests
PREAMBLE = compute_tests.PREAMBLE
HEADER = compute_tests.HEADER

ROOT = Path(__file__).resolve().parents[2]
STORAGE = ROOT / 'core/include/tc/gpu/tcStorageBuffer.h'
GRAPHICS = ROOT / 'core/include/tc/gpu/tcShader.h'


def graphics_setter():
    text = GRAPHICS.read_text(encoding='utf-8')
    start = text.index('    TC_LUA_SKIP bool setStorageBuffer(')
    end = text.index('\n    // ---', start)
    return '''
namespace trussc {
class GraphicsProbe {
public:
    sg_shader shader = {};
    bool loaded = true;
    struct Binding { sg_view view; sg_sampler sampler; };
    std::unordered_map<int, Binding> pendingViews;
    static void warnStorageBuffer(const char* message) { warnings << message; }
''' + text[start:end] + '\n};\n}\n'


def graphics_draw_paths():
    """Compile actual submission/draw/guard bodies with a minimal target seam."""
    text = GRAPHICS.read_text(encoding='utf-8')
    def section(start, end):
        a = text.index(start)
        return text[a:text.index(end, a)]
    submit = section('    void submitVertices(', '\nprotected:')
    helpers = section('    void snapshotUniforms_(', '\n    bool hasStorageBuffers_')
    guard = section('    bool validateStorageBindings(', '\n    // Slots')
    warn = section('    static void warnStorageBuffer(', '\n    static bool validateTextureSlot')
    fullscreen = text[text.index('class FullscreenShader'):]
    fullscreen = fullscreen[fullscreen.index('    void draw()'):fullscreen.index('\nprotected:')]
    return DRAW_SEAM + submit + helpers + guard + warn + '\n};\nclass FullscreenProbe : public GraphicsDrawProbe {\npublic:\n' + fullscreen + '\n};\n}\n'


DRAW_SEAM = r"""
#include <memory>
#include <vector>
#include <unordered_map>
namespace trussc {
struct ShaderVertex { float x, y; };
enum class PrimitiveType { Lines, LineStrip, Triangles };
namespace internal {
struct ShaderStreamState { sg_buffer vertices{}, indices{}; };
struct DeferredShaderDraw {
    std::shared_ptr<ShaderStreamState> stream;
    sg_pipeline pipeline{};
    uint64_t pipelineGeneration = 0;
    std::vector<ShaderVertex> vertices;
    PrimitiveType type{};
    std::vector<std::pair<int, std::vector<uint8_t>>> uniforms;
    sg_bindings bindings{};
    int layerId = 0;
};
struct Target {
    bool isFbo = false;
    int sglLayerNext = 1;
    std::vector<DeferredShaderDraw> deferredShaderDraws;
    void advanceLayer() { ++sglLayerNext; }
    void flushLayers() {}
} target;
struct WindowContext { Target* currentTarget = &target; } context;
WindowContext& currentWindowContext() { return context; }
Target& currentTarget() { return target; }
bool ensureCurrentPass() { return true; }
void applyTargetDrawingState() {}
void reportImportError(const char*, const char*) { std::abort(); }
bool appendTargetRecording(Target&, std::vector<DeferredShaderDraw>& list, DeferredShaderDraw draw) {
    list.push_back(std::move(draw)); return true;
}
}
class GraphicsDrawProbe {
public:
    bool loaded = true, hasStorageBuffers_ = true;
    sg_shader shader{};
    sg_buffer vertexBuffer{}, indexBuffer{};
    sg_pipeline pipeline{};
    sg_view storageView{};
    std::shared_ptr<internal::ShaderStreamState> stream_;
    std::unordered_map<int, std::vector<uint8_t>> pendingUniforms;
    std::vector<uint8_t> paramsData_;
    struct Selection { sg_pipeline raw; uint64_t generation; };
    Selection selectPipelineForCurrentTarget_() { return {pipeline, 1}; }
    void fillTextureBindings(sg_bindings& bind) { bind.views[0] = storageView; }
    void setupBindings(sg_bindings&) {}
"""

DRAW_MAIN = r"""
#undef sg_query_features
int draws = 0, bindings = 0, errors = 0;
int main(int argc, char** argv) {
    using namespace trussc;
    // Each refusal runs in a fresh process, with fatal debug validation enabled.
    const std::string mode = argc > 1 ? argv[1] : "valid";
    sg_desc setup{};
    setup.logger.func = [](const char*, uint32_t level, uint32_t, const char*, uint32_t, const char*, void*) {
        if (level <= 1) ++errors;
    };
    sg_setup(&setup);
    _sg.features.compute = true;
    sg_trace_hooks hooks{};
    hooks.apply_bindings = [](const sg_bindings*, void*) { ++bindings; };
    hooks.draw = [](int, int, int, void*) { ++draws; };
    sg_install_trace_hooks(&hooks);
    sg_shader_desc desc{};
    desc.vertex_func.source = "vs"; desc.fragment_func.source = "fs";
    desc.views[0].storage_buffer.stage = SG_SHADERSTAGE_FRAGMENT;
    desc.views[0].storage_buffer.readonly = mode != "writable";
    FullscreenProbe probe;
    probe.shader = sg_make_shader(&desc);
    sg_pipeline_desc pd{};
    pd.shader = probe.shader;
    pd.index_type = SG_INDEXTYPE_UINT16;
    pd.depth.pixel_format = SG_PIXELFORMAT_NONE;
    probe.pipeline = sg_make_pipeline(&pd);
    float vertices[16]{};
    sg_buffer_desc bd{}; bd.data = SG_RANGE(vertices);
    probe.vertexBuffer = sg_make_buffer(&bd);
    uint16_t indices[6]{};
    bd = {}; bd.usage.index_buffer = true; bd.data = SG_RANGE(indices);
    probe.indexBuffer = sg_make_buffer(&bd);
    StorageBuffer buffer;
    if (!buffer.allocate(32)) return 1;
    if (mode != "missing") probe.storageView = buffer.getView();
    if (mode == "retired") sg_destroy_view(probe.storageView);
    sg_image_desc image{}; image.width = image.height = 4; image.usage.color_attachment = true;
    sg_view_desc view{}; view.color_attachment.image = sg_make_image(&image);
    sg_pass pass{}; pass.attachments.colors[0] = sg_make_view(&view);
    sg_begin_pass(&pass);
    ShaderVertex vertex{};
    const bool valid = mode == "valid";
    internal::target.isFbo = TEST_FBO;
    warnings.str("");
    if (TEST_FULLSCREEN) probe.draw();
    else probe.submitVertices(&vertex, 1, PrimitiveType::Triangles);
    if (valid) {
        if (!warnings.str().empty()) return 2;
    } else {
        const char* expected = mode == "writable" ? "read-only access" : "Missing or wrong storage buffer view type";
        if (warnings.str().find(expected) == std::string::npos) return 3;
    }
    if (internal::target.deferredShaderDraws.size() != (valid && !TEST_FULLSCREEN ? 1u : 0u) ||
        draws != (valid && TEST_FULLSCREEN ? 1 : 0) ||
        bindings != (valid && TEST_FULLSCREEN ? 1 : 0) || errors) return 4;
    sg_end_pass();
    sg_shutdown();
}
"""


MAIN = r'''
#undef sg_query_features
int errors = 0, copies = 0, writes = 0;
bool gpuUploadSource = true;
const sg_shader_desc* descriptor(sg_backend) {
    static sg_shader_desc d;
    d = {};
    d.compute_func.source = "dummy compute";
    d.views[0].storage_buffer.stage = SG_SHADERSTAGE_COMPUTE;
    d.views[1].storage_buffer.stage = SG_SHADERSTAGE_COMPUTE;
    d.views[1].storage_buffer.readonly = true;
    return &d;
}
const sg_shader_desc* readonlyDescriptor(sg_backend backend) {
    static sg_shader_desc d;
    d = *descriptor(backend);
    d.views[0].storage_buffer.readonly = true;
    return &d;
}
int main() {
    using namespace trussc;
    float values[8] = {};
    {
        StorageBuffer buffer;
        if (buffer.allocate(32) || buffer.update(values, 32)) return 1;
        if (warnings.str().find("without an initialized GPU") == std::string::npos) return 2;
        GraphicsProbe graphics;
        if (graphics.setStorageBuffer(0, buffer)) return 3;
    }
    sg_desc setup = {};
    setup.logger.func = [](const char*, uint32_t level, uint32_t, const char*, uint32_t, const char*, void*) {
        if (level <= 1) ++errors;
    };
    sg_setup(&setup);
    _sg.features.compute = true;
    sg_trace_hooks hooks = {};
    hooks.copy_buffer_to_buffer = [](const sg_copy_buffer_to_buffer_desc* copy, void*) {
        ++copies;
        const auto usage = sg_query_buffer_usage(copy->src.buffer);
        gpuUploadSource &= usage.vertex_buffer && !usage.staging_buffer;
    };
    hooks.write_buffer_transient = [](const sg_write_buffer_desc*, void*) { ++writes; };
    sg_install_trace_hooks(&hooks);
    {
        computeEnabled = false;
        StorageBuffer buffer;
        if (buffer.allocate(32)) return 4;
        computeEnabled = true;
        if (buffer.allocate(0) || buffer.allocate(3) || buffer.allocate(size_t(INT_MAX) + 1)) return 5;
        if (!buffer.allocate(32, values) || !buffer.isAllocated() || buffer.getSize() != 32) return 6;
        const auto usage = sg_query_buffer_usage(buffer.getBuffer());
        if (!usage.storage_buffer || !usage.copy_dst || usage.write_transient ||
            sg_query_view_buffer(buffer.getView()).id != buffer.getBuffer().id) return 7;
        if (buffer.update(nullptr, 16) || buffer.update(values, 0) || buffer.update(values, 36) || buffer.update(values, 3)) return 8;
        if (!buffer.update(values, 16) || copies != 1 || writes != 1 || internal::suspensions != 1 || !gpuUploadSource) return 9;
        if (buffer.update(values, 16) || copies != 1 || writes != 1) return 10;
        sg_commit();
        if (!buffer.update(values, 32) || copies != 2 || writes != 2) return 11;
        const auto handle = buffer.getBuffer();
        const auto view = buffer.getView();
        StorageBuffer moved(std::move(buffer));
        if (buffer.getSize() || buffer.getBuffer().id || buffer.getView().id || buffer.isAllocated() ||
            !moved.isAllocated() || moved.getBuffer().id != handle.id || moved.getView().id != view.id) return 12;
        if (moved.update(values, 16)) return 13; // upload limit follows ownership
        StorageBuffer assigned;
        if (!assigned.allocate(16)) return 14;
        assigned = std::move(moved);
        if (moved.isAllocated() || moved.getSize() || assigned.getSize() != 32 || assigned.getView().id != view.id) return 15;
        assigned.clear();
        if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID || sg_query_buffer_state(handle) != SG_RESOURCESTATE_VALID) return 17;
        if (assigned.isAllocated() || assigned.getSize() || assigned.getView().id || assigned.getBuffer().id) return 16;
    }
    {
        StorageBuffer output, input;
        if (!output.allocate(32) || !input.allocate(32)) return 20;
        ComputeShader compute;
        if (!compute.load(descriptor)) return 21;
        if (compute.setStorageBuffer(-1, output) || compute.setStorageBuffer(SG_MAX_VIEW_BINDSLOTS, output) ||
            compute.setStorageBuffer(2, output)) return 22;
        StorageBuffer empty;
        if (compute.setStorageBuffer(0, empty)) return 23;
        if (!compute.setStorageBuffer(0, output) || !compute.setStorageBuffer(1, output) || compute.dispatch(1)) return 24;
        if (!compute.setStorageBuffer(1, input) || !compute.dispatch(1)) return 25;
        if (!compute.setStorageBuffer(0, input) || compute.dispatch(1)) return 26;
        // Replacing several slots may temporarily alias: ping-pong rebinding
        // must work without a third buffer or reloading the program.
        if (!compute.setStorageBuffer(1, output) || !compute.dispatch(1)) return 43;
        ComputeShader missing;
        if (!missing.load(descriptor) || missing.dispatch(1)) return 27;
        sg_bindings bindings = {};
        auto desc = *descriptor(SG_BACKEND_DUMMY);
        bindings.views[0] = output.getView(); bindings.views[1] = output.getView();
        if (!internal::storageBufferBindingsError(desc, bindings, true)) return 28;
        desc.views[0].storage_buffer.readonly = true;
        if (internal::storageBufferBindingsError(desc, bindings, true)) return 29; // repeated read-only is legal
        ComputeShader readonlyCompute;
        if (!readonlyCompute.load(readonlyDescriptor) || !readonlyCompute.setStorageBuffer(0, output) ||
            !readonlyCompute.setStorageBuffer(1, output) || !readonlyCompute.dispatch(1)) return 41;
        sg_shader_desc readonlyDesc = desc;
        readonlyDesc.compute_func = {}; readonlyDesc.vertex_func.source = "vs"; readonlyDesc.fragment_func.source = "fs";
        for (int i = 0; i < 2; ++i) readonlyDesc.views[i].storage_buffer.stage = SG_SHADERSTAGE_VERTEX;
        GraphicsProbe graphics;
        graphics.shader = sg_make_shader(&readonlyDesc);
        computeEnabled = false;
        if (graphics.setStorageBuffer(0, output)) return 44;
        computeEnabled = true;
        graphics.loaded = false;
        if (graphics.setStorageBuffer(0, output)) return 45;
        graphics.loaded = true;
        if (!graphics.setStorageBuffer(0, output) || !graphics.setStorageBuffer(1, output) ||
            graphics.setStorageBuffer(-1, output) || graphics.setStorageBuffer(SG_MAX_VIEW_BINDSLOTS, output) ||
            graphics.setStorageBuffer(2, output) || graphics.setStorageBuffer(0, empty)) return 30;
        if (internal::storageBufferBindingsError(readonlyDesc, bindings, false)) return 31;
        desc.views[0].storage_buffer.readonly = false;
        desc.views[0].storage_buffer.stage = SG_SHADERSTAGE_FRAGMENT;
        const char* accessError = internal::storageBufferBindingsError(desc, bindings, false);
        if (!accessError || std::string(accessError).find("read-only access") == std::string::npos) return 32;
        readonlyDesc.views[0].storage_buffer.stage = SG_SHADERSTAGE_FRAGMENT;
        readonlyDesc.views[0].storage_buffer.readonly = false;
        graphics.shader = sg_make_shader(&readonlyDesc);
        if (graphics.setStorageBuffer(0, output)) return 33;
        sg_buffer_desc transientDesc = {};
        transientDesc.size = 32; transientDesc.usage.storage_buffer = true; transientDesc.usage.write_transient = true;
        const auto transient = sg_make_buffer(&transientDesc);
        sg_view_desc viewDesc = {}; viewDesc.storage_buffer.buffer = transient;
        bindings.views[0] = sg_make_view(&viewDesc);
        desc = *descriptor(SG_BACKEND_DUMMY);
        bindings.views[1] = input.getView();
        if (!internal::storageBufferBindingsError(desc, bindings, true)) return 34;
        compute.setTexture(0, bindings.views[0], {});
        if (compute.dispatch(1)) return 35;
        bindings.views[0] = {};
        if (!internal::storageBufferBindingsError(desc, bindings, true)) return 36;
        sg_image_desc imageDesc = {}; imageDesc.width = imageDesc.height = 4; imageDesc.usage.color_attachment = true;
        sg_view_desc textureDesc = {}; textureDesc.texture.image = sg_make_image(&imageDesc);
        bindings.views[0] = sg_make_view(&textureDesc);
        const char* typeError = internal::storageBufferBindingsError(desc, bindings, true);
        if (!typeError || std::string(typeError).find("view type") == std::string::npos) return 37;
        compute.setTexture(0, bindings.views[0], {});
        if (compute.dispatch(1)) return 38;
        bindings.views[0] = output.getView();
        sg_destroy_buffer(output.getBuffer());
        if (output.isAllocated()) return 42;
        if (!internal::storageBufferBindingsError(desc, bindings, true)) return 39;
    }
    sg_shutdown();
    return errors ? 40 : 0;
}
'''


class StorageBufferTests(unittest.TestCase):
    compile_and_run = compute_tests.ComputeShaderTests.compile_and_run
    def test_draw_vertices_warning_gates_and_compiling_mutants(self):
        text = GRAPHICS.read_text(encoding='utf-8')
        start = text.index('    void drawVertices(')
        # Compile the production refusal path; count calls that reach drawing.
        end = text.index('        auto& wctx =', start)
        source = ("namespace trussc { struct DrawProbe {\n"
                  "bool loaded = true, hasVertexAttributes_ = false; int accepted = 0;\n"
                  + text[start:end] + "++accepted; } }; }\n")
        preamble = PREAMBLE + '#include "tc/utils/tcOnceGate.h"\n'
        main = r'''
int main() {
    using namespace trussc;
    DrawProbe probe;
    for (int i = 0; i < 3; ++i) {
        probe.drawVertices(-1); probe.drawVertices(3, -1);
    }
    if (probe.accepted || warnings.str() != "drawVertices requires nonnegative vertex and instance counts") return 1;
    warnings.str("");
    probe.hasVertexAttributes_ = true;
    for (int i = 0; i < 3; ++i) probe.drawVertices(3);
    if (probe.accepted || warnings.str() != "drawVertices requires a shader without vertex attributes") return 2;
    probe.hasVertexAttributes_ = false;
    probe.drawVertices(0); probe.drawVertices(3, 0);
    probe.loaded = false; probe.drawVertices(3);
    if (probe.accepted) return 3;
    probe.loaded = true; probe.drawVertices(3);
    return probe.accepted == 1 ? 0 : 4;
}
'''
        self.compile_and_run(source, True, main, preamble)
        for gate in ('negativeCount', 'vertexAttributes'):
            with self.subTest(gate=gate):
                before = f'if ({gate}.isFirstTime())'
                self.assertIn(before, source)
                self.compile_and_run(source.replace(before, 'if (true)', 1),
                                     False, main, preamble)

    def test_fullscreen_submission_parity_and_compiling_mutant(self):
        preamble = PREAMBLE.replace('#define SOKOL_IMPL',
                                    '#define SOKOL_DEBUG\n#define SOKOL_TRACE_HOOKS\n#define SOKOL_IMPL')
        paths = graphics_draw_paths()
        before = 'if (!validateStorageBindings(bind)) return;'
        start = paths.index('class FullscreenProbe')
        self.assertIn(before, paths[start:])
        mutant = paths[:start] + paths[start:].replace(before, '(void)0;', 1)
        for mode in ('missing', 'writable', 'retired', 'valid'):
            for fullscreen, fbo in ((False, False), (False, True), (True, False), (True, True)):
                # Fresh processes also isolate the real shared OnceGate warning.
                main = DRAW_MAIN.replace('int main(int argc, char** argv)', 'int main()').replace(
                    'argc > 1 ? argv[1] : "valid"', '"' + mode + '"').replace(
                    'TEST_FULLSCREEN', str(fullscreen).lower()).replace('TEST_FBO', str(fbo).lower())
                with self.subTest(mode=mode, fullscreen=fullscreen, fbo=fbo):
                    self.compile_and_run(paths, True, main, preamble)
                    if fullscreen:
                        self.compile_and_run(mutant, mode == 'valid', main, preamble)

    def test_storage_and_compiling_mutations(self):
        storage = STORAGE.read_text(encoding='utf-8').replace('#pragma once', '')
        compute = HEADER.read_text(encoding='utf-8').replace('#pragma once', '')
        graphics = graphics_setter()
        preamble = PREAMBLE.replace('#include "tc/gpu/tcStorageBuffer.h"', '').replace(
            '#define SOKOL_IMPL', '#define SOKOL_DEBUG\n#define SOKOL_VALIDATE_NON_FATAL\n#define SOKOL_TRACE_HOOKS\n#define SOKOL_IMPL')
        self.compile_and_run(storage + compute + graphics, True, MAIN, preamble)
        mutations = [
            ('storage', 'if (!sg_isvalid())', 'if (false)'),
            ('storage', 'if (!sg_query_features().compute)', 'if (false)'),
            ('storage', '!size || size % 4 || size > INT_MAX', 'false'),
            ('storage', '!data || !size || size > size_ || size % 4', 'false'),
            ('storage', 'updated_ && uploadFrame_ == frame', 'false'),
            ('storage', 'sg_write_buffer_transient(&write);', '(void)write;'),
            ('storage', 'sg_copy_buffer_to_buffer(&copy);', '(void)copy;'),
            ('storage', 'internal::suspendForImageUpload();', '(void)0;'),
            ('storage', 'desc.usage.copy_dst = true;', 'desc.usage.copy_dst = false;'),
            ('storage', 'desc.usage.vertex_buffer = true;', 'desc.usage.staging_buffer = true;'),
            ('storage', 'size_ = size;', 'size_ = 0;'),
            ('storage', 'size_t getSize() const { return size_; }', 'size_t getSize() const { return 0; }'),
            ('storage', 'sg_buffer getBuffer() const { return buffer_; }', 'sg_buffer getBuffer() const { return {}; }'),
            ('storage', 'sg_view getView() const { return view_; }', 'sg_view getView() const { return {}; }'),
            ('storage', 'sg_query_buffer_state(buffer_) == SG_RESOURCESTATE_VALID &&', 'true &&'),
            ('storage', 'internal::deferGpuDestroy(view_);', 'sg_destroy_view(view_);'),
            ('storage', 'internal::deferGpuDestroy(buffer_);', 'sg_destroy_buffer(buffer_);'),
            ('storage', 'view_ = std::exchange(other.view_, {});', 'view_ = other.view_;'),
            ('storage', 'size_ = std::exchange(other.size_, 0);', 'size_ = other.size_;'),
            ('storage', 'updated_ = std::exchange(other.updated_, false);', 'updated_ = false;'),
            ('storage', 'view_ = {}; buffer_ = {}; staging_ = {};', '(void)0;'),
            ('storage', 'size_ = 0; updated_ = false;', 'updated_ = false;'),
            ('storage', '(!compute && (required.stage == SG_SHADERSTAGE_COMPUTE || !required.readonly))', 'false'),
            ('storage', 'sg_query_view_type(view) != SG_VIEWTYPE_STORAGEBUFFER', 'false'),
            ('storage', 'if (!required.readonly && sg_query_buffer_usage(buffer).write_transient)', 'if (false)'),
            ('storage', '(!previous.readonly || !required.readonly)', 'false'),
            ('compute', 'if (!buffer.isAllocated())', 'if (false)'),
            ('compute', 'sg_query_shader_desc(shader_).views[slot].storage_buffer.stage != SG_SHADERSTAGE_COMPUTE', 'false'),
            ('compute', 'warn(error); return false;', 'warn(error);'),
            ('graphics', '|| !required.readonly', '|| false'),
            ('graphics', '|| !sg_query_features().compute', '|| false'),
            ('graphics', '|| !loaded', '|| false'),
            ('graphics', '|| !buffer.isAllocated()', '|| false'),
        ]
        for which, before, after in mutations:
            with self.subTest(which=which, mutation=before):
                sources = dict(storage=storage, compute=compute, graphics=graphics)
                self.assertIn(before, sources[which])
                sources[which] = sources[which].replace(before, after, 1)
                self.compile_and_run(sources['storage'] + sources['compute'] + sources['graphics'], False, MAIN, preamble)


if __name__ == '__main__':
    unittest.main()
