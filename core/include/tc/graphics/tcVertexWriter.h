#pragma once

// =============================================================================
// tcVertexWriter.h - Vertex writing abstraction for shader integration
// =============================================================================
//
// This file defines the VertexWriter interface that allows draw functions
// to work with both sokol_gl (normal mode) and custom shaders.
//
// =============================================================================

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
struct DeferredShaderDraw {
    int layerId;                        // sokol_gl layer before this draw
    Shader* shader;                     // shader to use
    std::vector<ShaderVertex> vertices; // vertex data
    PrimitiveType type;                 // primitive type
};

// ---------------------------------------------------------------------------
// Global shader stack and vertex writers
// ---------------------------------------------------------------------------
namespace internal {
    inline std::vector<Shader*> shaderStack;
    inline SglWriter sglWriter;
    inline ShaderWriter shaderWriter;

    // sokol_gl layer management for proper draw ordering with shaders
    // Each pushShader() increments this, so post-shader draws go to a new layer
    inline int sglLayerNext = 0;

    // Deferred shader draws - executed in present() for proper ordering
    inline std::vector<DeferredShaderDraw> deferredShaderDraws;

    inline Shader* getCurrentShader() {
        return shaderStack.empty() ? nullptr : shaderStack.back();
    }

    inline bool isShaderActive() {
        return !shaderStack.empty();
    }

    inline void resetShaderStack() {
        shaderStack.clear();
    }

    inline VertexWriter& getActiveWriter() {
        return isShaderActive() ? static_cast<VertexWriter&>(shaderWriter)
                                : static_cast<VertexWriter&>(sglWriter);
    }
}

} // namespace trussc
