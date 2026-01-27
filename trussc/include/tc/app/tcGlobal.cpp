// =============================================================================
// tcGlobal.cpp - TrussC Global Functions Implementation
// Large functions moved from TrussC.h for better compile times
// =============================================================================

#include <TrussC.h>

namespace trussc {

// ---------------------------------------------------------------------------
// Initialization (setup)
// ---------------------------------------------------------------------------
void setup() {
    // Initialize sokol_gfx
    sg_desc sgdesc = {};
    sgdesc.environment = sglue_environment();
    sgdesc.logger.func = slog_func;
    sg_setup(&sgdesc);

    // Initialize sokol_gl
    sgl_desc_t sgldesc = {};
    sgldesc.logger.func = slog_func;
    sgl_setup(&sgldesc);

    // Initialize bitmap font texture
    if (!internal::fontInitialized) {
        // Generate texture atlas pixel data
        unsigned char* pixels = bitmapfont::generateAtlasPixels();

        // Create texture
        sg_image_desc img_desc = {};
        img_desc.width = bitmapfont::ATLAS_WIDTH;
        img_desc.height = bitmapfont::ATLAS_HEIGHT;
        img_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        img_desc.data.mip_levels[0].ptr = pixels;
        img_desc.data.mip_levels[0].size = bitmapfont::ATLAS_WIDTH * bitmapfont::ATLAS_HEIGHT * 4;
        internal::fontTexture = sg_make_image(&img_desc);

        // Create texture view
        sg_view_desc view_desc = {};
        view_desc.texture.image = internal::fontTexture;
        internal::fontView = sg_make_view(&view_desc);

        // Create sampler (nearest neighbor, pixel perfect)
        sg_sampler_desc smp_desc = {};
        smp_desc.min_filter = SG_FILTER_NEAREST;
        smp_desc.mag_filter = SG_FILTER_NEAREST;
        smp_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
        smp_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
        internal::fontSampler = sg_make_sampler(&smp_desc);

        // Create pipeline for alpha blending
        // RGB: standard alpha blend
        // Alpha: overwrite (to prevent FBO from becoming semi-transparent when drawing to FBO)
        sg_pipeline_desc pip_desc = {};
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ZERO;
        internal::fontPipeline = sgl_make_pipeline(&pip_desc);

        delete[] pixels;
        internal::fontInitialized = true;
    }

    // Create pipeline for 3D drawing (depth test + alpha blend)
    if (!internal::pipeline3dInitialized) {
        sg_pipeline_desc pip_desc = {};
        pip_desc.cull_mode = SG_CULLMODE_NONE;  // No culling
        pip_desc.depth.write_enabled = true;
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        // Enable alpha blending
        pip_desc.colors[0].blend.enabled = true;
        pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        internal::pipeline3d = sgl_make_pipeline(&pip_desc);
        internal::pipeline3dInitialized = true;
    }

    // Create blend mode pipelines
    // Alpha channel is additive in all modes (does not reduce existing alpha)
    if (!internal::blendPipelinesInitialized) {
        // Alpha - normal alpha blending
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
            internal::blendPipelines[static_cast<int>(BlendMode::Alpha)] = sgl_make_pipeline(&pip_desc);
        }
        // Add - additive blending
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            internal::blendPipelines[static_cast<int>(BlendMode::Add)] = sgl_make_pipeline(&pip_desc);
        }
        // Multiply - multiply blending
        // Pure multiplication: result = src × dst
        // Semi-transparency expressed by color darkness (assumes srcAlpha is premultiplied into RGB)
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_DST_COLOR;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ZERO;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            internal::blendPipelines[static_cast<int>(BlendMode::Multiply)] = sgl_make_pipeline(&pip_desc);
        }
        // Screen - screen blending
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            internal::blendPipelines[static_cast<int>(BlendMode::Screen)] = sgl_make_pipeline(&pip_desc);
        }
        // Subtract - subtractive blending
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = true;
            pip_desc.colors[0].blend.op_rgb = SG_BLENDOP_REVERSE_SUBTRACT;
            pip_desc.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
            pip_desc.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.op_alpha = SG_BLENDOP_ADD;  // Alpha remains additive
            pip_desc.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
            pip_desc.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE;
            internal::blendPipelines[static_cast<int>(BlendMode::Subtract)] = sgl_make_pipeline(&pip_desc);
        }
        // Disabled - no blending (overwrite)
        {
            sg_pipeline_desc pip_desc = {};
            pip_desc.colors[0].blend.enabled = false;
            internal::blendPipelines[static_cast<int>(BlendMode::Disabled)] = sgl_make_pipeline(&pip_desc);
        }

        internal::blendPipelinesInitialized = true;
        internal::currentBlendMode = BlendMode::Alpha;
    }
}

