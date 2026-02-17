#pragma once

// =============================================================================
// tcShader.h - Shader system with draw integration
// =============================================================================
//
// Shader class that integrates with TrussC drawing system.
// Uses sokol-shdc compiled shaders for cross-platform support.
//
// Usage:
//   tc::Shader shader;
//   shader.load(my_shader_desc);  // sokol-shdc generated function
//
//   // Drawing with shader
//   pushShader(shader);
//   drawTriangle(100, 100, 200, 100, 150, 200);  // Uses shader!
//   drawRect(300, 100, 200, 150);
//   popShader();
//
//   drawCircle(400, 400, 50);  // Normal sokol_gl drawing
//
// =============================================================================

// Note: This file is included from TrussC.h after tcMath.h, tcLog.h, etc.
// Required types: Vec2, Vec3, Vec4, Color, sg_* are already available

#include <string>
#include <vector>
#include <unordered_map>
#include <cstring>

namespace trussc {

// ---------------------------------------------------------------------------
// Shader class
// ---------------------------------------------------------------------------
class Shader {
public:
    Shader() = default;
    virtual ~Shader() { clear(); }

    // Copy prohibited
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Move support
    Shader(Shader&& other) noexcept { moveFrom(std::move(other)); }
    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Loading
    // -------------------------------------------------------------------------

    // Load from sokol-shdc generated descriptor function
    bool load(const sg_shader_desc* (*descFn)(sg_backend)) {
        clear();

        sg_backend backend = sg_query_backend();
        const sg_shader_desc* desc = descFn(backend);
        if (!desc) {
            logError("Shader") << "Failed to get shader desc";
            return false;
        }

        shader = sg_make_shader(desc);
        if (sg_query_shader_state(shader) != SG_RESOURCESTATE_VALID) {
            logError("Shader") << "Failed to create shader";
            return false;
        }

        // Create pipeline with standard vertex layout
        sg_pipeline_desc pipDesc = createPipelineDesc();
        pipDesc.shader = shader;

        pipeline = sg_make_pipeline(&pipDesc);
        if (sg_query_pipeline_state(pipeline) != SG_RESOURCESTATE_VALID) {
            logError("Shader") << "Failed to create pipeline";
            sg_destroy_shader(shader);
            shader = {};
            return false;
        }

        // Create dynamic vertex buffer
        createVertexBuffer();

        loaded = true;
        return true;
    }

    void clear() {
        if (loaded) {
            if (indexBuffer.id) sg_destroy_buffer(indexBuffer);
            if (vertexBuffer.id) sg_destroy_buffer(vertexBuffer);
            if (pipeline.id) sg_destroy_pipeline(pipeline);
            if (shader.id) sg_destroy_shader(shader);
        }
        shader = {};
        pipeline = {};
        vertexBuffer = {};
        indexBuffer = {};
        loaded = false;
    }

    bool isLoaded() const { return loaded; }

    // -------------------------------------------------------------------------
    // Begin / End
    // -------------------------------------------------------------------------

    void begin() {
        if (!loaded) return;

        // Push to stack
        internal::shaderStack.push_back(this);

        // Increment layer for post-shader sokol_gl draws
        // (shader draws will be deferred and executed between layers)
        internal::sglLayerNext++;
        sgl_layer(internal::sglLayerNext);

        // Call virtual hook
        onBegin();
    }

    void end() {
        if (!loaded) return;
        if (internal::shaderStack.empty()) return;
        if (internal::shaderStack.back() != this) {
            logWarning("Shader") << "end() called on wrong shader";
            return;
        }

        // Call virtual hook
        onEnd();

        // Pop from stack
        internal::shaderStack.pop_back();

        // Restore sokol_gl state if no more shaders
        if (internal::shaderStack.empty()) {
            // Reset sokol_gfx state cache so sokol_gl can apply its pipeline
            sg_reset_state_cache();
        }
    }

    // -------------------------------------------------------------------------
    // Uniform setters
    // -------------------------------------------------------------------------

    void setUniform(int slot, float value) {
        float data[4] = { value, 0, 0, 0 };
        storeUniform(slot, data, sizeof(data));
    }

