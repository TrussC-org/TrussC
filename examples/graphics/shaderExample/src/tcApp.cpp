#include "tcApp.h"

// Shader desc getter function array
typedef const sg_shader_desc* (*ShaderDescFunc)(sg_backend);
static ShaderDescFunc shaderDescFuncs[] = {
    gradient_shader_desc,
    ripple_shader_desc,
    plasma_shader_desc,
    mouse_follow_shader_desc
};

void tcApp::setup() {
    tcLogNotice("tcApp") << "shaderExample: Cross-Platform Shader Demo";
    tcLogNotice("tcApp") << "  - Press 1-4 to switch effects";
    tcLogNotice("tcApp") << "  - Press SPACE to cycle effects";

    sg_backend backend = sg_query_backend();

    // Create 4 shaders and pipelines
    for (int i = 0; i < NUM_EFFECTS; i++) {
        const sg_shader_desc* shd_desc = shaderDescFuncs[i](backend);
        if (!shd_desc) {
            tcLogError("tcApp") << "Failed to get shader desc for effect " << i;
            return;
        }

        shaders[i] = sg_make_shader(shd_desc);
        if (sg_query_shader_state(shaders[i]) != SG_RESOURCESTATE_VALID) {
            tcLogError("tcApp") << "Failed to create shader for effect " << i;
            return;
        }

        // Create pipeline (attributes are the same for all)
        sg_pipeline_desc pip_desc = {};
        pip_desc.shader = shaders[i];
        pip_desc.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;  // position
        pip_desc.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;  // texcoord0
        pip_desc.index_type = SG_INDEXTYPE_UINT16;
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;

        pipelines[i] = sg_make_pipeline(&pip_desc);
        if (sg_query_pipeline_state(pipelines[i]) != SG_RESOURCESTATE_VALID) {
            tcLogError("tcApp") << "Failed to create pipeline for effect " << i;
            return;
        }
    }

    // Fullscreen quad vertex buffer (shared)
    float vertices[] = {
        // position    texcoord
        -1.0f, -1.0f,  0.0f, 1.0f,
         1.0f, -1.0f,  1.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 0.0f,
    };

    sg_buffer_desc vbuf_desc = {};
    vbuf_desc.data = SG_RANGE(vertices);
    vbuf_desc.label = "effect_vertices";
    vertexBuffer = sg_make_buffer(&vbuf_desc);

    // Index buffer (shared)
    uint16_t indices[] = { 0, 1, 2, 0, 2, 3 };
    sg_buffer_desc ibuf_desc = {};
    ibuf_desc.usage.index_buffer = true;
    ibuf_desc.data = SG_RANGE(indices);
    ibuf_desc.label = "effect_indices";
    indexBuffer = sg_make_buffer(&ibuf_desc);

    loaded = true;
    tcLogNotice("tcApp") << "All 4 effects loaded successfully!";
}

void tcApp::update() {
    uniforms.time = getElapsedTime();
    uniforms.resolution[0] = (float)getWindowWidth();
    uniforms.resolution[1] = (float)getWindowHeight();
    uniforms.mouse[0] = (float)getGlobalMouseX();
    uniforms.mouse[1] = (float)getGlobalMouseY();
}

void tcApp::draw() {
    clear(0.0f);

    if (!loaded) {
        drawBitmapStringHighlight("Shader failed to load", 10, 20,
            Color(0, 0, 0, 0.7f), Color(1, 0, 0));
        return;
    }

    // Flush sokol_gl drawing
    sgl_draw();

    // Apply current effect pipeline
    sg_apply_pipeline(pipelines[currentEffect]);

    sg_bindings bindings = {};
    bindings.vertex_buffers[0] = vertexBuffer;
    bindings.index_buffer = indexBuffer;
    sg_apply_bindings(&bindings);

    // Apply uniforms
    sg_range range = { &uniforms, sizeof(fs_params_t) };
    sg_apply_uniforms(UB_fs_params, &range);

    // Fullscreen draw
    sg_draw(0, 6, 1);

    // Reset sokol_gl state
    sgl_defaults();
    sgl_matrix_mode_projection();
    sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -10000.0f, 10000.0f);
    sgl_matrix_mode_modelview();
    sgl_load_identity();

    // Display info
    string info = "Effect " + to_string(currentEffect + 1) + ": " + getEffectName(currentEffect);
    drawBitmapStringHighlight(info, 10, 20,
        Color(0, 0, 0, 0.7f), Color(1, 1, 1));
    drawBitmapStringHighlight("Press 1-4 or SPACE to change", 10, 40,
        Color(0, 0, 0, 0.7f), Color(0.7f, 0.7f, 0.7f));
}

void tcApp::keyPressed(int key) {
    if (key == '1') currentEffect = 0;
    else if (key == '2') currentEffect = 1;
    else if (key == '3') currentEffect = 2;
    else if (key == '4') currentEffect = 3;
    else if (key == ' ') {
        currentEffect = (currentEffect + 1) % NUM_EFFECTS;
    }
}

const char* tcApp::getEffectName(int index) {
    static const char* names[] = {
        "Gradient",
        "Ripple",
        "Plasma",
        "Mouse Follow"
    };
    return names[index];
}
