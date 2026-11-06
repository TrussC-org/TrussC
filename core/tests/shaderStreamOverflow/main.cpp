#define SOKOL_IMPL
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL
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
int validationErrors = 0;
void logger(const char*, uint32_t level, uint32_t item, const char* message,
            uint32_t, const char*, void*) {
    if (level <= 1 && item != SG_LOGITEM_BUFFER_POOL_EXHAUSTED) {
        ++validationErrors;
        std::printf("validation item %u: %s\n", item, message ? message : "");
    }
}
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

void written(const sg_write_buffer_desc* desc, void* user) {
    auto& t = *static_cast<Trace*>(user);
    ++t.appends;
    if (sg_query_buffer_desc(desc->dst.buffer).usage.index_buffer) {
        t.indexOffset = static_cast<int>(desc->dst.offset);
        const auto* data = static_cast<const uint32_t*>(desc->src.data.ptr);
        t.indices.assign(data, data + desc->src.data.size / sizeof(uint32_t));
        t.relative &= t.indices.front() == 0;
    } else t.vertexOffset = static_cast<int>(desc->dst.offset);
}

void bound(const sg_bindings* bindings, void* user) {
    static_cast<Trace*>(user)->bindings = *bindings;
}

void drawn(int base, int count, int instances, void* user) {
    auto& t = *static_cast<Trace*>(user);
    ++t.draws;
    t.offsets &= t.bindings.vertex_buffer_offsets[0] == t.vertexOffset
        && base == 0 && t.bindings.index_buffer_offset == t.indexOffset
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
        const auto first = stream.state->vertices;
        check("exact capacity fits", trace.draws == 1 && trace.appends == 2);
        check("quad winding preserved", trace.indices == std::vector<uint32_t>({0,1,2,0,2,3}));
        executeDeferredShaderDraw(quad);
        check("second flush has distinct transient storage", trace.draws == 2 && stream.state->vertices.id != first.id);
        check("previous flush remains valid", sg_query_buffer_state(first) == SG_RESOURCESTATE_VALID);
        sg_end_pass();
        beginPass();
        executeDeferredShaderDraw(quad);
        check("separate pass gets a third buffer pair", trace.draws == 3 && stream.state->flushBuffers.size() == 3);
        sg_end_pass();
        sg_disable_stats(); commit();
        beginPass(); executeDeferredShaderDraw(quad); sg_end_pass();
        check("next device frame reuses first pair with stats disabled", stream.state->vertices.id == first.id);
        commit(); sg_enable_stats();
    }
    commit();
    {
        trace = {};
        Stream stream(4, 6, pip);
        std::vector<DeferredShaderDraw> draws;
        for (int i = 0; i < 12000; ++i) draws.push_back(stream.capture(4, PrimitiveType::Quads));
        beginPass();
        prepareDeferredShaderDraws(draws);
        const auto buffers = stream.state->flushBuffers.size();
        for (auto& draw : draws) executeDeferredShaderDraw(draw);
        check("12000 rectangles share one flush buffer pair", trace.draws == 12000 && buffers == 1);
        check("batch offsets cover all vertices and indices", draws.back().uploadedBindings.vertex_buffer_offsets[0] == 11999 * 4 * int(sizeof(ShaderVertex))
            && draws.back().uploadedBindings.index_buffer_offset == 11999 * 6 * int(sizeof(uint32_t)));
        sg_end_pass(); commit();
        // Batch writes precede drawing, so trace.offsets is checked below only
        // for one-draw flushes where the last write belongs to that draw.
        trace = {};
    }
    commit();
    {
        trace = {};
        Stream stream(4, 6, pip);
        auto draw = stream.capture(4, PrimitiveType::Quads);
        draw.stream.reset();
        beginPass(); executeDeferredShaderDraw(draw); sg_end_pass();
        check("custom-buffer replay gets transient storage", trace.draws == 1 && trace.relative && trace.offsets);
        commit();
    }
    commit();
    for (auto type : {PrimitiveType::Triangles, PrimitiveType::Quads, PrimitiveType::TriangleStrip}) {
        trace = {};
        Stream stream(4, 6, pip);
        beginPass(); executeDeferredShaderDraw(stream.capture(70000, type));
        check("first large draw keeps uint32 indices", trace.draws == 1 && trace.indices.back() == 69999);
        if (type == PrimitiveType::TriangleStrip) check("strip winding preserved", trace.indices[3] == 2 && trace.indices[4] == 1 && trace.indices[5] == 3);
        sg_end_pass(); commit();
        beginPass(); executeDeferredShaderDraw(stream.capture(300000, type));
        check("later growth draws immediately", trace.draws == 2 && trace.indices.back() == 299999);
        sg_end_pass(); commit();
    }
    commit();
    {
        trace = {};
        DeferredShaderDraw captured;
        { Stream stream(3, 3, pip); captured = stream.capture(3, PrimitiveType::Triangles); }
        beginPass(); executeDeferredShaderDraw(captured); executeDeferredShaderDraw(captured);
        check("captured stream survives its owner", trace.draws == 2 && trace.appends == 4);
        sg_end_pass(); commit();
    }
    commit();
    {
        trace = {};
        Stream stream(4, 6, pip);
        auto quad = stream.capture(4, PrimitiveType::Quads);
        beginPass(); executeDeferredShaderDraw(quad);
        std::vector<sg_buffer> held;
        for (;;) { const auto buffer = sg_alloc_buffer(); if (!buffer.id) break; held.push_back(buffer); }
        sg_dealloc_buffer(held.back()); held.pop_back();
        const auto first = stream.state->vertices;
        executeDeferredShaderDraw(quad);
        check("partial failure skips draw and retains previous storage", trace.draws == 1 && allocationFailures == 1 && stream.state->vertices.id == first.id);
        for (auto buffer : held) sg_dealloc_buffer(buffer);
        executeDeferredShaderDraw(quad);
        check("failed flush can recover in the same frame", trace.draws == 2);
        sg_end_pass(); commit();
        const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
        check("oversized byte count is rejected", ShaderStreamState::capacityFor({}, representable + 1) == 0);
        check("doubling stays representable", ShaderStreamState::capacityFor({}, representable) == representable);
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
    desc.logger.func = logger;
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
    hooks.write_buffer_transient = written;
    hooks.apply_bindings = bound;
    hooks.draw = drawn;
    sg_install_trace_hooks(&hooks);
    tests(pipeline, trace);
    check("all captured indices and offsets valid", trace.relative && trace.offsets);
    check("no sokol validation errors", validationErrors == 0);
    sg_destroy_pipeline(pipeline);
    sg_destroy_shader(shader);
    sg_shutdown();
    return failures ? 1 : 0;
}
