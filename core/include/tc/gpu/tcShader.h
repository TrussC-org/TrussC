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
#include "../utils/tcOnceGate.h"

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
            internal::deferGpuDestroy(shader); shader = {};
            return false;
        }

        // Capture the subclass recipe; attachment compatibility is resolved at draw.
        sg_pipeline_desc pipDesc = createPipelineDesc();
        pipDesc.shader = shader;

        recipe_ = pipDesc;
        recipe_.label = nullptr; // subclass-owned label need not outlive load()

        // Create dynamic vertex buffer
        createVertexBuffer();
        if (sg_query_buffer_state(vertexBuffer) != SG_RESOURCESTATE_VALID ||
            (recipe_.index_type != SG_INDEXTYPE_NONE && recipe_.index_type != _SG_INDEXTYPE_DEFAULT &&
             sg_query_buffer_state(indexBuffer) != SG_RESOURCESTATE_VALID)) {
            if (!stream_) {
                internal::deferGpuDestroy(vertexBuffer); internal::deferGpuDestroy(indexBuffer);
            }
            internal::deferGpuDestroy(shader);
            shader = {}; vertexBuffer = {}; indexBuffer = {}; stream_.reset();
            logError("Shader") << "Failed to create draw buffers";
            return false;
        }

        loaded = true;
        return true;
    }

    void clear() {
        // Defer all destroys to end-of-frame (tcGpuDestroyQueue.h): a deferred
        // shader draw recorded this frame may still reference these handles.
        // This is what makes scope-local Shaders safe — the captured pipeline
        // and buffers outlive the object until the flush.
        if (loaded) {
            if (!stream_) {
                internal::deferGpuDestroy(indexBuffer);
                internal::deferGpuDestroy(vertexBuffer);
            }
            internal::retirePipelineProgram(shader);
            internal::deferGpuDestroy(shader);
        }
        for (auto& [slot, cached] : imageViews_) {
            internal::deferGpuDestroy(cached.view);
        }
        imageViews_.clear();
        shader = {};

        vertexBuffer = {};
        indexBuffer = {};
        stream_.reset();
        loaded = false;
    }

    bool isLoaded() const { return loaded; }

    // -------------------------------------------------------------------------
    // Begin / End
    // -------------------------------------------------------------------------

    void begin() {
        if (!loaded) return;

        // Push to stack (per-window)
        auto& wctx = internal::currentWindowContext();
        // Bind the scope to an activation, including while its target is parked.
        auto& target = internal::currentTarget();
        wctx.shaderScopeTargets.push_back({&target, target.activation});
        try { wctx.shaderStack.push_back(this); }
        catch (...) { wctx.shaderScopeTargets.pop_back(); throw; }
        ++target.openShaderScopes;

        // Increment layer for post-shader sokol_gl draws
        // (shader draws will be deferred and executed between layers).
        // Inside an FBO pass the layering is handled per-submission instead
        // (target layer bump in submitVertices, like the deferred PBR path),
        // so only the swapchain layer counter is advanced here.
        if (!wctx.currentTarget->isFbo) {
            wctx.currentTarget->advanceLayer();
        }

        // Call virtual hook
        onBegin();
    }

    void end() {
        if (!loaded) return;
        auto& stack = internal::currentWindowContext().shaderStack;
        if (stack.empty()) return;
        if (stack.back() != this) {
            logWarning("Shader") << "end() called on wrong shader";
            return;
        }

        auto& wctx = internal::currentWindowContext();
        const auto scope = wctx.shaderScopeTargets.back();
        if (scope.target != &internal::currentTarget() || scope.token != scope.target->activation) {
            logWarning("Shader") << "end() rejected on a different target activation";
            return;
        }
        // Call virtual hook
        onEnd();

        // Pop from stack
        stack.pop_back();
        wctx.shaderScopeTargets.pop_back();
        --scope.target->openShaderScopes;

        // Restore sokol_gl state if no more shaders
        if (stack.empty()) {
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
        if (slot < 0 || slot >= SG_MAX_UNIFORMBLOCK_BINDSLOTS) {
            static OnceGate warned;
            if (warned.isFirstTime()) {
                logWarning("Shader") << "Uniform slot " << slot << " is outside [0, "
                    << SG_MAX_UNIFORMBLOCK_BINDSLOTS << "); ignored";
            }
            return;
        }
        auto& bytes = pendingUniforms[slot];
        if (size == 0) {
            bytes.clear();
            return;
        }
        bytes.assign((const uint8_t*)data, (const uint8_t*)data + size);
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

    // Both overloads accept view slots [0, SG_MAX_VIEW_BINDSLOTS). A sampler
    // is bound at the same slot only below SG_MAX_SAMPLER_BINDSLOTS (12).
    // Higher texture slots (12-31) ignore a supplied sampler with a warning;
    // the shader must sample them with a sampler at binding 0-11, set by
    // another setTexture() call or an overridden setupBindings().

    // Convenience overload taking a raw sg_image. sokol's binding model needs
    // an sg_view, so one is created here and cached per slot (recreated only
    // when the image changes; the old view goes through the deferred-destroy
    // queue since a recorded draw may still reference it). Prefer the sg_view
    // overload when a view is already available (e.g. Texture::getView()).
    void setTexture(int slot, sg_image image, sg_sampler sampler) {
        if (!validateTextureSlot(slot, sampler)) return;
        auto& cached = imageViews_[slot];
        if (cached.image.id != image.id) {
            internal::deferGpuDestroy(cached.view);
            sg_view_desc vd = {};
            vd.texture.image = image;
            cached.view = sg_make_view(&vd);
            cached.image = image;
        }
        pendingViews[slot] = { cached.view, sampler };
    }

    void setTexture(int slot, sg_view view, sg_sampler sampler) {
        if (!validateTextureSlot(slot, sampler)) return;
        pendingViews[slot] = { view, sampler };
    }

    // -------------------------------------------------------------------------
    // Drawing (called by ShaderWriter) - defers draw to present()
    // -------------------------------------------------------------------------

    void submitVertices(const ShaderVertex* data, int count, PrimitiveType type) {
        if (count == 0 || !loaded) return;
        if (count < 0 || !data || (size_t)count > SIZE_MAX / sizeof(ShaderVertex)) {
            internal::reportImportError("Shader", "shader vertex count cannot be represented");
            return;
        }
        try {

        // Lines/LineStrip are not supported in shader mode (use StrokeMesh instead)
        if (type == PrimitiveType::Lines || type == PrimitiveType::LineStrip) {
            return;
        }

        // Capture contract: snapshot EVERYTHING the replay needs NOW, so the
        // flush never touches this Shader object (it may be scope-local and
        // destroyed before present()) and later setUniform()/setTexture()
        // calls cannot retroactively change this draw. Same pattern as
        // PbrDrawCommand; executed by internal::executeDeferredShaderDraw().
        // Protected handles are aliases; replay owns and can replace the stream.
        if (stream_) {
            vertexBuffer = stream_->vertices;
            indexBuffer = stream_->indices;
        }
        internal::DeferredShaderDraw draw;
        draw.stream = stream_;
        const auto selection = selectPipelineForCurrentTarget_();
        draw.pipeline = selection.raw;
        draw.pipelineGeneration = selection.generation;
        if (!draw.pipeline.id) return;
        draw.vertices.assign(data, data + count);
        draw.type = type;

        // Snapshot uniform block bytes (the same bytes applyUniforms() would
        // upload, but frozen per draw instead of last-write-wins).
        draw.uniforms.reserve(pendingUniforms.size());
        for (const auto& [slot, bytes] : pendingUniforms) {
            draw.uniforms.emplace_back(slot, bytes);
        }

        // Snapshot bindings: stream buffers + texture view/sampler pairs.
        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;
        fillTextureBindings(bind);
        setupBindings(bind);  // subclass hook (runs at submission, object is alive)
        bind.index_buffer = indexBuffer;
        draw.bindings = bind;

        auto& wctx = internal::currentWindowContext();
        if (wctx.currentTarget->isFbo) {
            // Defer into the per-FBO list; flushed per-layer by
            // RenderTarget::flushLayers() at Fbo::end()/clearColor(). Bump the FBO
            // layer so 2D drawn after this composites on top of it, matching
            // the deferred PBR/point pattern (tcMeshPbrPipeline.h).
            draw.layerId = wctx.currentTarget->sglLayerNext;
            if (!internal::appendTargetRecording(*wctx.currentTarget, wctx.currentTarget->deferredShaderDraws, std::move(draw))) return;
            wctx.currentTarget->advanceLayer();
        } else {
            // Swapchain: executed in present() between sokol_gl layers.
            draw.layerId = wctx.currentTarget->sglLayerNext - 1;  // Layer before this shader
            if (!internal::appendTargetRecording(*wctx.currentTarget, wctx.currentTarget->deferredShaderDraws, std::move(draw))) return;
        }
        } catch (const std::bad_alloc&) {
            internal::reportImportError("Shader", "shader snapshot allocation failed");
        } catch (const std::length_error&) {
            internal::reportImportError("Shader", "shader snapshot size cannot be represented");
        }
    }

protected:
    // Slots have been checked by setTexture(); views and samplers have
    // different sokol limits, so high view slots must not write a sampler.
    void fillTextureBindings(sg_bindings& bind) const {
        for (const auto& [slot, tex] : pendingViews) {
            bind.views[slot] = tex.view;
            if (slot < SG_MAX_SAMPLER_BINDSLOTS) {
                bind.samplers[slot] = tex.sampler;
            }
        }
    }

    // Sokol resources
    sg_shader shader = {};
    sg_pipeline_desc recipe_ = {};
    sg_buffer vertexBuffer = {};
    sg_buffer indexBuffer = {};
    bool loaded = false;

    // Pending texture bindings
    struct ViewBinding {
        sg_view view;
        sg_sampler sampler;
    };
    std::unordered_map<int, ViewBinding> pendingViews;

    // Views created by the setTexture(slot, sg_image, sampler) convenience
    // overload, cached per slot and released (deferred) in clear().
    struct CachedImageView {
        sg_image image = {};
        sg_view view = {};
    };
    std::unordered_map<int, CachedImageView> imageViews_;

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
        desc.index_type = SG_INDEXTYPE_UINT32;

        desc.label = "tc_shader_pipeline";
        return desc;
    }

    virtual void createVertexBuffer() {
        stream_ = std::make_shared<internal::ShaderStreamState>();
        vertexBuffer = stream_->vertices = internal::makeShaderStreamBuffer(65536 * sizeof(ShaderVertex), false);
        indexBuffer = stream_->indices = internal::makeShaderStreamBuffer(65536 * sizeof(uint32_t), true);
    }

    virtual void onBegin() {}
    virtual void onEnd() {}
    virtual void setupBindings(sg_bindings& bind) {}

    internal::PipelineSelection selectPipelineForCurrentTarget_() {
        return internal::currentTarget().pipeline(internal::PipelineKey::raw(internal::PipelineRole::Shader, recipe_));
    }
    sg_pipeline pipelineForCurrentTarget() { return selectPipelineForCurrentTarget_().raw; }

private:
    static bool validateTextureSlot(int slot, sg_sampler sampler) {
        if (slot < 0 || slot >= SG_MAX_VIEW_BINDSLOTS) {
            static OnceGate warned;
            if (warned.isFirstTime()) {
                logWarning("Shader") << "Texture slot " << slot << " is outside [0, "
                    << SG_MAX_VIEW_BINDSLOTS << "); ignored";
            }
            return false;
        }
        if (slot >= SG_MAX_SAMPLER_BINDSLOTS && sampler.id != SG_INVALID_ID) {
            static OnceGate warned;
            if (warned.isFirstTime()) {
                logWarning("Shader") << "Sampler for texture slot " << slot
                    << " is ignored; use a sampler at binding [0, "
                    << SG_MAX_SAMPLER_BINDSLOTS << ")";
            }
        }
        return true;
    }

    std::shared_ptr<internal::ShaderStreamState> stream_;

    void moveFrom(Shader&& other) {
        shader = other.shader;
        recipe_ = other.recipe_;
        vertexBuffer = other.vertexBuffer;
        indexBuffer = other.indexBuffer;
        loaded = other.loaded;
        stream_ = std::move(other.stream_);
        pendingViews = std::move(other.pendingViews);
        pendingUniforms = std::move(other.pendingUniforms);
        imageViews_ = std::move(other.imageViews_);

        other.shader = {};
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
        if (vertices.size() > (size_t)INT_MAX) internal::reportImportError("Shader", "shader vertex count cannot be represented");
        else shader->submitVertices(vertices.data(), (int)vertices.size(), currentType);
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
        if (!internal::ensureCurrentPass()) return;

        // Submit all preceding layers, including deferred user shaders, meshes
        // and points. Advance the command base so end()/present() cannot blend
        // that content a second time after this fullscreen draw.
        internal::currentTarget().flushLayers();

        // Match the pipeline to the current target (swapchain vs FBO format).
        const auto selected = selectPipelineForCurrentTarget_();
        if (!selected.raw.id) return;
        sg_apply_pipeline(selected.raw);

        sg_bindings bind = {};
        bind.vertex_buffers[0] = vertexBuffer;
        bind.index_buffer = indexBuffer;
        // Apply inputs set via setTexture(slot, view, sampler), so a plain
        // FullscreenShader can sample a source without a setupBindings() override.
        fillTextureBindings(bind);
        setupBindings(bind);
        sg_apply_bindings(&bind);

        // Apply uniform params
        if (!paramsData_.empty()) {
            sg_range range = { paramsData_.data(), paramsData_.size() };
            sg_apply_uniforms(0, &range);
        }

        sg_draw(0, 6, 1);

        sg_reset_state_cache();
        internal::applyTargetDrawingState();
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
