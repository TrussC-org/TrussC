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
#include "../utils/tcAnnotations.h"

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
        for (auto& [key, pip] : targetPipelines_) {
            if (pip.id) sg_destroy_pipeline(pip);
        }
        targetPipelines_.clear();
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

    // NOTE: unlike Vec2/Vec4 overloads this one is NOT zero-copy. std140
    // uniform block layout aligns each vec3[] element to 16B, so we pad
    // every Vec3 to a Vec4 (w=0) before sending. GLSL side can still
    // declare the array as `uniform vec3 arr[N];` — the alignment is
    // handled under the hood.
    void setUniform(int slot, const std::vector<Vec3>& v) {
        std::vector<Vec4> padded;
        padded.reserve(v.size());
        for (const auto& e : v) padded.emplace_back(e.x, e.y, e.z, 0.0f);
        storeUniform(slot, padded.data(), padded.size() * sizeof(Vec4));
    }

    void setUniform(int slot, const std::vector<Vec4>& v) {
        storeUniform(slot, v.data(), v.size() * sizeof(Vec4));
    }

    void setUniform(int slot, const void* data, size_t size) {
        storeUniform(slot, data, size);
    }

protected:
    // Internal uniform plumbing — protected so Shader subclasses can reuse it.
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
public:

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
        internal::DeferredShaderDraw draw;
        draw.layerId = internal::sglLayerNext - 1;  // Layer before this shader
        draw.shader = this;
        draw.vertices.assign(data, data + count);
        draw.type = type;
        internal::deferredShaderDraws.push_back(std::move(draw));
    }

    // Execute a deferred draw (called from present())
    void executeDeferredDraw(const std::vector<ShaderVertex>& vertices, PrimitiveType type) {
        if (vertices.empty()) return;

        // Ensure pipeline and uniforms are applied (FBO-aware: deferred draws run
        // at present time on the swapchain, but route through the same helper so
        // the path stays correct if a draw is ever executed inside an FBO pass).
        sg_apply_pipeline(pipelineForCurrentTarget());
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
    sg_pipeline pipeline = {};   // targets the swapchain (created at load())
    sg_buffer vertexBuffer = {};
    sg_buffer indexBuffer = {};
    bool loaded = false;

    // Render-target-specific pipeline variants, keyed by (color format, sample
    // count). `pipeline` above matches the swapchain; an FBO pass has a different
    // color format / sample count / depth, so a matching pipeline is built lazily
    // on first draw into each distinct FBO target. Using a mismatched pipeline
    // (e.g. a swapchain BGRA8 pipeline inside an RGBA8 FBO) corrupts the output.
    std::unordered_map<uint64_t, sg_pipeline> targetPipelines_;

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

    // Pipeline matching the current render target. The swapchain uses the
    // load()-time `pipeline`; an FBO pass needs a pipeline whose color format,
    // sample count and depth match the FBO, so one is built lazily (from the same
    // createPipelineDesc()) and cached per distinct (format, sampleCount) target.
    sg_pipeline pipelineForCurrentTarget() {
        if (!internal::currentWindowContext().inFboPass) return pipeline;
        uint64_t key = ((uint64_t)internal::currentFboColorFormat << 8)
                     | (uint64_t)(internal::currentFboSampleCount & 0xff);
        auto it = targetPipelines_.find(key);
        if (it != targetPipelines_.end()) return it->second;
        sg_pipeline_desc desc = createPipelineDesc();
        desc.shader = shader;
        desc.colors[0].pixel_format = internal::currentFboColorFormat;
        desc.sample_count           = internal::currentFboSampleCount;
        desc.depth.pixel_format     = SG_PIXELFORMAT_DEPTH_STENCIL;  // Fbo always allocates depth-stencil
        sg_pipeline pip = sg_make_pipeline(&desc);
        targetPipelines_[key] = pip;
        return pip;
    }

private:
    void moveFrom(Shader&& other) {
        shader = other.shader;
        pipeline = other.pipeline;
        vertexBuffer = other.vertexBuffer;
        indexBuffer = other.indexBuffer;
        loaded = other.loaded;
        pendingTextures = std::move(other.pendingTextures);
        pendingViews = std::move(other.pendingViews);
        targetPipelines_ = std::move(other.targetPipelines_);

        other.shader = {};
        other.pipeline = {};
        other.targetPipelines_.clear();
        other.vertexBuffer = {};
        other.indexBuffer = {};
        other.loaded = false;
    }
};

