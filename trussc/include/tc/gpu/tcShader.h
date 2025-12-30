#pragma once

// =============================================================================
// tcShader.h - Fullscreen shader (for post-processing)
// =============================================================================
//
// Fullscreen shader using sokol_gfx directly
// Combined with FBO to achieve post-process effects
//
// Usage example:
//   tc::Shader shader;
//   shader.loadFromSource(fragmentShaderSource);
//
//   // Inside draw()
//   shader.begin();
//   shader.setUniformTime(tc::getElapsedTime());
//   shader.setUniformResolution(tc::getWindowWidth(), tc::getWindowHeight());
//   shader.draw();  // Fullscreen draw
//   shader.end();
//
// =============================================================================

#include <string>
#include <fstream>
#include <sstream>
#include <filesystem>

#include "../utils/tcLog.h"

namespace trussc {

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Shader class (fullscreen shader)
// ---------------------------------------------------------------------------
class Shader {
public:
    Shader() = default;
    ~Shader() { clear(); }

    // Copy prohibited
    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;

    // Move support
    Shader(Shader&& other) noexcept {
        moveFrom(std::move(other));
    }

    Shader& operator=(Shader&& other) noexcept {
        if (this != &other) {
            clear();
            moveFrom(std::move(other));
        }
        return *this;
    }

    // -------------------------------------------------------------------------
    // Shader loading
    // -------------------------------------------------------------------------

    // Load from file
    bool load(const fs::path& path) {
        std::ifstream file(path);
        if (!file) {
            logError() << "Shader: failed to open " << path.string();
            return false;
        }

        std::stringstream buffer;
        buffer << file.rdbuf();
        return loadFromSource(buffer.str());
    }

    // Load from fragment shader source (Metal MSL)
    bool loadFromSource(const std::string& fragmentSource) {
        clear();

        fragmentSource_ = fragmentSource;

        // Vertex shader (fixed: for fullscreen quad)
        const char* vertexSource = R"(
            #include <metal_stdlib>
            using namespace metal;

            struct VertexIn {
                float2 position [[attribute(0)]];
                float2 texcoord [[attribute(1)]];
            };

            struct VertexOut {
                float4 position [[position]];
                float2 texcoord;
            };

            vertex VertexOut vertexMain(VertexIn in [[stage_in]]) {
                VertexOut out;
                out.position = float4(in.position, 0.0, 1.0);
                out.texcoord = in.texcoord;
                return out;
            }
        )";

        // Create shader
        sg_shader_desc shd_desc = {};
        shd_desc.vertex_func.source = vertexSource;
        shd_desc.vertex_func.entry = "vertexMain";
        shd_desc.fragment_func.source = fragmentSource_.c_str();
        shd_desc.fragment_func.entry = "fragmentMain";

        // Vertex attributes
        shd_desc.attrs[0].base_type = SG_SHADERATTRBASETYPE_FLOAT;  // position
        shd_desc.attrs[1].base_type = SG_SHADERATTRBASETYPE_FLOAT;  // texcoord

        // Uniform block
        shd_desc.uniform_blocks[0].stage = SG_SHADERSTAGE_FRAGMENT;
        shd_desc.uniform_blocks[0].size = sizeof(Uniforms);
        shd_desc.uniform_blocks[0].msl_buffer_n = 0;

        shd_desc.label = "fullscreen_shader";

        shader_ = sg_make_shader(&shd_desc);
        if (sg_query_shader_state(shader_) != SG_RESOURCESTATE_VALID) {
            logError() << "Shader: failed to create shader";
            return false;
        }

        // Create pipeline
        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = shader_;
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;  // position
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;  // texcoord
        pip_desc.index_type = SG_INDEXTYPE_UINT16;  // Use index buffer
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.label = "fullscreen_pipeline";

        pipeline_ = sg_make_pipeline(&pip_desc);
        if (sg_query_pipeline_state(pipeline_) != SG_RESOURCESTATE_VALID) {
            logError() << "Shader: failed to create pipeline";
            sg_destroy_shader(shader_);
            shader_ = {};
            return false;
        }