    void setUniform(int slot, const Vec2& v) {
        float data[4] = { v.x, v.y, 0, 0 };
        storeUniform(slot, data, sizeof(data));
    }

    void setUniform(int slot, const Vec3& v) {
        float data[4] = { v.x, v.y, v.z, 0 };
        storeUniform(slot, data, sizeof(data));
    }

    void setUniform(int slot, const Vec4& v) {
        float data[4] = { v.x, v.y, v.z, v.w };
        storeUniform(slot, data, sizeof(data));
    }

    void setUniform(int slot, const Color& c) {
        float data[4] = { c.r, c.g, c.b, c.a };
        storeUniform(slot, data, sizeof(data));
    }

    void setUniform(int slot, const std::vector<float>& v) {
        storeUniform(slot, v.data(), v.size() * sizeof(float));
    }

    void setUniform(int slot, const std::vector<Vec2>& v) {
        storeUniform(slot, v.data(), v.size() * sizeof(Vec2));
    }

    void setUniform(int slot, const std::vector<Vec4>& v) {
        storeUniform(slot, v.data(), v.size() * sizeof(Vec4));
    }

    void setUniform(int slot, const void* data, size_t size) {
        storeUniform(slot, data, size);
    }

    // Store uniform data for later application
    void storeUniform(int slot, const void* data, size_t size) {
        pendingUniforms[slot].assign((const uint8_t*)data, (const uint8_t*)data + size);
    }

    // Apply all stored uniforms
    void applyUniforms() {
        for (auto& [slot, data] : pendingUniforms) {
            sg_range range = { data.data(), data.size() };
            sg_apply_uniforms(slot, &range);
        }
    }

    // -------------------------------------------------------------------------
    // Texture binding
    // -------------------------------------------------------------------------

    void setTexture(int slot, sg_image image, sg_sampler sampler) {
        pendingTextures[slot] = { image, sampler };
    }

    void setTexture(int slot, sg_view view, sg_sampler sampler) {
        pendingViews[slot] = { view, sampler };
    }

    // -------------------------------------------------------------------------
    // Drawing (called by ShaderWriter) - defers draw to present()
    // -------------------------------------------------------------------------

    void submitVertices(const ShaderVertex* data, int count, PrimitiveType type) {
        if (count == 0) return;

        // Lines/LineStrip are not supported in shader mode (use StrokeMesh instead)
        if (type == PrimitiveType::Lines || type == PrimitiveType::LineStrip) {
            return;
        }

        // Defer this draw - will be executed in present() between sokol_gl layers
        DeferredShaderDraw draw;
        draw.layerId = internal::sglLayerNext - 1;  // Layer before this shader
        draw.shader = this;
        draw.vertices.assign(data, data + count);
        draw.type = type;
        internal::deferredShaderDraws.push_back(std::move(draw));
    }

    // Execute a deferred draw (called from present())
    void executeDeferredDraw(const std::vector<ShaderVertex>& vertices, PrimitiveType type) {
        if (vertices.empty()) return;

        // Ensure pipeline and uniforms are applied
        sg_apply_pipeline(pipeline);
        applyUniforms();

        // Append to vertex buffer
        sg_range range = { vertices.data(), vertices.size() * sizeof(ShaderVertex) };
        int vertexOffset = sg_append_buffer(vertexBuffer, &range);
        if (vertexOffset < 0) {
            logWarning("Shader") << "Vertex buffer overflow, skipping draw";
            return;
        }

        // Setup bindings
        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;

        // Apply pending textures
        for (auto& [slot, tex] : pendingViews) {
            bind.views[slot] = tex.view;
            bind.samplers[slot] = tex.sampler;
        }

        setupBindings(bind);

        // Generate triangle indices
        std::vector<uint16_t> indices;
        int count = (int)vertices.size();

        if (type == PrimitiveType::Quads) {
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
        } else if (type == PrimitiveType::TriangleStrip) {
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
        } else if (type == PrimitiveType::Triangles) {
            indices.reserve(count);
            for (int i = 0; i < count; i++) {
                indices.push_back((uint16_t)i);
            }
        } else if (type == PrimitiveType::Points) {
            return;
        }

        // Adjust indices for vertex buffer offset
        int baseVertex = vertexOffset / sizeof(ShaderVertex);
        for (auto& idx : indices) {
            idx += baseVertex;
        }

        // Append to index buffer
        sg_range idxRange = { indices.data(), indices.size() * sizeof(uint16_t) };
        int indexOffset = sg_append_buffer(indexBuffer, &idxRange);
        if (indexOffset < 0) {
            logWarning("Shader") << "Index buffer overflow, skipping draw";
            return;
        }

        bind.index_buffer = indexBuffer;
        bind.vertex_buffer_offsets[0] = 0;
        sg_apply_bindings(&bind);

        // Draw
        int baseElement = indexOffset / sizeof(uint16_t);
        sg_draw(baseElement, (int)indices.size(), 1);
    }

protected:
    // Sokol resources
    sg_shader shader = {};
    sg_pipeline pipeline = {};
    sg_buffer vertexBuffer = {};
    sg_buffer indexBuffer = {};
    bool loaded = false;

