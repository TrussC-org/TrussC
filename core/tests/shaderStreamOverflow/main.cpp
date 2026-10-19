#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#define SOKOL_TRACE_HOOKS
#include "sokol/sokol_gfx.h"
#include "tc/gpu/tcShaderStream.h"

#include <cstdio>

using namespace trussc;
using namespace trussc::internal;

namespace {
int failures = 0;
std::vector<sg_buffer> pendingDestroys;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

struct Trace {
    int draws = 0;
    int appends = 0;
    sg_bindings bindings = {};
    int vertexOffset = 0;
    int indexOffset = 0;
    std::vector<uint32_t> indices;
    bool relative = true;
    bool offsets = true;
};

void appended(sg_buffer buffer, const sg_range* data, int offset, void* user) {
    auto& t = *static_cast<Trace*>(user);
    ++t.appends;
    if (sg_query_buffer_desc(buffer).usage.index_buffer) {
        t.indexOffset = offset;
        const auto* indices = static_cast<const uint32_t*>(data->ptr);
        t.indices.assign(indices, indices + data->size / sizeof(uint32_t));
        t.relative &= t.indices.front() == 0;
    } else {
        t.vertexOffset = offset;
    }
}

void bound(const sg_bindings* bindings, void* user) {
    static_cast<Trace*>(user)->bindings = *bindings;
}

void drawn(int base, int count, int instances, void* user) {
    auto& t = *static_cast<Trace*>(user);
    ++t.draws;
    t.offsets &= t.bindings.vertex_buffer_offsets[0] == t.vertexOffset
        && base == t.indexOffset / static_cast<int>(sizeof(uint32_t))
        && count == static_cast<int>(t.indices.size()) && instances == 1;
}

void beginPass() {
    sg_pass pass = {};
    pass.swapchain.width = pass.swapchain.height = 32;
    pass.swapchain.color_format = SG_PIXELFORMAT_RGBA8;
    pass.swapchain.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    pass.swapchain.sample_count = 1;
    sg_begin_pass(&pass);
}

void commit() {
    sg_commit();
    for (sg_buffer buffer : pendingDestroys) sg_destroy_buffer(buffer);
    pendingDestroys.clear();
}

struct Stream {
    sg_buffer vertices;
    sg_buffer indices;
    std::shared_ptr<ShaderStreamState> state = std::make_shared<ShaderStreamState>();
    sg_pipeline pipeline;

    Stream(size_t vertexCount, size_t indexCount, sg_pipeline pip)
        : vertices(makeShaderStreamBuffer(vertexCount * sizeof(ShaderVertex), false)),
          indices(makeShaderStreamBuffer(indexCount * sizeof(uint32_t), true)), pipeline(pip) {
        state->beginFrame(vertices, indices);
    }

    ~Stream() {
        deferGpuDestroy(vertices);
        deferGpuDestroy(indices);
    }

