#pragma once

// =============================================================================
// tcVertexWriter.h - Vertex writing abstraction for shader integration
// =============================================================================
//
// This file defines the VertexWriter interface that allows draw functions
// to work with both sokol_gl (normal mode) and custom shaders.
//
// =============================================================================

#include <cstdint>
#include <utility>
#include <vector>

namespace trussc {

// Forward declaration
class Shader;

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

// The writer classes and shader-stack plumbing below are framework
// internals. Draw functions reach them via internal::getActiveWriter();
// ShaderVertex / PrimitiveType above stay public (shader-authoring API).
namespace internal {

// ---------------------------------------------------------------------------
// VertexWriter interface - abstraction for sokol_gl vs shader drawing
// ---------------------------------------------------------------------------
class VertexWriter {
public:
    virtual ~VertexWriter() = default;
    virtual void begin(PrimitiveType type) = 0;
    virtual void vertex(float x, float y, float z) = 0;
    virtual void texCoord(float u, float v) = 0;
    virtual void color(float r, float g, float b, float a) = 0;
    virtual void end() = 0;
};

// ---------------------------------------------------------------------------
// SglWriter - writes to sokol_gl (default mode)
// ---------------------------------------------------------------------------
class SglWriter : public VertexWriter {
public:
    void begin(PrimitiveType type) override {
        switch (type) {
            case PrimitiveType::Points:        sgl_begin_points(); break;
            case PrimitiveType::Lines:         sgl_begin_lines(); break;
            case PrimitiveType::LineStrip:     sgl_begin_line_strip(); break;
            case PrimitiveType::Triangles:     sgl_begin_triangles(); break;
            case PrimitiveType::TriangleStrip: sgl_begin_triangle_strip(); break;
            case PrimitiveType::Quads:         sgl_begin_quads(); break;
        }
    }

    void vertex(float x, float y, float z) override {
        sgl_v3f(x, y, z);
    }

    void texCoord(float u, float v) override {
        sgl_t2f(u, v);
    }

    void color(float r, float g, float b, float a) override {
        sgl_c4f(r, g, b, a);
    }

    void end() override {
        sgl_end();
    }
};

// ---------------------------------------------------------------------------
// ShaderWriter - writes to custom shader pipeline
// ---------------------------------------------------------------------------
class ShaderWriter : public VertexWriter {
public:
    void begin(PrimitiveType type) override {
        vertices.clear();
        currentType = type;
        currentU = 0;
        currentV = 0;
        currentR = 1;
        currentG = 1;
        currentB = 1;
        currentA = 1;
    }

    void vertex(float x, float y, float z) override {
        ShaderVertex v;
        v.x = x; v.y = y; v.z = z;
        v.u = currentU; v.v = currentV;
        v.r = currentR; v.g = currentG; v.b = currentB; v.a = currentA;
        vertices.push_back(v);
    }

    void texCoord(float u, float v) override {
        currentU = u;
        currentV = v;
    }

    void color(float r, float g, float b, float a) override {
        currentR = r;
        currentG = g;
        currentB = b;
        currentA = a;
    }

    void end() override;  // Implemented in tcShader.h (needs Shader class)

    std::vector<ShaderVertex> vertices;
    PrimitiveType currentType = PrimitiveType::Triangles;

private:
    float currentU = 0, currentV = 0;
    float currentR = 1, currentG = 1, currentB = 1, currentA = 1;
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
};

// Replay a captured shader draw on the GPU. Self-contained: only reads the
// snapshot, never a live Shader. Used by flushDeferredShaderDraws()
// (swapchain, tcShader.h) and flushFboDeferredPbr() (FBO passes,
// tcMeshPbrPipeline.h).
inline void executeDeferredShaderDraw(const DeferredShaderDraw& d) {
    if (d.vertices.empty()) return;

    sg_apply_pipeline(d.pipeline);

    // Uniforms snapshotted at submission time (fixes last-write-wins: two
    // draws with different setUniform values keep their own values).
    for (const auto& [slot, data] : d.uniforms) {
        sg_range range = { data.data(), data.size() };
        sg_apply_uniforms(slot, &range);
    }

    // Append vertices to the captured stream buffer
    sg_bindings bind = d.bindings;
    sg_range range = { d.vertices.data(), d.vertices.size() * sizeof(ShaderVertex) };
    int vertexOffset = sg_append_buffer(bind.vertex_buffers[0], &range);
    if (vertexOffset < 0) {
        logWarning("Shader") << "Vertex buffer overflow, skipping draw";
        return;
    }

    // Generate triangle indices
    std::vector<uint16_t> indices;
    int count = (int)d.vertices.size();

    if (d.type == PrimitiveType::Quads) {
        int numQuads = count / 4;
        indices.reserve(numQuads * 6);
        for (int i = 0; i < numQuads; i++) {
            int base = i * 4;
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
            for (int i = 0; i < count - 2; i++) {
                if (i % 2 == 0) {
                    indices.push_back(i);
                    indices.push_back(i + 1);
                    indices.push_back(i + 2);
                } else {
                    indices.push_back(i + 1);
                    indices.push_back(i);
                    indices.push_back(i + 2);
                }
            }
        }
    } else if (d.type == PrimitiveType::Triangles) {
        indices.reserve(count);
        for (int i = 0; i < count; i++) {
            indices.push_back((uint16_t)i);
        }
    } else if (d.type == PrimitiveType::Points) {
        return;
    }

    // Adjust indices for vertex buffer offset
    int baseVertex = vertexOffset / sizeof(ShaderVertex);
    for (auto& idx : indices) {
        idx += baseVertex;
    }

    // Append to index buffer
    sg_range idxRange = { indices.data(), indices.size() * sizeof(uint16_t) };
    int indexOffset = sg_append_buffer(bind.index_buffer, &idxRange);
    if (indexOffset < 0) {
        logWarning("Shader") << "Index buffer overflow, skipping draw";
        return;
    }

    bind.vertex_buffer_offsets[0] = 0;
    sg_apply_bindings(&bind);

    // Draw
    int baseElement = indexOffset / sizeof(uint16_t);
    sg_draw(baseElement, (int)indices.size(), 1);
}

// ---------------------------------------------------------------------------
// Vertex writers (shared scratch — stateless singletons) and the per-window
// shader-stack / layer-counter / deferred-draw accessors.
// ---------------------------------------------------------------------------
    inline SglWriter sglWriter;
    inline ShaderWriter shaderWriter;

    // The shader stack, sokol_gl layer counter (sglLayerNext), and the deferred
    // swapchain/FBO draw queues are now PER-WINDOW: they live in WindowContext
    // (tc/app/tcWindowContext.h) and are reached through currentWindowContext().
    // They used to be process globals here — moved so no per-frame draw/record
    // state is shared between windows (each window ticks serially, populating
    // and draining its own queues within one tick).

    inline Shader* getCurrentShader() {
        auto& s = currentWindowContext().shaderStack;
        return s.empty() ? nullptr : s.back();
    }

    inline bool isShaderActive() {
        return !currentWindowContext().shaderStack.empty();
    }

    inline void resetShaderStack() {
        currentWindowContext().shaderStack.clear();
    }

    inline VertexWriter& getActiveWriter() {
        return isShaderActive() ? static_cast<VertexWriter&>(shaderWriter)
                                : static_cast<VertexWriter&>(sglWriter);
    }
}

} // namespace trussc