    // Pending texture bindings
    struct TextureBinding {
        sg_image image;
        sg_sampler sampler;
    };
    struct ViewBinding {
        sg_view view;
        sg_sampler sampler;
    };
    std::unordered_map<int, TextureBinding> pendingTextures;
    std::unordered_map<int, ViewBinding> pendingViews;

    // Pending uniform data (stored for reapplication after pipeline switch)
    std::unordered_map<int, std::vector<uint8_t>> pendingUniforms;

    // -------------------------------------------------------------------------
    // Virtual hooks for derived classes
    // -------------------------------------------------------------------------

    virtual sg_pipeline_desc createPipelineDesc() {
        sg_pipeline_desc desc = {};

        // Standard vertex layout: position(3) + texcoord(2) + color(4)
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT3;  // position
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;  // texcoord
        desc.layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT4;  // color

        // Explicitly disable depth test and face culling
        desc.depth.compare = SG_COMPAREFUNC_ALWAYS;
        desc.depth.write_enabled = false;
        desc.cull_mode = SG_CULLMODE_NONE;

        // Default alpha blending
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        // Index buffer for quad support
        desc.index_type = SG_INDEXTYPE_UINT16;

        desc.label = "tc_shader_pipeline";
        return desc;
    }

    virtual void createVertexBuffer() {
        // Vertex buffer for append mode (stream usage)
        sg_buffer_desc vbufDesc = {};
        vbufDesc.size = 65536 * sizeof(ShaderVertex);
        vbufDesc.usage.stream_update = true;  // Enable append mode
        vbufDesc.label = "tc_shader_vertices";
        vertexBuffer = sg_make_buffer(&vbufDesc);

        // Index buffer for append mode
        sg_buffer_desc ibufDesc = {};
        ibufDesc.size = 65536 * sizeof(uint16_t);
        ibufDesc.usage.index_buffer = true;
        ibufDesc.usage.stream_update = true;  // Enable append mode
        ibufDesc.label = "tc_shader_indices";
        indexBuffer = sg_make_buffer(&ibufDesc);
    }