    DeferredShaderDraw capture(size_t count, PrimitiveType type) {
        DeferredShaderDraw draw;
        draw.vertices.resize(count);
        draw.type = type;
        draw.pipeline = pipeline;
        draw.bindings.vertex_buffers[0] = vertices;
        draw.bindings.index_buffer = indices;
        draw.stream = state;
        return draw;
    }
};

void tests(sg_pipeline pip, Trace& trace) {
    {
        ShaderStreamState state;
        state.overflow = true;
        state.vertexBytes = state.indexBytes = 4;
        sg_buffer vertices = {}, indices = {};
        check("missing buffers fail growth without looping", state.beginFrame(vertices, indices)
            == ShaderStreamGrowth::Failed && vertices.id == 0 && indices.id == 0);
    }
    {
        Stream stream(4, 6, pip);
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        executeDeferredShaderDraw(quad);
        check("exact capacity fits", trace.draws == 1 && trace.appends == 2);
        check("quad winding preserved", trace.indices == std::vector<uint32_t>({0, 1, 2, 0, 2, 3}));
        check("pre-check detects full stream", !shaderStreamAppendFits(stream.vertices, sizeof(ShaderVertex)));
        executeDeferredShaderDraw(quad);
        check("overflow skips both appends and draw", trace.draws == 1 && trace.appends == 2);
        check("overflow recorded without poisoning sokol", stream.state->overflow
            && !sg_query_buffer_overflow(stream.vertices) && !sg_query_buffer_overflow(stream.indices));
        check("same-frame growth deferred", stream.state->beginFrame(stream.vertices, stream.indices)
            == ShaderStreamGrowth::None && stream.vertices.id == quad.bindings.vertex_buffers[0].id);
        sg_end_pass();
        sg_disable_stats();
        commit();

        // A stale append_pos must be treated as zero before the first append.
        check("new-frame pre-check ignores stale cursor with stats disabled",
            shaderStreamAppendFits(stream.vertices, 4 * sizeof(ShaderVertex)));
        sg_enable_stats();
        const auto oldVertices = stream.vertices;
        const auto oldIndices = stream.indices;
        check("next frame doubles both streams", stream.state->beginFrame(stream.vertices, stream.indices)
            == ShaderStreamGrowth::Grown && sg_query_buffer_size(stream.vertices) == 8 * sizeof(ShaderVertex)
            && sg_query_buffer_size(stream.indices) == 12 * sizeof(uint32_t));
        check("old buffers queued and still valid", pendingDestroys.size() == 2
            && sg_query_buffer_state(oldVertices) == SG_RESOURCESTATE_VALID
            && sg_query_buffer_state(oldIndices) == SG_RESOURCESTATE_VALID);
        trace = {};
        quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        executeDeferredShaderDraw(quad);
        executeDeferredShaderDraw(quad);
        check("same workload fully draws next frame", trace.draws == 2 && !stream.state->overflow);
        sg_end_pass();
        commit();
        check("old buffers destroyed after commit", sg_query_buffer_state(oldVertices) == SG_RESOURCESTATE_INVALID
            && sg_query_buffer_state(oldIndices) == SG_RESOURCESTATE_INVALID);
    }
    {
        trace = {};
        Stream stream(16, 6, pip);
        beginPass();
        auto quad = stream.capture(4, PrimitiveType::Quads);
        executeDeferredShaderDraw(quad);
        executeDeferredShaderDraw(quad);
        check("index-only overflow does not consume vertices", trace.draws == 1
            && sg_query_buffer_info(stream.vertices).append_pos == 4 * static_cast<int>(sizeof(ShaderVertex)));
        sg_end_pass();
        commit();
    }
    {
        trace = {};
        Stream stream(6, 20, pip);
        auto small = stream.capture(3, PrimitiveType::Triangles);
        beginPass();
        executeDeferredShaderDraw(small);
        executeDeferredShaderDraw(stream.capture(4, PrimitiveType::Quads));
        executeDeferredShaderDraw(small);
        check("vertex-only overflow leaves room for a later fitting draw", trace.draws == 2
            && sg_query_buffer_info(stream.indices).append_pos == 6 * static_cast<int>(sizeof(uint32_t)));
        // Simulate two separately flushed FBO/swapchain queues in one frame.
        sg_end_pass();
        beginPass();
        executeDeferredShaderDraw(small);
        check("multiple passes share demand and overflow", trace.draws == 2
            && stream.state->vertexBytes == 13 * sizeof(ShaderVertex));
        sg_end_pass();
        commit();
        check("demand beyond twice capacity grows by repeated doubling",
            stream.state->beginFrame(stream.vertices, stream.indices) == ShaderStreamGrowth::Grown
            && sg_query_buffer_size(stream.vertices) == 24 * sizeof(ShaderVertex));
        commit();
    }
    {
        trace = {};
        Stream stream(65536, 65536, pip);
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        for (int i = 0; i < 12000; ++i) executeDeferredShaderDraw(quad);
        check("12000 rectangles overflow indices safely", trace.draws == 65536 / 6 && stream.state->overflow);
        sg_end_pass();
        commit();
        stream.state->beginFrame(stream.vertices, stream.indices);
        trace = {};
        quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        for (int i = 0; i < 12000; ++i) executeDeferredShaderDraw(quad);
        check("12000 rectangles fully draw after grow", trace.draws == 12000 && !stream.state->overflow
            && trace.relative && trace.offsets);
        sg_end_pass();
        commit();
    }
    for (auto type : {PrimitiveType::Triangles, PrimitiveType::Quads, PrimitiveType::TriangleStrip}) {
        trace = {};
        Stream stream(65536, 65536, pip);
        auto large = stream.capture(70000, type);
        beginPass();
        executeDeferredShaderDraw(large);
        check("large single draw skipped before growth", trace.draws == 0 && trace.appends == 0);
        sg_end_pass();
        commit();
        stream.state->beginFrame(stream.vertices, stream.indices);
        trace = {};
        large = stream.capture(70000, type);
        beginPass();
        executeDeferredShaderDraw(large);
        check("single draw past 65536 vertices retains uint32 indices",
            trace.draws == 1 && trace.indices.back() == 69999 && !stream.state->overflow);
        if (type == PrimitiveType::TriangleStrip) {
            check("triangle strip alternating winding preserved", trace.indices[0] == 0
                && trace.indices[1] == 1 && trace.indices[2] == 2 && trace.indices[3] == 2
                && trace.indices[4] == 1 && trace.indices[5] == 3);
        }
        // This second append starts beyond vertex 65536, yet its indices stay relative.
        executeDeferredShaderDraw(stream.capture(4, PrimitiveType::Quads));
        check("frame total past 65536 uses draw-relative vertex offset",
            trace.draws == 2 && trace.vertexOffset == 70000 * static_cast<int>(sizeof(ShaderVertex))
            && trace.relative && trace.offsets && trace.indices.back() == 3);
        sg_end_pass();
        commit();
    }
    {
        trace = {};
        DeferredShaderDraw captured;
        {
            Stream stream(3, 3, pip);
            captured = stream.capture(3, PrimitiveType::Triangles);
        }
        // The Shader's accounting owner is gone; replay still owns valid handles/state.
        beginPass();
        executeDeferredShaderDraw(captured);
        executeDeferredShaderDraw(captured);
        check("captured resources and accounting outlive owner",
            captured.stream->overflow && trace.draws == 1 && trace.appends == 2);
        sg_end_pass();
        commit();
    }
}
} // namespace

namespace trussc::internal {
void deferGpuDestroy(sg_buffer buffer) {
    pendingDestroys.push_back(buffer);
}
} // namespace trussc::internal

int main() {
    sg_desc desc = {};
    sg_setup(&desc);
    sg_shader_desc shaderDesc = {};
    sg_shader shader = sg_make_shader(&shaderDesc);
    sg_pipeline_desc pipelineDesc = {};
    pipelineDesc.shader = shader;
    pipelineDesc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;
    pipelineDesc.layout.buffers[0].stride = sizeof(ShaderVertex);
    pipelineDesc.index_type = SG_INDEXTYPE_UINT32;
    sg_pipeline pipeline = sg_make_pipeline(&pipelineDesc);
    Trace trace;
    sg_trace_hooks hooks = {};
    hooks.user_data = &trace;
    hooks.append_buffer = appended;
    hooks.apply_bindings = bound;
    hooks.draw = drawn;
    sg_install_trace_hooks(&hooks);
    tests(pipeline, trace);
    check("all captured indices and offsets valid", trace.relative && trace.offsets);
    sg_destroy_pipeline(pipeline);
    sg_destroy_shader(shader);
    sg_shutdown();
    return failures ? 1 : 0;
}