        // Create vertex buffer for fullscreen quad
        float vertices[] = {
            // position    texcoord
            -1.0f, -1.0f,  0.0f, 1.0f,
             1.0f, -1.0f,  1.0f, 1.0f,
             1.0f,  1.0f,  1.0f, 0.0f,
            -1.0f,  1.0f,  0.0f, 0.0f,
        };

        sg_buffer_desc vbuf_desc = {};
        vbuf_desc.data = SG_RANGE(vertices);
        vbuf_desc.label = "fullscreen_vertices";
        vertexBuffer_ = sg_make_buffer(&vbuf_desc);

        // Index buffer
        uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
        sg_buffer_desc ibuf_desc = {};
        ibuf_desc.usage.index_buffer = true;
        ibuf_desc.data = SG_RANGE(indices);
        ibuf_desc.label = "fullscreen_indices";
        indexBuffer_ = sg_make_buffer(&ibuf_desc);

        loaded_ = true;
        return true;
    }

    // Release resources
    void clear() {
        if (loaded_) {
            sg_destroy_buffer(indexBuffer_);
            sg_destroy_buffer(vertexBuffer_);
            sg_destroy_pipeline(pipeline_);
            sg_destroy_shader(shader_);
            loaded_ = false;
        }
        shader_ = {};
        pipeline_ = {};
        vertexBuffer_ = {};
        indexBuffer_ = {};
        fragmentSource_.clear();
    }

    bool isLoaded() const { return loaded_; }

    // -------------------------------------------------------------------------
    // Shader application and drawing
    // -------------------------------------------------------------------------

    // Enable shader
    void begin() {
        if (!loaded_) return;
        active_ = true;

        // Flush sokol_gl drawing
        sgl_draw();

        sg_apply_pipeline(pipeline_);

        sg_bindings bindings = {};
        bindings.vertex_buffers[0] = vertexBuffer_;
        bindings.index_buffer = indexBuffer_;
        sg_apply_bindings(&bindings);
    }

    // Fullscreen draw
    void draw() {
        if (!active_) return;

        // Apply uniforms
        sg_range range = { &uniforms_, sizeof(Uniforms) };
        sg_apply_uniforms(0, &range);

        sg_draw(0, 6, 1);
    }

    // Disable shader
    void end() {
        if (!active_) return;
        active_ = false;
        // Reset sokol_gl state
        sgl_defaults();
        sgl_matrix_mode_projection();
        sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -10000.0f, 10000.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();
    }

    // -------------------------------------------------------------------------
    // Uniform settings
    // -------------------------------------------------------------------------

    void setUniformTime(float time) {
        uniforms_.time = time;
    }

    void setUniformResolution(float width, float height) {
        uniforms_.resolution[0] = width;
        uniforms_.resolution[1] = height;
    }

    void setUniformMouse(float x, float y) {
        uniforms_.mouse[0] = x;
        uniforms_.mouse[1] = y;
    }

    void setUniformCustom(int index, float value) {
        if (index >= 0 && index < 4) {
            uniforms_.custom[index] = value;
        }
    }

private:
    // Uniform block (Metal 16-byte alignment)
    struct Uniforms {
        float time = 0;
        float _pad0[3] = {0, 0, 0};
        float resolution[4] = {0, 0, 0, 0};  // xy: resolution, zw: unused
        float mouse[4] = {0, 0, 0, 0};       // xy: mouse, zw: unused
        float custom[4] = {0, 0, 0, 0};
    };

    sg_shader shader_ = {};
    sg_pipeline pipeline_ = {};
    sg_buffer vertexBuffer_ = {};
    sg_buffer indexBuffer_ = {};
    std::string fragmentSource_;
    bool loaded_ = false;
    bool active_ = false;
    Uniforms uniforms_;

    void moveFrom(Shader&& other) {
        shader_ = other.shader_;
        pipeline_ = other.pipeline_;
        vertexBuffer_ = other.vertexBuffer_;
        indexBuffer_ = other.indexBuffer_;
        fragmentSource_ = std::move(other.fragmentSource_);
        loaded_ = other.loaded_;
        active_ = other.active_;
        uniforms_ = other.uniforms_;

        other.shader_ = {};
        other.pipeline_ = {};
        other.vertexBuffer_ = {};
        other.indexBuffer_ = {};
        other.loaded_ = false;
        other.active_ = false;
    }
};

} // namespace trussc