    virtual void onBegin() {}
    virtual void onEnd() {}
    virtual void setupBindings(sg_bindings& bind) {}

private:
    void moveFrom(Shader&& other) {
        shader = other.shader;
        pipeline = other.pipeline;
        vertexBuffer = other.vertexBuffer;
        indexBuffer = other.indexBuffer;
        loaded = other.loaded;
        pendingTextures = std::move(other.pendingTextures);
        pendingViews = std::move(other.pendingViews);

        other.shader = {};
        other.pipeline = {};
        other.vertexBuffer = {};
        other.indexBuffer = {};
        other.loaded = false;
    }
};

// ---------------------------------------------------------------------------
// ShaderWriter::end() implementation (needs Shader class)
// ---------------------------------------------------------------------------
inline void ShaderWriter::end() {
    Shader* shader = internal::getCurrentShader();
    if (shader && !vertices.empty()) {
        // Apply current transformation matrix to vertices
        Mat4 mat = getCurrentMatrix();
        for (auto& v : vertices) {
            Vec3 transformed = mat * Vec3(v.x, v.y, v.z);
            v.x = transformed.x;
            v.y = transformed.y;
            v.z = transformed.z;
        }
        shader->submitVertices(vertices.data(), (int)vertices.size(), currentType);
    }
    vertices.clear();
}

// ---------------------------------------------------------------------------
// Global functions
// ---------------------------------------------------------------------------

inline void pushShader(Shader& shader) {
    shader.begin();
}

inline void popShader() {
    Shader* current = internal::getCurrentShader();
    if (current) {
        current->end();
    }
}

// Reset shader stack (called at end of frame)
inline void resetShaderStack() {
    while (!internal::shaderStack.empty()) {
        popShader();
    }
}

// Flush deferred shader draws (called from present())
// Draws sokol_gl layers interleaved with shader draws for correct ordering
inline void flushDeferredShaderDraws() {
    // Check for vertex buffer overflow — skip sgl draw to avoid crash
    // (overflowed commands may contain invalid pipeline IDs)
    sgl_error_t err = sgl_error();
    bool sglOverflow = err.vertices_full || err.commands_full;

    // For each layer: draw sokol_gl, then execute shader draws for that layer
    for (int layer = 0; layer <= internal::sglLayerNext; layer++) {
        // Draw sokol_gl content for this layer (skip if overflowed)
        if (!sglOverflow) {
            sgl_draw_layer(layer);
        }

        // Deferred shader draws are independent of sgl, always safe
        for (auto& draw : internal::deferredShaderDraws) {
            if (draw.layerId == layer) {
                draw.shader->executeDeferredDraw(draw.vertices, draw.type);
            }
        }
    }

    // Clear deferred draws for next frame
    internal::deferredShaderDraws.clear();

    // Reset layer for next frame
    internal::sglLayerNext = 0;
    sgl_layer(0);
}

// ---------------------------------------------------------------------------
// FullscreenShader - Fullscreen effect shader (position + texcoord layout)
// ---------------------------------------------------------------------------
class FullscreenShader : public Shader {
public:
    FullscreenShader() = default;

    // Set uniform params (call before draw)
    template<typename T>
    void setParams(const T& params) {
        paramsData_.resize(sizeof(T));
        std::memcpy(paramsData_.data(), &params, sizeof(T));
    }

    // Draw fullscreen quad with shader
    void draw() {
        if (!loaded) return;

        // Flush sokol_gl
        sgl_draw();

        sg_apply_pipeline(pipeline);

        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;
        bind.index_buffer = indexBuffer;
        setupBindings(bind);
        sg_apply_bindings(&bind);

        // Apply uniform params
        if (!paramsData_.empty()) {
            sg_range range = { paramsData_.data(), paramsData_.size() };
            sg_apply_uniforms(0, &range);
        }

        sg_draw(0, 6, 1);

        // Restore sokol_gl
        sgl_defaults();
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
    }

protected:
    sg_pipeline_desc createPipelineDesc() override {
        sg_pipeline_desc desc = {};

        // Fullscreen shader layout: position(2) + texcoord(2)
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;  // position
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;  // texcoord

        // Default alpha blending
        desc.colors[0].blend.enabled = true;
        desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        // Index buffer for quad
        desc.index_type = SG_INDEXTYPE_UINT16;

        desc.label = "tc_fullscreen_pipeline";
        return desc;
    }

    void createVertexBuffer() override {
        // Immutable fullscreen quad
        float vertices[] = {
            // position    texcoord
            -1.0f, -1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 1.0f,
             1.0f,  1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 0.0f,
        };

        sg_buffer_desc vbufDesc = {};
        vbufDesc.data = SG_RANGE(vertices);
        vbufDesc.label = "tc_fullscreen_vertices";
        vertexBuffer = sg_make_buffer(&vbufDesc);

        uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
        sg_buffer_desc ibufDesc = {};
        ibufDesc.usage.index_buffer = true;
        ibufDesc.data = SG_RANGE(indices);
        ibufDesc.label = "tc_fullscreen_indices";
        indexBuffer = sg_make_buffer(&ibufDesc);
    }

private:
    std::vector<uint8_t> paramsData_;
};

} // namespace trussc