// ---------------------------------------------------------------------------
// ShaderWriter::end() implementation (needs Shader class)
// ---------------------------------------------------------------------------
inline void internal::ShaderWriter::end() {
    Shader* shader = internal::getCurrentShader();
    if (shader && !vertices.empty()) {
        // Apply current transformation matrix to vertices
        Mat4 mat = getMatrix();
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

// NOTE: the frame-end shader-stack reset is internal::resetShaderStack()
// (defined in tcVertexWriter.h, called from present()). A second public
// resetShaderStack() that drained the stack via popShader() used to live
// here but had no callers — removed during the internal:: consolidation.

// Flush deferred shader draws (called from present())
// Draws sokol_gl layers interleaved with shader draws for correct ordering
namespace internal {
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

        // Deferred PBR mesh draws — same per-layer ordering, so PBR composites
        // with sokol_gl 2D in submission order (a 2D background drawn first
        // stays behind the meshes).
        for (auto& d : internal::deferredPbrDraws) {
            if (d.layerId == layer) {
                internal::getPbrPipeline().executePbrDraw(d.cmd);
            }
        }

        // Deferred point-splat draws (Mesh in PrimitiveMode::Points) — same
        // per-layer ordering, sharing the swapchain pass + depth buffer.
        for (auto& d : internal::deferredPointDraws) {
            if (d.layerId == layer) {
                internal::executePointDraw(d.cmd);
            }
        }
    }

    // Clear deferred draws for next frame
    internal::deferredShaderDraws.clear();
    internal::deferredPbrDraws.clear();
    internal::deferredPointDraws.clear();

    // Reset layer for next frame
    internal::sglLayerNext = 0;
    sgl_layer(0);
}
} // namespace internal

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

        // Ensure render pass is active (swapchain or FBO)
        ensureSwapchainPass();

        // Flush sokol_gl so it draws before the fullscreen quad
        sgl_draw();

        // Match the pipeline to the current target (swapchain vs FBO format).
        sg_apply_pipeline(pipelineForCurrentTarget());

        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;
        bind.index_buffer = indexBuffer;
        // Apply inputs set via setTexture(slot, view, sampler), so a plain
        // FullscreenShader can sample a source without a setupBindings() override.
        for (auto& [slot, v] : pendingViews) {
            bind.views[slot] = v.view;
            bind.samplers[slot] = v.sampler;
        }
        setupBindings(bind);
        sg_apply_bindings(&bind);

        // Apply uniform params
        if (!paramsData_.empty()) {
            sg_range range = { paramsData_.data(), paramsData_.size() };
            sg_apply_uniforms(0, &range);
        }

        sg_draw(0, 6, 1);

        // Restore sokol_gl state
        sg_reset_state_cache();
        sgl_defaults();
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)getFramebufferWidth(), (float)getFramebufferHeight(), 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
    }

protected:
    sg_pipeline_desc createPipelineDesc() override {
        sg_pipeline_desc desc = {};

        // Fullscreen shader layout: position(2) + texcoord(2)
        desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;  // position
        desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;  // texcoord

        // A fullscreen pass covers the whole target, so OVERWRITE by default (no
        // blend). Blending would composite over existing content — and for a
        // premultiplied-alpha source (e.g. blurring an FBO) it re-premultiplies
        // every pass, darkening/desaturating the result. Subclasses that need to
        // composite can override this to enable blending.
        desc.colors[0].blend.enabled = false;

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
