#pragma once

// =============================================================================
// tcVertexWriter.h - Vertex writing abstraction for shader integration
// =============================================================================
//
// This file defines the VertexWriter interface that allows draw functions
// to work with both sokol_gl (normal mode) and custom shaders.
//
// =============================================================================

#include "../gpu/tcShaderStream.h"

#include <cstdint>
#include <utility>
#include <vector>

namespace trussc {

// Forward declaration
class Shader;

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
// Vertex writers (shared scratch — stateless singletons) and the per-window
// shader-stack / layer-counter / deferred-draw accessors.
// ---------------------------------------------------------------------------
    // A per-module copy is harmless: SglWriter has no state, and ShaderWriter's
    // vertices are scratch that one draw call fills and flushes (begin() to
    // end()) inside a single function.
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
