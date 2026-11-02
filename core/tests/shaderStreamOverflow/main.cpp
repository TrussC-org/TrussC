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
int allocationFailures = 0;
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
        state->vertices = vertices;
        state->indices = indices;
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
        Stream stream(4, 6, pip);
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        executeDeferredShaderDraw(quad);
        check("exact capacity fits", trace.draws == 1 && trace.appends == 2);
        check("quad winding preserved", trace.indices == std::vector<uint32_t>({0, 1, 2, 0, 2, 3}));
        executeDeferredShaderDraw(quad);
        check("overflow grows and draws immediately", trace.draws == 2 && trace.appends == 4);
        check("both buffers double in-frame", sg_query_buffer_size(stream.state->vertices) == 8 * sizeof(ShaderVertex)
            && sg_query_buffer_size(stream.state->indices) == 12 * sizeof(uint32_t));
        check("old buffers queued and still valid", pendingDestroys.size() == 2
            && sg_query_buffer_state(stream.vertices) == SG_RESOURCESTATE_VALID
            && sg_query_buffer_state(stream.indices) == SG_RESOURCESTATE_VALID);
        check("replacement starts empty without re-upload", trace.vertexOffset == 0 && trace.indexOffset == 0);
        // Separately flushed target in the same device frame uses the replacement.
        sg_end_pass();
        beginPass();
        executeDeferredShaderDraw(quad);
        check("captured handles use shared current stream across passes", trace.draws == 3
            && trace.vertexOffset == 4 * static_cast<int>(sizeof(ShaderVertex)));
        sg_end_pass();
        sg_disable_stats();
        commit();
        check("old buffers destroyed after commit", sg_query_buffer_state(stream.vertices) == SG_RESOURCESTATE_INVALID
            && sg_query_buffer_state(stream.indices) == SG_RESOURCESTATE_INVALID);
        check("new-frame pre-check ignores stale cursor with stats disabled",
            shaderStreamAppendFits(stream.state->vertices, 8 * sizeof(ShaderVertex)));
        // Another window's commit must not invalidate an idle target's snapshot.
        beginPass();
        executeDeferredShaderDraw(quad);
        check("captured draw still works after another window commits", trace.draws == 4 && trace.vertexOffset == 0);
        sg_end_pass();
        commit();
        sg_enable_stats();
    }
    commit();
    for (bool indexOnly : {false, true}) {
        trace = {};
        Stream stream(indexOnly ? 16 : 4, indexOnly ? 6 : 24, pip);
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        executeDeferredShaderDraw(quad);
        executeDeferredShaderDraw(quad);
        check("one-buffer growth preserves the other append cursor", trace.draws == 2
            && (indexOnly ? trace.vertexOffset == 4 * static_cast<int>(sizeof(ShaderVertex))
                          : trace.indexOffset == 6 * static_cast<int>(sizeof(uint32_t))));
        sg_end_pass();
        commit();
    }
    commit();
    {
        trace = {};
        Stream stream(65536, 65536, pip);
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        for (int i = 0; i < 12000; ++i) executeDeferredShaderDraw(quad);
        check("12000 rectangles fully draw in first frame", trace.draws == 12000
            && trace.relative && trace.offsets);
        sg_end_pass();
        commit();
    }
    commit();
    for (auto type : {PrimitiveType::Triangles, PrimitiveType::Quads, PrimitiveType::TriangleStrip}) {
        trace = {};
        Stream stream(65536, 65536, pip);
        auto large = stream.capture(70000, type);
        beginPass();
        executeDeferredShaderDraw(large);
        check("first draw past 65536 vertices retains uint32 indices",
            trace.draws == 1 && trace.indices.back() == 69999);
        if (type == PrimitiveType::TriangleStrip) {
            check("triangle strip alternating winding preserved", trace.indices.size() >= 6 && trace.indices[0] == 0
                && trace.indices[1] == 1 && trace.indices[2] == 2 && trace.indices[3] == 2
                && trace.indices[4] == 1 && trace.indices[5] == 3);
        }
        executeDeferredShaderDraw(stream.capture(4, PrimitiveType::Quads));
        check("frame total past 65536 uses draw-relative vertex offset",
            trace.draws == 2 && trace.vertexOffset == 70000 * static_cast<int>(sizeof(ShaderVertex))
            && trace.relative && trace.offsets && trace.indices.back() == 3);
        sg_end_pass();
        commit();
        // A later sudden jump needs repeated doubling, without a warm-up frame.
        beginPass();
        executeDeferredShaderDraw(stream.capture(300000, type));
        check("later sudden growth draws immediately", trace.draws == 3 && trace.indices.back() == 299999);
        sg_end_pass();
        commit();
    }
    commit();
    {
        trace = {};
        DeferredShaderDraw captured;
        {
            Stream stream(3, 3, pip);
            captured = stream.capture(3, PrimitiveType::Triangles);
        }
        beginPass();
        executeDeferredShaderDraw(captured);
        executeDeferredShaderDraw(captured);
        check("captured stream grows after owner destruction", trace.draws == 2 && trace.appends == 4);
        sg_end_pass();
        commit();
    }
    commit();
    {
        trace = {};
        Stream stream(4, 6, pip);
        // Exhaust actual sokol buffer slots, without requesting enormous memory.
        std::vector<sg_buffer> held;
        for (;;) {
            const auto buffer = sg_alloc_buffer();
            if (!buffer.id) break;
            held.push_back(buffer);
        }
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass();
        executeDeferredShaderDraw(quad);
        // Permit exactly one allocation: vertex replacement succeeds, index
        // replacement fails, and neither live stream may change.
        sg_dealloc_buffer(held.back());
        held.pop_back();
        executeDeferredShaderDraw(quad);
        executeDeferredShaderDraw(quad);
        check("partial replacement allocation is retired", pendingDestroys.size() == 1
            && sg_query_buffer_state(pendingDestroys.front()) == SG_RESOURCESTATE_VALID);
        check("allocation failure skips without appending or poisoning", trace.draws == 1 && trace.appends == 2
            && allocationFailures == 2 && !sg_query_buffer_overflow(stream.vertices)
            && !sg_query_buffer_overflow(stream.indices));
        check("failed replacement preserves live handles", stream.state->vertices.id == stream.vertices.id
            && stream.state->indices.id == stream.indices.id);
        for (auto buffer : held) sg_dealloc_buffer(buffer);
        executeDeferredShaderDraw(quad);
        check("stream recovers in same frame when allocation succeeds", trace.draws == 2 && trace.appends == 4);
        sg_end_pass();
        commit();
        const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
        check("oversized byte count rejected before narrowing", ShaderStreamState::capacityFor({}, representable + 1) == 0);
        check("doubling at signed size boundary stays representable", ShaderStreamState::capacityFor({}, representable) == representable);
    }
    commit();
    {
        ShaderStreamState state;
        const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
        state.vertices = makeShaderStreamBuffer(representable, false);
        state.indices = makeShaderStreamBuffer(16, true);
        const auto previous = state.vertices;
        // The dummy backend allocates no GPU storage. Put its cursor at the
        // actual signed boundary to check cumulative accounting without a
        // multi-gigabyte upload or a wall-time assertion.
        auto* boundaryBuffer = _sg_buffer_at(previous.id);
        boundaryBuffer->cmn.append_frame_index = shaderStreamFrame();
        boundaryBuffer->cmn.append_pos = static_cast<int>(representable - 4);
        check("cumulative signed boundary starts a fresh buffer", state.prepare(8, 4)
            && state.vertices.id != previous.id && sg_query_buffer_info(state.vertices).append_pos == 0
            && sg_query_buffer_size(state.vertices) == representable);
    }
    commit();
}
} // namespace

namespace trussc::internal {
// This dummy-backend unit has no Runtime; supply its process predicate.
void deferGpuDestroy(sg_buffer buffer) noexcept {
    if (buffer.id) pendingDestroys.push_back(buffer);
}
sg_buffer growShaderStreamBuffer(size_t bytes, bool index) {
    return makeShaderStreamBuffer(bytes, index);
}
void reportShaderStreamFailure(size_t) noexcept {
    ++allocationFailures;
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