// ---------------------------------------------------------------------------
// Cleanup (shutdown)
// ---------------------------------------------------------------------------
void cleanup() {
    // Release blend mode pipelines
    if (internal::blendPipelinesInitialized) {
        for (int i = 0; i < 6; i++) {
            sgl_destroy_pipeline(internal::blendPipelines[i]);
        }
        internal::blendPipelinesInitialized = false;
    }
    // Release 3D pipeline
    if (internal::pipeline3dInitialized) {
        sgl_destroy_pipeline(internal::pipeline3d);
        internal::pipeline3dInitialized = false;
    }
    // Release font resources
    if (internal::fontInitialized) {
        sgl_destroy_pipeline(internal::fontPipeline);
        sg_destroy_sampler(internal::fontSampler);
        sg_destroy_view(internal::fontView);
        sg_destroy_image(internal::fontTexture);
        internal::fontInitialized = false;
    }
    sgl_shutdown();
    sg_shutdown();
}

// ---------------------------------------------------------------------------
// Clear screen (RGB float: 0.0 ~ 1.0)
// Works correctly in FBO or during swapchain pass (oF compatible)
// ---------------------------------------------------------------------------
void clear(float r, float g, float b, float a /* = 1.0f */) {
    // Skip in headless mode (no graphics context)
    if (headless::isActive()) return;

    if (internal::inFboPass && internal::fboClearColorFunc) {
        // During FBO pass, call FBO's clearColor() to restart pass
        internal::fboClearColorFunc(r, g, b, a);
    } else if (internal::inSwapchainPass) {
        // During swapchain pass
        BlendMode prevBlendMode = internal::currentBlendMode;

        sgl_push_matrix();
        sgl_matrix_mode_projection();
        sgl_push_matrix();

        sgl_load_identity();
        sgl_ortho(-1.0f, 1.0f, 1.0f, -1.0f, -1.0f, 1.0f);
        sgl_matrix_mode_modelview();
        sgl_load_identity();

        sgl_load_pipeline(internal::blendPipelines[static_cast<int>(BlendMode::Disabled)]);
        sgl_disable_texture();
        sgl_begin_quads();
        sgl_c4f(r, g, b, a);
        sgl_v2f(-1.0f, -1.0f);
        sgl_v2f( 1.0f, -1.0f);
        sgl_v2f( 1.0f,  1.0f);
        sgl_v2f(-1.0f,  1.0f);
        sgl_end();

        sgl_matrix_mode_projection();
        sgl_pop_matrix();
        sgl_matrix_mode_modelview();
        sgl_pop_matrix();

        sgl_load_pipeline(internal::blendPipelines[static_cast<int>(prevBlendMode)]);
    } else {
        // Outside pass, start new swapchain pass
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass.action.colors[0].clear_value = { r, g, b, a };
        // Also clear depth buffer (for 3D drawing)
        pass.action.depth.load_action = SG_LOADACTION_CLEAR;
        pass.action.depth.clear_value = 1.0f;
        pass.swapchain = sglue_swapchain();
        sg_begin_pass(&pass);
        internal::inSwapchainPass = true;
    }
}

} // namespace trussc
