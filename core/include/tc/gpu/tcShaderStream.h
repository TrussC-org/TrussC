#pragma once

// Custom Shader stream accounting and replay, shared by swapchain and FBO draws.
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>
#ifndef SOKOL_GFX_INCLUDED
#include "../../sokol/sokol_gfx.h"
#endif
#include "tcGpuDestroyQueue.h"

namespace trussc {
namespace internal { struct PipelineGeneration; }

// ---------------------------------------------------------------------------
// Standard vertex format for shader drawing
// ---------------------------------------------------------------------------
struct ShaderVertex {
    float x, y, z;      // position
    float u, v;         // texcoord
    float r, g, b, a;   // color
};

// Primitive types
enum class PrimitiveType {
    Points,
    Lines,
    LineStrip,
    Triangles,
    TriangleStrip,
    Quads
};

namespace internal {

// sokol publishes the completed frame index at commit, even with stats disabled.
// Its current append frame starts at 1 and advances on each sg_commit(), including
// commits by other windows; an app/window frame counter would not match it.
inline uint32_t shaderStreamFrame() {
    return sg_query_stats().prev_frame.frame_index + 1;
}

inline bool shaderStreamAppendFits(sg_buffer buffer, size_t bytes) {
    const auto info = sg_query_buffer_info(buffer);
    const size_t pos = info.append_frame_index == shaderStreamFrame()
        ? static_cast<size_t>(info.append_pos) : 0;
    const size_t capacity = sg_query_buffer_size(buffer);
    // Stream uploads here are multiples of four. Avoid addition overflow.
    return sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID
        && pos <= capacity && bytes <= capacity - pos;
}

inline sg_buffer makeShaderStreamBuffer(size_t bytes, bool index) {
    sg_buffer_desc desc = {};
    desc.size = bytes;
    desc.usage.stream_update = true;
    desc.usage.index_buffer = index;
    desc.label = index ? "tc_shader_indices" : "tc_shader_vertices";
    return sg_make_buffer(&desc);
}

// Growth allocations consolidate immediate backend errors into the once-per-run
// Shader diagnostic. Both functions live in the core for host/guest sharing.
sg_buffer growShaderStreamBuffer(size_t bytes, bool index);
void reportShaderStreamFailure(size_t vertices) noexcept;

// Replay owns the live buffers, even after the Shader is moved or destroyed.
// Each replacement starts an empty append stream. Previously issued draws keep
// their old buffers until submission; they need neither copying nor re-upload.
struct ShaderStreamState {
    sg_buffer vertices = {};
    sg_buffer indices = {};

    ~ShaderStreamState() {
        deferGpuDestroy(vertices);
        deferGpuDestroy(indices);
    }

    ShaderStreamState() = default;
    ShaderStreamState(const ShaderStreamState&) = delete;
    ShaderStreamState& operator=(const ShaderStreamState&) = delete;

    static size_t capacityFor(sg_buffer buffer, size_t bytes) {
        // Bound (representation): buffer bytes are limited to INT_MAX rounded down to four-byte alignment for sokol signed sizes/cursors; oversized byte requests return 0, cumulative overflow starts a fresh buffer.
        const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
        if (bytes > representable) return 0;
        const auto info = sg_query_buffer_info(buffer);
        const size_t pos = info.append_frame_index == shaderStreamFrame()
            ? static_cast<size_t>(info.append_pos) : 0;
        // If cumulative appends cannot be represented, start a fresh buffer
        // fitting this draw instead; there is no per-frame geometry cap.
        const size_t required = pos <= representable - bytes ? pos + bytes : bytes;
        size_t capacity = sg_query_buffer_size(buffer);
        if (!capacity) capacity = 4;
        while (capacity < required) {
            capacity = capacity > representable / 2 ? representable : capacity * 2;
        }
        return capacity;
    }

