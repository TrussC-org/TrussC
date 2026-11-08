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
#include "../app/tcExit.h"

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
// Its current device frame starts at 1 and advances on each sg_commit(), including
// commits by other windows; an app/window frame counter would not match it.
inline uint32_t shaderStreamFrame() {
    return sg_query_stats().prev_frame.frame_index + 1;
}

inline bool shaderStreamFits(sg_buffer buffer, size_t bytes) {
    return sg_query_buffer_state(buffer) == SG_RESOURCESTATE_VALID
        && bytes <= sg_query_buffer_size(buffer);
}

inline sg_buffer makeShaderStreamBuffer(size_t bytes, bool index) {
    sg_buffer_desc desc = {};
    desc.size = bytes;
    desc.usage.write_transient = true;
    desc.usage.index_buffer = index;
    desc.label = index ? "tc_shader_indices" : "tc_shader_vertices";
    return sg_make_buffer(&desc);
}

// Growth allocations consolidate immediate backend errors into the once-per-run
// Shader diagnostic. Both functions live in the core for host/guest sharing.
sg_buffer growShaderStreamBuffer(size_t bytes, bool index);
void reportShaderStreamFailure(size_t vertices) noexcept;

// Replay owns a pair of write-transient buffers per flush. All draws in a
// flush upload before any bindings use them; later flushes get a distinct pair.
struct ShaderStreamState {
    sg_buffer vertices = {};
    sg_buffer indices = {};
    struct Buffers { sg_buffer vertices = {}; sg_buffer indices = {}; };
    std::vector<Buffers> flushBuffers;
    uint32_t frame = 0;
    size_t next = 0;

    ~ShaderStreamState() {
        if (flushBuffers.empty()) {
            deferGpuDestroy(vertices);
            deferGpuDestroy(indices);
        } else for (const auto& buffers : flushBuffers) {
            deferGpuDestroy(buffers.vertices);
            deferGpuDestroy(buffers.indices);
        }
    }
    ShaderStreamState() = default;
    ShaderStreamState(const ShaderStreamState&) = delete;
    ShaderStreamState& operator=(const ShaderStreamState&) = delete;

    static size_t capacityFor(sg_buffer buffer, size_t bytes) {
        // Bound (abi): sokol buffer sizes and draw offsets must fit signed int.
        const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
        if (bytes > representable) return 0;
        size_t capacity = sg_query_buffer_size(buffer);
        if (!capacity) capacity = 4;
        while (capacity < bytes) capacity = capacity > representable / 2 ? representable : capacity * 2;
        return capacity;
    }

