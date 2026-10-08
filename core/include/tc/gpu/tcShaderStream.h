#pragma once

// Custom Shader stream accounting and replay, shared by swapchain and FBO draws.
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>
#ifndef SOKOL_GFX_INCLUDED
#include "../../sokol/sokol_gfx.h"
#endif
#include "tcGpuDestroyQueue.h"

namespace trussc {

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

enum class ShaderStreamGrowth { None, Grown, Failed };

// Deferred draws own this accounting too, so replay can remember overflow
// without dereferencing a Shader that may already have been destroyed.
struct ShaderStreamState {
    uint32_t frame = 0;
    size_t vertexBytes = 0;
    size_t indexBytes = 0;
    bool overflow = false;

    ShaderStreamGrowth beginFrame(sg_buffer& vertices, sg_buffer& indices) {
        const uint32_t next = shaderStreamFrame();
        if (frame == next) return ShaderStreamGrowth::None;
        ShaderStreamGrowth result = ShaderStreamGrowth::None;
        if (overflow) {
            size_t vsize = sg_query_buffer_size(vertices);
            size_t isize = sg_query_buffer_size(indices);
            // Double both buffers; repeat for a frame (or a single draw) needing
            // more than 2x, so replaying the same workload fits the next frame.
            const size_t maxSize = static_cast<size_t>(std::numeric_limits<int>::max());
            do {
                // sokol stores buffer sizes and append offsets in signed ints.
                if (vsize == 0 || isize == 0 || vsize > maxSize / 2 || isize > maxSize / 2) {
                    result = ShaderStreamGrowth::Failed;
                    break;
                }
                vsize *= 2;
                isize *= 2;
            } while (vsize < vertexBytes || isize < indexBytes);
            if (result != ShaderStreamGrowth::Failed) {
                sg_buffer vbuf = makeShaderStreamBuffer(vsize, false);
                sg_buffer ibuf = makeShaderStreamBuffer(isize, true);
                if (sg_query_buffer_state(vbuf) == SG_RESOURCESTATE_VALID
                        && sg_query_buffer_state(ibuf) == SG_RESOURCESTATE_VALID) {
                    deferGpuDestroy(vertices);
                    deferGpuDestroy(indices);
                    vertices = vbuf;
                    indices = ibuf;
                    result = ShaderStreamGrowth::Grown;
                } else {
                    deferGpuDestroy(vbuf);
                    deferGpuDestroy(ibuf);
                    result = ShaderStreamGrowth::Failed;
                }
            }
        }
        frame = next;
        vertexBytes = indexBytes = 0;
        overflow = false;
        return result;
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
//   - bindings (vertex/index buffer handles + texture view/sampler pairs)
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
    sg_pipeline pipeline = {};          // resolved for the target at submission
    sg_bindings bindings = {};          // buffers + views/samplers snapshot
    std::vector<std::pair<int, std::vector<uint8_t>>> uniforms;  // slot -> block bytes
    std::vector<ShaderVertex> vertices; // vertex data
    PrimitiveType type = PrimitiveType::Triangles;
    std::shared_ptr<ShaderStreamState> stream;
};

// Replay a captured shader draw on the GPU. Self-contained: uses captured
// resources and accounting, never a live Shader. Used by flushDeferredShaderDraws()
// (swapchain, tcShader.h) and flushFboDeferredPbr() (FBO passes,
// tcMeshPbrPipeline.h).
inline void executeDeferredShaderDraw(const DeferredShaderDraw& d) {
    if (d.vertices.empty()) return;

    const size_t count = d.vertices.size();
    size_t indexCount = 0;
    if (d.type == PrimitiveType::Quads) indexCount = (count / 4) * 6;
    else if (d.type == PrimitiveType::TriangleStrip && count >= 3) indexCount = (count - 2) * 3;
    else if (d.type == PrimitiveType::Triangles) indexCount = count;
    if (indexCount == 0) return;

    const size_t vertexBytes = count * sizeof(ShaderVertex);
    const size_t indexBytes = indexCount * sizeof(uint32_t);
    if (d.stream) {
        // Include skipped draws in demand, across all FBO/swapchain flushes.
        d.stream->vertexBytes += vertexBytes;
        d.stream->indexBytes += indexBytes;
    }
    if (!shaderStreamAppendFits(d.bindings.vertex_buffers[0], vertexBytes)
            || !shaderStreamAppendFits(d.bindings.index_buffer, indexBytes)) {
        if (d.stream) d.stream->overflow = true;
        return;  // Neither buffer is appended: no Debug panic or poisoned cursor.
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

    sg_bindings bind = d.bindings;
    sg_range range = { d.vertices.data(), vertexBytes };
    bind.vertex_buffer_offsets[0] = sg_append_buffer(bind.vertex_buffers[0], &range);
    sg_range idxRange = { indices.data(), indexBytes };
    const int indexOffset = sg_append_buffer(bind.index_buffer, &idxRange);
    // Indices stay relative to this draw, including when the frame has already
    // appended more than 65536 vertices.
    sg_apply_bindings(&bind);
    const int baseElement = indexOffset / static_cast<int>(sizeof(uint32_t));
    sg_draw(baseElement, static_cast<int>(indices.size()), 1);
}

} // namespace internal
} // namespace trussc