    bool prepare(size_t vertexBytes, size_t indexBytes) {
        const bool growVertices = !shaderStreamAppendFits(vertices, vertexBytes);
        const bool growIndices = !shaderStreamAppendFits(indices, indexBytes);
        if (!growVertices && !growIndices) return true;
        const size_t vsize = capacityFor(vertices, vertexBytes);
        const size_t isize = capacityFor(indices, indexBytes);
        if (!vsize || !isize) return false;
        const sg_buffer vbuf = growVertices ? growShaderStreamBuffer(vsize, false) : vertices;
        const sg_buffer ibuf = growIndices ? growShaderStreamBuffer(isize, true) : indices;
        if (sg_query_buffer_state(vbuf) != SG_RESOURCESTATE_VALID
                || sg_query_buffer_state(ibuf) != SG_RESOURCESTATE_VALID) {
            if (growVertices) deferGpuDestroy(vbuf);
            if (growIndices) deferGpuDestroy(ibuf);
            return false;
        }
        if (growVertices) { deferGpuDestroy(vertices); vertices = vbuf; }
        if (growIndices) { deferGpuDestroy(indices); indices = ibuf; }
        return true;
    }
};

// ---------------------------------------------------------------------------
// Deferred shader draw - for proper draw ordering
// ---------------------------------------------------------------------------
// A fully-resolved shader draw, captured at Shader::submitVertices() time and
// replayed later (same pattern as PbrDrawCommand in tcMeshPbrPipeline.h).
//
// Capture contract: the flush must NEVER touch the Shader object — a
// scope-local Shader destructed before present() is legal. Everything the
// replay needs is snapshotted here at submission time:
//   - pipeline resolved for the render target that was current at submission
//   - bindings (texture view/sampler pairs and non-stream buffer handles)
//   - shared stream owner, which selects/grows append buffers at replay
//   - raw uniform-block bytes per slot
//   - vertex data
// The captured sg handles stay valid until the flush because Shader routes
// its resource destruction through internal::deferGpuDestroy()
// (tcGpuDestroyQueue.h), drained after sg_commit().
//
// NOTE: the per-draw heap snapshots (uniforms/vertices) churn the allocator
// every frame; a pooled arena is a possible future optimization.
struct DeferredShaderDraw {
    int layerId = 0;                    // sokol_gl layer this draw follows
    std::shared_ptr<PipelineGeneration> pipelineGeneration;
    sg_pipeline pipeline = {};          // resolved for the target at submission
    sg_bindings bindings = {};          // buffers + views/samplers snapshot
    std::vector<std::pair<int, std::vector<uint8_t>>> uniforms;  // slot -> block bytes
    std::vector<ShaderVertex> vertices; // vertex data
    PrimitiveType type = PrimitiveType::Triangles;
    std::shared_ptr<ShaderStreamState> stream;
};

// Replay a captured shader draw on the GPU. Self-contained: uses captured
// resources and accounting, never a live Shader. RenderTarget::flushLayers()
// consumes these snapshots in the same order on every drawing target.
inline void executeDeferredShaderDraw(const DeferredShaderDraw& d) {
    if (d.vertices.empty()) return;

    const size_t count = d.vertices.size();
    try {
    // Bound (representation): vertex/index bytes must fit four-byte-aligned INT_MAX for sokol signed sizes/cursors and draw counts; exceeding either skips this draw and reports the once-per-run Error.
    const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
    if (count > representable / sizeof(ShaderVertex)) {
        reportShaderStreamFailure(count);
        return;
    }
    size_t indexCount = 0;
    if (d.type == PrimitiveType::Quads) indexCount = (count / 4) * 6;
    else if (d.type == PrimitiveType::TriangleStrip && count >= 3) indexCount = (count - 2) * 3;
    else if (d.type == PrimitiveType::Triangles) indexCount = count;
    if (indexCount == 0) return;
    if (indexCount > representable / sizeof(uint32_t)) {
        reportShaderStreamFailure(count);
        return;
    }

    const size_t vertexBytes = count * sizeof(ShaderVertex);
    const size_t indexBytes = indexCount * sizeof(uint32_t);
    sg_bindings bind = d.bindings;
    if (d.stream) {
        if (!d.stream->prepare(vertexBytes, indexBytes)) {
            reportShaderStreamFailure(count);
            return;
        }
        bind.vertex_buffers[0] = d.stream->vertices;
        bind.index_buffer = d.stream->indices;
    } else if (!shaderStreamAppendFits(bind.vertex_buffers[0], vertexBytes)
            || !shaderStreamAppendFits(bind.index_buffer, indexBytes)) {
        reportShaderStreamFailure(count);
        return;
    }

    // Generate triangle indices
    std::vector<uint32_t> indices;

    if (d.type == PrimitiveType::Quads) {
        size_t numQuads = count / 4;
        indices.reserve(numQuads * 6);
        for (size_t i = 0; i < numQuads; i++) {
            uint32_t base = static_cast<uint32_t>(i * 4);
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
            indices.push_back(base + 0);
            indices.push_back(base + 2);
            indices.push_back(base + 3);
        }
    } else if (d.type == PrimitiveType::TriangleStrip) {
        if (count >= 3) {
            indices.reserve((count - 2) * 3);
            for (size_t i = 0; i < count - 2; i++) {
                const uint32_t base = static_cast<uint32_t>(i);
                if (i % 2 == 0) {
                    indices.push_back(base);
                    indices.push_back(base + 1);
                    indices.push_back(base + 2);
                } else {
                    indices.push_back(base + 1);
                    indices.push_back(base);
                    indices.push_back(base + 2);
                }
            }
        }
    } else if (d.type == PrimitiveType::Triangles) {
        indices.reserve(count);
        for (size_t i = 0; i < count; i++) {
            indices.push_back(static_cast<uint32_t>(i));
        }
    }

    sg_apply_pipeline(d.pipeline);
    for (const auto& [slot, data] : d.uniforms) {
        sg_range range = { data.data(), data.size() };
        sg_apply_uniforms(slot, &range);
    }

    sg_range range = { d.vertices.data(), vertexBytes };
    bind.vertex_buffer_offsets[0] = sg_append_buffer(bind.vertex_buffers[0], &range);
    sg_range idxRange = { indices.data(), indexBytes };
    const int indexOffset = sg_append_buffer(bind.index_buffer, &idxRange);
    // Indices stay relative to this draw, including when the frame has already
    // appended more than 65536 vertices.
    sg_apply_bindings(&bind);
    const int baseElement = indexOffset / static_cast<int>(sizeof(uint32_t));
    sg_draw(baseElement, static_cast<int>(indices.size()), 1);
    } catch (const std::bad_alloc&) {
        reportShaderStreamFailure(count);
    } catch (const std::length_error&) {
        reportShaderStreamFailure(count);
    }
}

} // namespace internal
} // namespace trussc