    bool prepare(size_t vertexBytes, size_t indexBytes) {
        if (frame != shaderStreamFrame()) { frame = shaderStreamFrame(); next = 0; }
        if (flushBuffers.empty()) flushBuffers.push_back({vertices, indices});
        if (next == flushBuffers.size()) flushBuffers.push_back({});
        auto& buffers = flushBuffers[next];
        const bool growVertices = !shaderStreamFits(buffers.vertices, vertexBytes);
        const bool growIndices = !shaderStreamFits(buffers.indices, indexBytes);
        const size_t vsize = capacityFor(buffers.vertices, vertexBytes);
        const size_t isize = capacityFor(buffers.indices, indexBytes);
        if (!vsize || !isize) return false;
        const sg_buffer vbuf = growVertices ? growShaderStreamBuffer(vsize, false) : buffers.vertices;
        const sg_buffer ibuf = growIndices ? growShaderStreamBuffer(isize, true) : buffers.indices;
        if (sg_query_buffer_state(vbuf) != SG_RESOURCESTATE_VALID || sg_query_buffer_state(ibuf) != SG_RESOURCESTATE_VALID) {
            if (growVertices) deferGpuDestroy(vbuf);
            if (growIndices) deferGpuDestroy(ibuf);
            return false;
        }
        if (growVertices) { deferGpuDestroy(buffers.vertices); buffers.vertices = vbuf; }
        if (growIndices) { deferGpuDestroy(buffers.indices); buffers.indices = ibuf; }
        vertices = vbuf; indices = ibuf;
        ++next;
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
//   - shared stream owner, which selects/grows flush buffers at replay
//   - raw uniform-block bytes per slot
//   - vertex data
// The captured sg handles stay valid until the flush because Shader routes
// its resource destruction through internal::deferGpuDestroy()
// (tcGpuDestroyQueue.h), drained after sg_commit().
//
// NOTE: the per-draw heap snapshots (uniforms/vertices) churn the allocator
// every frame; a pooled arena is a possible future optimization.
struct DeferredShaderDraw {
    int vertexCount = 0;                // non-indexed draw; zero selects the stream path
    int instanceCount = 1;
    int layerId = 0;                    // sokol_gl layer this draw follows
    std::shared_ptr<PipelineGeneration> pipelineGeneration;
    sg_pipeline pipeline = {};          // resolved for the target at submission
    sg_bindings bindings = {};          // buffers + views/samplers snapshot
    std::vector<std::pair<int, std::vector<uint8_t>>> uniforms;  // slot -> block bytes
    std::vector<ShaderVertex> vertices; // vertex data
    PrimitiveType type = PrimitiveType::Triangles;
    std::shared_ptr<ShaderStreamState> stream;
    sg_bindings uploadedBindings = {};
    int uploadedIndexCount = 0;
    bool uploadPrepared = false;
};

inline std::vector<uint32_t> shaderDrawIndices(const DeferredShaderDraw& d) {
    const size_t count = d.vertices.size();
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

    return indices;
}

// Stage each stream's entire flush before its first draw binds the buffers.
// Offsets are local to this flush; captured handles remain valid across FBOs.
inline void prepareDeferredShaderDraws(std::vector<DeferredShaderDraw>& draws) {
    try {
        const size_t representable = static_cast<size_t>((std::numeric_limits<int>::max)()) & ~size_t(3);
        for (size_t first = 0; first < draws.size(); ++first) {
            auto& owner = draws[first];
            if (owner.uploadPrepared || !owner.stream) continue;
            size_t vertexBytes = 0, indexBytes = 0;
            std::vector<std::pair<DeferredShaderDraw*, std::vector<uint32_t>>> batch;
            auto uploadBatch = [&] {
                if (batch.empty()) return;
                if (!owner.stream->prepare(vertexBytes, indexBytes)) { reportShaderStreamFailure(owner.vertices.size()); return; }
                size_t vertexOffset = 0, indexOffset = 0;
                for (auto& entry : batch) {
                    auto& d = *entry.first;
                    const size_t vbytes = d.vertices.size() * sizeof(ShaderVertex);
                    const size_t ibytes = entry.second.size() * sizeof(uint32_t);
                    sg_write_buffer_desc write = {};
                    write.src.data = {d.vertices.data(), vbytes};
                    write.dst.buffer = owner.stream->vertices;
                    write.dst.offset = vertexOffset;
                    sg_write_buffer_transient(&write);
                    write.src.data = {entry.second.data(), ibytes};
                    write.dst.buffer = owner.stream->indices;
                    write.dst.offset = indexOffset;
                    sg_write_buffer_transient(&write);
                    d.uploadedBindings = d.bindings;
                    d.uploadedBindings.vertex_buffers[0] = owner.stream->vertices;
                    d.uploadedBindings.vertex_buffer_offsets[0] = static_cast<int>(vertexOffset);
                    d.uploadedBindings.index_buffer = owner.stream->indices;
                    d.uploadedBindings.index_buffer_offset = static_cast<int>(indexOffset);
                    d.uploadedIndexCount = static_cast<int>(entry.second.size());
                    vertexOffset += vbytes; indexOffset += ibytes;
                }
            };
            for (size_t i = first; i < draws.size(); ++i) {
                auto& d = draws[i];
                if (d.stream != owner.stream) continue;
                d.uploadPrepared = true;
                if (d.vertices.size() > representable / sizeof(ShaderVertex)) { reportShaderStreamFailure(d.vertices.size()); continue; }
                auto indices = shaderDrawIndices(d);
                if (indices.empty()) continue;
                const size_t vbytes = d.vertices.size() * sizeof(ShaderVertex);
                const size_t ibytes = indices.size() * sizeof(uint32_t);
                if (vbytes > representable - vertexBytes || ibytes > representable - indexBytes) {
                    uploadBatch();
                    batch.clear(); vertexBytes = indexBytes = 0;
                }
                vertexBytes += vbytes; indexBytes += ibytes;
                batch.emplace_back(&d, std::move(indices));
            }
            uploadBatch();
        }
    } catch (const std::bad_alloc&) {
        for (auto& d : draws) { d.uploadPrepared = true; d.uploadedIndexCount = 0; }
        reportShaderStreamFailure(draws.empty() ? 0 : draws.front().vertices.size());
    } catch (const std::length_error&) {
        for (auto& d : draws) { d.uploadPrepared = true; d.uploadedIndexCount = 0; }
        reportShaderStreamFailure(draws.empty() ? 0 : draws.front().vertices.size());
    }
}

inline void executeDeferredShaderDraw(const DeferredShaderDraw& d) {
    try {
        if (d.vertexCount > 0) {
            sg_apply_pipeline(d.pipeline);
            for (const auto& [slot, data] : d.uniforms) {
                const sg_range range = {data.data(), data.size()};
                sg_apply_uniforms(slot, &range);
            }
            sg_apply_bindings(&d.bindings);
            sg_draw(0, d.vertexCount, d.instanceCount);
            return;
        }
        if (!d.uploadPrepared) {
            // Standalone replay (unit tests and internal callers) is a one-draw flush.
            std::vector<DeferredShaderDraw> batch{d};
            // A custom Shader may provide its own initial buffers. This replay
            // still needs fresh transient storage once those buffers were used.
            if (!batch.front().stream) batch.front().stream = std::make_shared<ShaderStreamState>();
            prepareDeferredShaderDraws(batch);
            if (batch.front().uploadPrepared) executeDeferredShaderDraw(batch.front());
            return;
        }
        if (!d.uploadedIndexCount) return;
        sg_apply_pipeline(d.pipeline);
        for (const auto& [slot, data] : d.uniforms) {
            const sg_range range = {data.data(), data.size()};
            sg_apply_uniforms(slot, &range);
        }
        sg_apply_bindings(&d.uploadedBindings);
        sg_draw(0, d.uploadedIndexCount, 1);
    } catch (const std::bad_alloc&) {
        reportShaderStreamFailure(d.vertices.size());
    } catch (const std::length_error&) {
        reportShaderStreamFailure(d.vertices.size());
    }
}

} // namespace internal
} // namespace trussc
