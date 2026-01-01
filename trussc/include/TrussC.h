#pragma once

// =============================================================================
// TrussC - Thin, Modern, and Native Creative Coding Framework
// Version 0.0.1
// =============================================================================

// Windows: Hide console window (in Release builds)
// Define TRUSSC_SHOW_CONSOLE to always show console
#if defined(_WIN32) && !defined(_DEBUG) && !defined(TRUSSC_SHOW_CONSOLE)
#pragma comment(linker, "/subsystem:\"windows\" /entry:\"mainCRTStartup\"")
#endif

// sokol headers
#include "sokol/sokol_log.h"
#define SOKOL_NO_ENTRY  // We define our own main()
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_glue.h"
#include "sokol/sokol_gl.h"

// Dear ImGui + sokol_imgui
#include "imgui/imgui.h"
#include "sokol/sokol_imgui.h"

// Standard libraries
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>
#include <fstream>

// Headless mode state (must be included early for graphics skip checks)
#include "tc/app/tcHeadlessState.h"

// Platform-specific headers for memory usage
#if defined(__APPLE__)
#include <mach/mach.h>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

// TrussC math library
#include "tcMath.h"

// TrussC direction/position specifiers
#include "tc/types/tcDirection.h"

// TrussC rectangle
#include "tc/types/tcRectangle.h"

// TrussC noise functions
#include "tc/math/tcNoise.h"

// TrussC ray (for hit testing)
#include "tc/math/tcRay.h"

// TrussC FFT (Fast Fourier Transform)
#include "tc/math/tcFFT.h"

// TrussC color library
#include "tcColor.h"

// TrussC bitmap font
#include "tcBitmapFont.h"

// TrussC platform-specific features
#include "tcPlatform.h"

// TrussC event system
#include "tc/events/tcCoreEvents.h"

// TrussC utilities
#include "tc/utils/tcUtils.h"
#include "tc/utils/tcTime.h"
#include "tc/utils/tcLog.h"

// TrussC file dialogs
#include "tc/utils/tcFileDialog.h"

// TrussC JSON/XML
#include "tc/utils/tcJson.h"
#include "tc/utils/tcXml.h"

// TrussC console input (accept commands from stdin)
#include "tc/utils/tcConsole.h"

// TrussC debug input (stdin-based input simulation)
#include "tc/utils/tcDebugInput.h"

// =============================================================================
// trussc namespace
// =============================================================================
namespace trussc {

// ---------------------------------------------------------------------------
// Blend mode
// ---------------------------------------------------------------------------
enum class BlendMode {
    Alpha,      // Normal alpha blending (default)
    Add,        // Additive blending
    Multiply,   // Multiply blending
    Screen,     // Screen blending
    Subtract,   // Subtractive blending
    Disabled    // No blending (overwrite)
};

// ---------------------------------------------------------------------------
// Texture filter
// ---------------------------------------------------------------------------
enum class TextureFilter {
    Nearest,    // Nearest neighbor (for pixel art)
    Linear      // Bilinear interpolation (default)
};

// ---------------------------------------------------------------------------
// Texture wrap mode
// ---------------------------------------------------------------------------
enum class TextureWrap {
    Repeat,         // Repeat
    ClampToEdge,    // Clamp to edge pixel (default)
    MirroredRepeat  // Mirrored repeat
};

// Forward declarations (for RenderContext)
namespace internal {
    inline sg_image fontTexture = {};
    inline sg_view fontView = {};
    inline sg_sampler fontSampler = {};
    inline sgl_pipeline fontPipeline = {};
    inline bool fontInitialized = false;
    inline sgl_pipeline pipeline3d = {};
    inline bool pipeline3dInitialized = false;
    inline bool pixelPerfectMode = false;

    // ImGui integration
    inline bool imguiEnabled = false;

    // Blend mode pipelines
    inline sgl_pipeline blendPipelines[6] = {};
    inline bool blendPipelinesInitialized = false;
    inline BlendMode currentBlendMode = BlendMode::Alpha;
}

} // namespace trussc (temporarily closed)

// RenderContext class (holds drawing state)
#include "tc/graphics/tcRenderContext.h"

// Reopen namespace
namespace trussc {

// Version information
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 0;
constexpr int VERSION_PATCH = 1;

// Math constants defined in tcMath.h: TAU, HALF_TAU, QUARTER_TAU, PI

// ---------------------------------------------------------------------------
// Internal state (application/window related)
// Drawing state has been moved to RenderContext
// ---------------------------------------------------------------------------
namespace internal {
    // Clipboard buffer size (for overflow check)
    inline int clipboardSize = 65536;

    // ---------------------------------------------------------------------------
    // Scissor Clipping stack
    // ---------------------------------------------------------------------------
    struct ScissorRect {
        float x, y, w, h;
        bool active;  // Whether a valid range exists in the stack
    };
    inline std::vector<ScissorRect> scissorStack;
    inline ScissorRect currentScissor = {0, 0, 0, 0, false};

    // ---------------------------------------------------------------------------
    // Loop Architecture (Decoupled Update/Draw)
    // ---------------------------------------------------------------------------

    // Special FPS values
    constexpr float VSYNC = -1.0f;        // Sync to monitor refresh rate
    constexpr float EVENT_DRIVEN = 0.0f;  // Only on redraw() call

    // FPS settings
    inline float updateTargetFps = VSYNC; // VSYNC, EVENT_DRIVEN, or fixed fps
    inline float drawTargetFps = VSYNC;   // VSYNC, EVENT_DRIVEN, or fixed fps
    inline bool updateSyncedToDraw = true; // true = update/draw in sync (1:1)
    inline int redrawCount = 1;            // redraw() counter (remaining draw count)

    // Update timing
    inline std::chrono::high_resolution_clock::time_point lastUpdateTime;
    inline bool lastUpdateTimeInitialized = false;
    inline double updateAccumulator = 0.0; // Accumulated time for independent Update

    // Draw timing (frame skip)
    inline std::chrono::high_resolution_clock::time_point lastDrawTime;
    inline bool lastDrawTimeInitialized = false;
    inline double drawAccumulator = 0.0;

    // Mouse state
    inline float mouseX = 0.0f;
    inline float mouseY = 0.0f;
    inline float pmouseX = 0.0f;  // Previous frame mouse position
    inline float pmouseY = 0.0f;
    inline int mouseButton = -1;  // Currently pressed button (-1 = none)
    inline bool mousePressed = false;

    // Frame rate measurement (10-frame moving average)
    inline double frameTimeBuffer[10] = {};
    inline int frameTimeIndex = 0;
    inline bool frameTimeBufferFilled = false;

    // Frame count (number of update calls)
    inline uint64_t updateFrameCount = 0;

    // Elapsed time measurement
    inline std::chrono::high_resolution_clock::time_point startTime;
    inline bool startTimeInitialized = false;

    // Pass state (for suspending swapchain pass for FBO)
    inline bool inSwapchainPass = false;

    // FBO pass state (for detecting when clear() is called inside FBO)
    inline bool inFboPass = false;

    // Current active FBO pipeline (used from clear())
    inline sgl_pipeline currentFboClearPipeline = {};
    inline sgl_pipeline currentFboBlendPipeline = {};

    // FBO clearColor function pointer (set in tcFbo.h)
    inline void (*fboClearColorFunc)(float, float, float, float) = nullptr;

    // Current active FBO pointer (used from clearColor)
    inline void* currentFbo = nullptr;
}

// ---------------------------------------------------------------------------
// Initialization and cleanup
// ---------------------------------------------------------------------------

// Initialize sokol_gfx + sokol_gl (call in setup callback)
inline void setup() {
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

    // Create pipeline for 3D drawing (based on sokol-samples sgl-sapp.c)
    if (!internal::pipeline3dInitialized) {
        sg_pipeline_desc pip_desc = {};
        pip_desc.cull_mode = SG_CULLMODE_NONE;  // No culling
        pip_desc.depth.write_enabled = true;
        pip_desc.depth.compare = SG_COMPAREFUNC_LESS_EQUAL;
        pip_desc.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
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

// Shutdown sokol_gfx + sokol_gl (call in cleanup callback)
inline void cleanup() {
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
// Frame control
// ---------------------------------------------------------------------------

// Get DPI scale (e.g., 2.0 for Retina displays)
inline float getDpiScale() {
    return sapp_dpi_scale();
}

// Get actual framebuffer size (in pixels)
inline int getFramebufferWidth() {
    return sapp_width();
}

inline int getFramebufferHeight() {
    return sapp_height();
}

// Call at frame start (before clear)
inline void beginFrame() {
    // Skip in headless mode (no graphics context)
    if (headless::isActive()) return;

    // Set default viewport
    sgl_defaults();
    sgl_matrix_mode_projection();

    if (internal::pixelPerfectMode) {
        // Pixel perfect: coordinate system = framebuffer size
        // near/far: specified as positive values (OpenGL convention)
        // Range that includes Z=0: specify symmetric range
        sgl_ortho(0.0f, (float)sapp_width(), (float)sapp_height(), 0.0f, -10000.0f, 10000.0f);
    } else {
        // Logical coordinate system: consider DPI scale
        float dpiScale = sapp_dpi_scale();
        float logicalWidth = (float)sapp_width() / dpiScale;
        float logicalHeight = (float)sapp_height() / dpiScale;
        sgl_ortho(0.0f, logicalWidth, logicalHeight, 0.0f, -10000.0f, 10000.0f);
    }

    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

// Clear screen (RGB float: 0.0 ~ 1.0)
// Works correctly in FBO or during swapchain pass (oF compatible)
inline void clear(float r, float g, float b, float a = 1.0f) {
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

// Clear screen (grayscale)
inline void clear(float gray, float a = 1.0f) {
    clear(gray, gray, gray, a);
}

// Clear screen (Color)
inline void clear(const Color& c) {
    clear(c.r, c.g, c.b, c.a);
}

// End pass and commit (call at end of draw)
inline void present() {
    // Skip in headless mode (no graphics context)
    if (headless::isActive()) return;

    sgl_draw();
    sg_end_pass();
    internal::inSwapchainPass = false;
    sg_commit();
}

// Get swapchain pass state (for FBO)
inline bool isInSwapchainPass() {
    return internal::inSwapchainPass;
}

// Suspend swapchain pass (for FBO)
// Call before FBO drawing to flush default context content and end pass
inline void suspendSwapchainPass() {
    if (internal::inSwapchainPass) {
        sgl_draw();  // Flush drawing commands from default context
        sg_end_pass();
        internal::inSwapchainPass = false;
    }
}

// Resume swapchain pass (for FBO)
// Call after FBO drawing to continue drawing to swapchain
inline void resumeSwapchainPass() {
    if (!internal::inSwapchainPass) {
        sg_pass pass = {};
        pass.action.colors[0].load_action = SG_LOADACTION_LOAD;  // Preserve existing content
        pass.action.depth.load_action = SG_LOADACTION_LOAD;
        pass.swapchain = sglue_swapchain();
        sg_begin_pass(&pass);
        internal::inSwapchainPass = true;
        // Reset sokol_gl state
        sgl_defaults();
        beginFrame();  // Reset projection matrix
    }
}

// ---------------------------------------------------------------------------
// Color settings (delegated to RenderContext)
// ---------------------------------------------------------------------------

// Set drawing color (float: 0.0 ~ 1.0)
inline void setColor(float r, float g, float b, float a = 1.0f) {
    getDefaultContext().setColor(r, g, b, a);
}

// Grayscale (0.0-1.0)
inline void setColor(float gray, float a = 1.0f) {
    getDefaultContext().setColor(gray, a);
}

// Set using Color struct
inline void setColor(const Color& c) {
    getDefaultContext().setColor(c);
}

// Get current drawing color
inline Color getColor() {
    return getDefaultContext().getColor();
}

// Set using HSB (H: 0-TAU, S: 0-1, B: 0-1)
inline void setColorHSB(float h, float s, float b, float a = 1.0f) {
    getDefaultContext().setColorHSB(h, s, b, a);
}

// Set using OKLab (L: 0-1, a: ~-0.4-0.4, b: ~-0.4-0.4)
inline void setColorOKLab(float L, float a_lab, float b_lab, float alpha = 1.0f) {
    getDefaultContext().setColorOKLab(L, a_lab, b_lab, alpha);
}

// Set using OKLCH (L: 0-1, C: 0-0.4, H: 0-TAU) - most perceptually natural
inline void setColorOKLCH(float L, float C, float H, float alpha = 1.0f) {
    getDefaultContext().setColorOKLCH(L, C, H, alpha);
}

// Enable fill
inline void fill() {
    getDefaultContext().fill();
}

// Disable fill
inline void noFill() {
    getDefaultContext().noFill();
}

// Enable stroke
inline void stroke() {
    getDefaultContext().stroke();
}

// Disable stroke
inline void noStroke() {
    getDefaultContext().noStroke();
}

// Stroke weight
inline void setStrokeWeight(float weight) {
    getDefaultContext().setStrokeWeight(weight);
}

// ---------------------------------------------------------------------------
// Scissor Clipping (drawing region restriction)
// ---------------------------------------------------------------------------

// Calculate intersection of two rectangles (internal helper)
inline void intersectRect(float x1, float y1, float w1, float h1,
                          float x2, float y2, float w2, float h2,
                          float& ox, float& oy, float& ow, float& oh) {
    float left = std::max(x1, x2);
    float top = std::max(y1, y2);
    float right = std::min(x1 + w1, x2 + w2);
    float bottom = std::min(y1 + h1, y2 + h2);
    ox = left;
    oy = top;
    ow = std::max(0.0f, right - left);
    oh = std::max(0.0f, bottom - top);
}

// Set scissor rectangle (screen coordinates)
inline void setScissor(float x, float y, float w, float h) {
    internal::currentScissor = {x, y, w, h, true};
    sgl_scissor_rectf(x, y, w, h, true);  // origin_top_left = true
}

// Set scissor rectangle (int version)
inline void setScissor(int x, int y, int w, int h) {
    setScissor((float)x, (float)y, (float)w, (float)h);
}

// Reset scissor to entire window
inline void resetScissor() {
    internal::currentScissor.active = false;
    sgl_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
}

// Save scissor to stack and set new range (intersection with current range)
inline void pushScissor(float x, float y, float w, float h) {
    // Save current state to stack
    internal::scissorStack.push_back(internal::currentScissor);

    // Calculate new range (intersection with current range)
    if (internal::currentScissor.active) {
        float nx, ny, nw, nh;
        intersectRect(internal::currentScissor.x, internal::currentScissor.y,
                      internal::currentScissor.w, internal::currentScissor.h,
                      x, y, w, h,
                      nx, ny, nw, nh);
        setScissor(nx, ny, nw, nh);
    } else {
        setScissor(x, y, w, h);
    }
}

// Restore scissor from stack
inline void popScissor() {
    if (internal::scissorStack.empty()) {
        resetScissor();
        return;
    }

    internal::currentScissor = internal::scissorStack.back();
    internal::scissorStack.pop_back();

    if (internal::currentScissor.active) {
        sgl_scissor_rectf(internal::currentScissor.x, internal::currentScissor.y,
                          internal::currentScissor.w, internal::currentScissor.h, true);
    } else {
        sgl_scissor_rect(0, 0, sapp_width(), sapp_height(), true);
    }
}

// ---------------------------------------------------------------------------
// Transformations (delegated to RenderContext)
// ---------------------------------------------------------------------------

// Save matrix to stack
inline void pushMatrix() {
    getDefaultContext().pushMatrix();
}

// Restore matrix from stack
inline void popMatrix() {
    getDefaultContext().popMatrix();
}

// Save style to stack (color, fill/stroke, textAlign, etc.)
inline void pushStyle() {
    getDefaultContext().pushStyle();
}

// Restore style from stack
inline void popStyle() {
    getDefaultContext().popStyle();
}

// Translation
inline void translate(Vec3 pos) {
    getDefaultContext().translate(pos);
}

inline void translate(float x, float y, float z) {
    getDefaultContext().translate(x, y, z);
}

inline void translate(float x, float y) {
    getDefaultContext().translate(x, y);
}

// Z-axis rotation (radians)
inline void rotate(float radians) {
    getDefaultContext().rotate(radians);
}

// X-axis rotation (radians)
inline void rotateX(float radians) {
    getDefaultContext().rotateX(radians);
}

// Y-axis rotation (radians)
inline void rotateY(float radians) {
    getDefaultContext().rotateY(radians);
}

// Z-axis rotation (radians) - explicit
inline void rotateZ(float radians) {
    getDefaultContext().rotateZ(radians);
}

// Rotation in degrees
inline void rotateDeg(float degrees) {
    getDefaultContext().rotateDeg(degrees);
}

inline void rotateXDeg(float degrees) {
    getDefaultContext().rotateXDeg(degrees);
}

inline void rotateYDeg(float degrees) {
    getDefaultContext().rotateYDeg(degrees);
}

inline void rotateZDeg(float degrees) {
    getDefaultContext().rotateZDeg(degrees);
}

// Scale (uniform)
inline void scale(float s) {
    getDefaultContext().scale(s);
}

// Scale (non-uniform 2D)
inline void scale(float sx, float sy) {
    getDefaultContext().scale(sx, sy);
}

// Scale (non-uniform 3D)
inline void scale(float sx, float sy, float sz) {
    getDefaultContext().scale(sx, sy, sz);
}

// Get current transformation matrix
inline Mat4 getCurrentMatrix() {
    return getDefaultContext().getCurrentMatrix();
}

// Reset transformation matrix
inline void resetMatrix() {
    getDefaultContext().resetMatrix();
}

// Set transformation matrix directly
inline void setMatrix(const Mat4& mat) {
    getDefaultContext().setMatrix(mat);
}

// ---------------------------------------------------------------------------
// Blend mode
// ---------------------------------------------------------------------------

// Set blend mode
// Alpha channel is additive in all modes (to prevent transparency when drawing to FBO)
inline void setBlendMode(BlendMode mode) {
    if (!internal::blendPipelinesInitialized) return;
    internal::currentBlendMode = mode;
    sgl_load_pipeline(internal::blendPipelines[static_cast<int>(mode)]);
}

// Get current blend mode
inline BlendMode getBlendMode() {
    return internal::currentBlendMode;
}

// Reset to default blend mode (Alpha)
inline void resetBlendMode() {
    setBlendMode(BlendMode::Alpha);
}

// ---------------------------------------------------------------------------
// 3D drawing mode
// ---------------------------------------------------------------------------

// Enable 3D drawing mode (depth test + back-face culling)
inline void enable3D() {
    if (internal::pipeline3dInitialized) {
        sgl_load_pipeline(internal::pipeline3d);
    }
}

// Enable 3D drawing mode (perspective)
// fov: field of view (radians), near/far: clip planes
inline void enable3DPerspective(float fovY = 0.785f, float nearZ = 0.1f, float farZ = 1000.0f) {
    if (internal::pipeline3dInitialized) {
        sgl_load_pipeline(internal::pipeline3d);
    }
    // Set perspective projection
    sgl_matrix_mode_projection();
    sgl_load_identity();
    float dpiScale = sapp_dpi_scale();
    float w = (float)sapp_width() / dpiScale;
    float h = (float)sapp_height() / dpiScale;
    float aspect = w / h;
    sgl_perspective(fovY, aspect, nearZ, farZ);
    sgl_matrix_mode_modelview();
    sgl_load_identity();
}

// Disable 3D drawing mode (return to default 2D drawing)
inline void disable3D() {
    sgl_load_default_pipeline();
    // Return to orthographic projection for 2D
    beginFrame();
}

// ---------------------------------------------------------------------------
// Basic shape drawing (delegated to RenderContext)
// ---------------------------------------------------------------------------

// Rectangle (top-left coordinate + size)
inline void drawRect(Vec3 pos, Vec2 size) {
    getDefaultContext().drawRect(pos, size);
}

inline void drawRect(Vec3 pos, float w, float h) {
    getDefaultContext().drawRect(pos, w, h);
}

inline void drawRect(float x, float y, float w, float h) {
    getDefaultContext().drawRect(x, y, w, h);
}

// Circle
inline void drawCircle(Vec3 center, float radius) {
    getDefaultContext().drawCircle(center, radius);
}

inline void drawCircle(float cx, float cy, float radius) {
    getDefaultContext().drawCircle(cx, cy, radius);
}

// Ellipse
inline void drawEllipse(Vec3 center, Vec2 radii) {
    getDefaultContext().drawEllipse(center, radii);
}

inline void drawEllipse(Vec3 center, float rx, float ry) {
    getDefaultContext().drawEllipse(center, rx, ry);
}

inline void drawEllipse(float cx, float cy, float rx, float ry) {
    getDefaultContext().drawEllipse(cx, cy, rx, ry);
}

// Line
inline void drawLine(Vec3 p1, Vec3 p2) {
    getDefaultContext().drawLine(p1, p2);
}

inline void drawLine(float x1, float y1, float x2, float y2) {
    getDefaultContext().drawLine(x1, y1, x2, y2);
}

// Triangle
inline void drawTriangle(Vec3 p1, Vec3 p2, Vec3 p3) {
    getDefaultContext().drawTriangle(p1, p2, p3);
}

inline void drawTriangle(float x1, float y1, float x2, float y2, float x3, float y3) {
    getDefaultContext().drawTriangle(x1, y1, x2, y2, x3, y3);
}

// Point
inline void drawPoint(Vec3 pos) {
    getDefaultContext().drawPoint(pos);
}

inline void drawPoint(float x, float y) {
    getDefaultContext().drawPoint(x, y);
}

// Set circle resolution
inline void setCircleResolution(int res) {
    getDefaultContext().setCircleResolution(res);
}

// ---------------------------------------------------------------------------
// Bitmap string drawing (delegated to RenderContext)
// ---------------------------------------------------------------------------

// Calculate text bounding box
inline void getBitmapStringBounds(const std::string& text, float& width, float& height) {
    float maxWidth = 0;
    float cursorX = 0;
    int lines = 1;

    for (char c : text) {
        if (c == '\n') {
            if (cursorX > maxWidth) maxWidth = cursorX;
            cursorX = 0;
            lines++;
        } else if (c == '\t') {
            cursorX += bitmapfont::GLYPH_WIDTH * 8;
        } else {
            cursorX += bitmapfont::GLYPH_WIDTH;
        }
    }
    if (cursorX > maxWidth) maxWidth = cursorX;

    width = maxWidth;
    height = lines * bitmapfont::CHAR_TEX_HEIGHT;
}

// Draw bitmap string
// screenFixed = true (default): fixed to screen (cancel rotation/scale)
// screenFixed = false: follow current matrix transformation (rotation/scale applied)
inline void drawBitmapString(const std::string& text, Vec3 pos, bool screenFixed = true) {
    getDefaultContext().drawBitmapString(text, pos, screenFixed);
}

inline void drawBitmapString(const std::string& text, float x, float y, bool screenFixed = true) {
    getDefaultContext().drawBitmapString(text, x, y, screenFixed);
}

// Draw bitmap string (with scale)
inline void drawBitmapString(const std::string& text, Vec3 pos, float scale) {
    getDefaultContext().drawBitmapString(text, pos, scale);
}

inline void drawBitmapString(const std::string& text, float x, float y, float scale) {
    getDefaultContext().drawBitmapString(text, x, y, scale);
}

// Draw bitmap string (with alignment)
inline void drawBitmapString(const std::string& text, Vec3 pos,
                              Direction h, Direction v) {
    getDefaultContext().drawBitmapString(text, pos, h, v);
}

inline void drawBitmapString(const std::string& text, float x, float y,
                              Direction h, Direction v) {
    getDefaultContext().drawBitmapString(text, x, y, h, v);
}

// Set text alignment
inline void setTextAlign(Direction h, Direction v) {
    getDefaultContext().setTextAlign(h, v);
}

// Get current text alignment
inline Direction getTextAlignH() {
    return getDefaultContext().getTextAlignH();
}

inline Direction getTextAlignV() {
    return getDefaultContext().getTextAlignV();
}

// Get bitmap font line height
inline float getBitmapFontHeight() {
    return getDefaultContext().getBitmapFontHeight();
}

// Get bitmap string width
inline float getBitmapStringWidth(const std::string& text) {
    return getDefaultContext().getBitmapStringWidth(text);
}

// Get bitmap string height
inline float getBitmapStringHeight(const std::string& text) {
    return getDefaultContext().getBitmapStringHeight(text);
}

// Get bitmap string bounding box
inline Rect getBitmapStringBBox(const std::string& text) {
    return getDefaultContext().getBitmapStringBBox(text);
}

// Draw bitmap string with background highlight
inline void drawBitmapStringHighlight(const std::string& text, float x, float y,
                                       const Color& background = Color(0, 0, 0),
                                       const Color& foreground = Color(1, 1, 1)) {
    if (text.empty()) return;

    // Calculate text size
    float textWidth, textHeight;
    getBitmapStringBounds(text, textWidth, textHeight);

    // Calculate exact height based on line count and lineHeight
    int lineCount = 1;
    for (char c : text) {
        if (c == '\n') lineCount++;
    }
    float lineHeight = bitmapfont::CHAR_TEX_HEIGHT;
    float exactHeight = lineCount * lineHeight;

    // Horizontal padding only (no vertical padding to prevent overlap with adjacent lines)
    const float paddingH = 4.0f;

    // Calculate offset based on current alignment
    float offsetX = 0, offsetY = 0;
    switch (getTextAlignH()) {
        case Direction::Left:   offsetX = 0; break;
        case Direction::Center: offsetX = -textWidth / 2; break;
        case Direction::Right:  offsetX = -textWidth; break;
        default: break;
    }
    switch (getTextAlignV()) {
        case Direction::Top:      offsetY = 0; break;
        case Direction::Center:   offsetY = -textHeight / 2; break;
        case Direction::Bottom:   offsetY = -textHeight; break;
        case Direction::Baseline: offsetY = -textHeight + 3; break;  // Approximate value
        default: break;
    }

    // Convert local coordinates to world coordinates (including offset)
    Mat4 currentMat = getCurrentMatrix();
    float worldX = currentMat.m[0]*(x + offsetX) + currentMat.m[1]*(y + offsetY) + currentMat.m[3];
    float worldY = currentMat.m[4]*(x + offsetX) + currentMat.m[5]*(y + offsetY) + currentMat.m[7];

    // Save matrix
    pushMatrix();
    resetMatrix();

    // Draw background with alpha blend pipeline
    // Height is exactly lineHeight * lineCount so adjacent lines don't overlap
    sgl_load_pipeline(internal::fontPipeline);
    setColor(background);
    drawRect(worldX - paddingH, worldY, textWidth + paddingH * 2, exactHeight);
    sgl_load_default_pipeline();

    popMatrix();

    // Draw text in foreground color
    setColor(foreground);
    drawBitmapString(text, x, y);
}

// ---------------------------------------------------------------------------
// Window control
// ---------------------------------------------------------------------------

// Set window title
inline void setWindowTitle(const std::string& title) {
    sapp_set_window_title(title.c_str());
}

// ---------------------------------------------------------------------------
// Clipboard
// ---------------------------------------------------------------------------

// Copy string to clipboard
inline void setClipboardString(const std::string& text) {
    if (static_cast<int>(text.size()) >= internal::clipboardSize) {
        logWarning("Clipboard") << "Text truncated (" << text.size() << " bytes > "
            << internal::clipboardSize << " buffer). Use WindowSettings::setClipboardSize() to increase.";
    }
    sapp_set_clipboard_string(text.c_str());
}

// Get string from clipboard
inline std::string getClipboardString() {
    const char* str = sapp_get_clipboard_string();
    return str ? str : "";
}

// Change window size (specify in size corresponding to coordinate system)
// pixelPerfect=true: specify in framebuffer size
// pixelPerfect=false: specify in logical size
inline void setWindowSize(int width, int height) {
    if (internal::pixelPerfectMode) {
        // Pixel perfect mode: convert framebuffer size to logical size
        float scale = sapp_dpi_scale();
        platform::setWindowSize(static_cast<int>(width / scale), static_cast<int>(height / scale));
    } else {
        // Logical coordinate mode: as is
        platform::setWindowSize(width, height);
    }
}

// Toggle fullscreen
inline void setFullscreen(bool full) {
    if (full != sapp_is_fullscreen()) {
        sapp_toggle_fullscreen();
    }
}

// Get fullscreen state
inline bool isFullscreen() {
    return sapp_is_fullscreen();
}

// Toggle fullscreen
inline void toggleFullscreen() {
    sapp_toggle_fullscreen();
}

// ---------------------------------------------------------------------------
// Window information (size corresponding to coordinate system)
// ---------------------------------------------------------------------------

// Get window width (size corresponding to coordinate system)
inline int getWindowWidth() {
    if (internal::pixelPerfectMode) {
        return sapp_width();  // Framebuffer size
    }
    return static_cast<int>(sapp_width() / sapp_dpi_scale());  // Logical size
}

// Get window height (size corresponding to coordinate system)
inline int getWindowHeight() {
    if (internal::pixelPerfectMode) {
        return sapp_height();  // Framebuffer size
    }
    return static_cast<int>(sapp_height() / sapp_dpi_scale());  // Logical size
}

// Aspect ratio
inline float getAspectRatio() {
    return static_cast<float>(sapp_width()) / static_cast<float>(sapp_height());
}

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------

inline double getElapsedTime() {
    auto now = std::chrono::high_resolution_clock::now();
    if (!internal::startTimeInitialized) {
        internal::startTime = now;
        internal::startTimeInitialized = true;
        return 0.0;
    }
    auto duration = std::chrono::duration<double>(now - internal::startTime);
    return duration.count();
}

// Number of update calls
// In decoupled mode, may be called more frequently than draw
inline uint64_t getUpdateCount() {
    return internal::updateFrameCount;
}

// Draw frame count (sokol's frame_count)
inline uint64_t getDrawCount() {
    return sapp_frame_count();
}

// Alias for getUpdateCount (for general use)
inline uint64_t getFrameCount() {
    return internal::updateFrameCount;
}

inline double getDeltaTime() {
    return sapp_frame_duration();
}

// Get frame rate (10-frame moving average)
inline double getFrameRate() {
    // Add current frame time to buffer
    double dt = sapp_frame_duration();
    internal::frameTimeBuffer[internal::frameTimeIndex] = dt;
    internal::frameTimeIndex = (internal::frameTimeIndex + 1) % 10;
    if (internal::frameTimeIndex == 0) {
        internal::frameTimeBufferFilled = true;
    }

    // Calculate average
    int count = internal::frameTimeBufferFilled ? 10 : internal::frameTimeIndex;
    if (count == 0) return 0.0;

    double sum = 0.0;
    for (int i = 0; i < count; i++) {
        sum += internal::frameTimeBuffer[i];
    }
    double avgDt = sum / count;
    return avgDt > 0.0 ? 1.0 / avgDt : 0.0;
}

// ---------------------------------------------------------------------------
// Mouse state (global / window coordinates)
// ---------------------------------------------------------------------------

// Current mouse X coordinate (window coordinates)
inline float getGlobalMouseX() {
    return internal::mouseX;
}

// Current mouse Y coordinate (window coordinates)
inline float getGlobalMouseY() {
    return internal::mouseY;
}

// Previous frame mouse X coordinate (window coordinates)
inline float getGlobalPMouseX() {
    return internal::pmouseX;
}

// Previous frame mouse Y coordinate (window coordinates)
inline float getGlobalPMouseY() {
    return internal::pmouseY;
}

// Is mouse button pressed
inline bool isMousePressed() {
    return internal::mousePressed;
}

// Currently pressed mouse button (-1 = none)
inline int getMouseButton() {
    return internal::mouseButton;
}

// Alias for getGlobalMouseX/Y (for tcDebugInput)
inline float getMouseX() { return internal::mouseX; }
inline float getMouseY() { return internal::mouseY; }

// ---------------------------------------------------------------------------
// System Information
// ---------------------------------------------------------------------------

// Get graphics backend name
inline std::string getBackendName() {
    switch (sg_query_backend()) {
        case SG_BACKEND_GLCORE: return "OpenGL";
        case SG_BACKEND_GLES3: return "OpenGL ES 3";
        case SG_BACKEND_D3D11: return "D3D11";
        case SG_BACKEND_METAL_IOS: return "Metal (iOS)";
        case SG_BACKEND_METAL_MACOS: return "Metal (macOS)";
        case SG_BACKEND_METAL_SIMULATOR: return "Metal (Simulator)";
        case SG_BACKEND_WGPU: return "WebGPU";
        default: return "Unknown";
    }
}

// Get process memory usage in bytes (resident set size)
inline size_t getMemoryUsage() {
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size;
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__linux__)
    // Read from /proc/self/status
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.substr(0, 6) == "VmRSS:") {
            size_t kb = 0;
            std::sscanf(line.c_str(), "VmRSS: %zu", &kb);
            return kb * 1024;
        }
    }
    return 0;
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Resource Counters (for debugging)
// ---------------------------------------------------------------------------
namespace internal {
    inline std::atomic<size_t> nodeCount{0};
    inline std::atomic<size_t> textureCount{0};
    inline std::atomic<size_t> fboCount{0};
}

inline size_t getNodeCount() { return internal::nodeCount.load(); }
inline size_t getTextureCount() { return internal::textureCount.load(); }
inline size_t getFboCount() { return internal::fboCount.load(); }

// ---------------------------------------------------------------------------
// Loop Architecture (Decoupled Update/Draw)
// ---------------------------------------------------------------------------

// Special FPS values (exposed to user)
constexpr float VSYNC = internal::VSYNC;              // Sync to monitor refresh rate
constexpr float EVENT_DRIVEN = internal::EVENT_DRIVEN; // Only on redraw() call

// FPS settings structure
struct FpsSettings {
    float updateFps;       // VSYNC(-1), EVENT_DRIVEN(0), or fixed fps
    float drawFps;         // VSYNC(-1), EVENT_DRIVEN(0), or fixed fps
    float actualVsyncFps;  // Actual monitor refresh rate (0 if unknown)
    bool synced;           // true = update/draw in sync (1:1)
};

// --- Main FPS API ---

// Set FPS (update and draw synchronized, 1:1)
// VSYNC: sync to monitor refresh rate
// EVENT_DRIVEN: only on redraw() call
// > 0: fixed fps
inline void setFps(float fps) {
    internal::updateTargetFps = fps;
    internal::drawTargetFps = fps;
    internal::updateSyncedToDraw = true;
    internal::updateAccumulator = 0.0;
    internal::drawAccumulator = 0.0;
}

// Set independent FPS for update and draw (not synchronized)
// Each loop runs at its own rate, may cause timing variations
inline void setIndependentFps(float updateFps, float drawFps) {
    internal::updateTargetFps = updateFps;
    internal::drawTargetFps = drawFps;
    internal::updateSyncedToDraw = false;
    internal::updateAccumulator = 0.0;
    internal::drawAccumulator = 0.0;
}

// Get current FPS settings
inline FpsSettings getFpsSettings() {
    FpsSettings settings;
    settings.updateFps = internal::updateTargetFps;
    settings.drawFps = internal::drawTargetFps;
    settings.synced = internal::updateSyncedToDraw;

    // Get actual VSync frequency (approximate from frame duration when VSYNC)
    if (internal::updateTargetFps == internal::VSYNC ||
        internal::drawTargetFps == internal::VSYNC) {
        double dt = sapp_frame_duration();
        settings.actualVsyncFps = (dt > 0.0) ? static_cast<float>(1.0 / dt) : 0.0f;
    } else {
        settings.actualVsyncFps = 0.0f;
    }

    return settings;
}

// Get current actual FPS (measured, 10-frame moving average)
// Alias for getFrameRate() with clearer naming
inline float getFps() {
    return static_cast<float>(getFrameRate());
}

// Request redraw (used when auto-draw is stopped)
// count: number of draws (max value is used when called multiple times)
inline void redraw(int count = 1) {
    if (count > internal::redrawCount) {
        internal::redrawCount = count;
    }
}

// Request application exit
// Called in order: exit() -> cleanup() -> destructor
inline void exitApp() {
    sapp_request_quit();
}

// ---------------------------------------------------------------------------
// Screenshot
// ---------------------------------------------------------------------------

// Save screenshot (uses OS window capture feature)
// Supported formats: .png, .jpg/.jpeg, .tiff/.tif, .bmp
inline bool saveScreenshot(const std::filesystem::path& path) {
    return platform::saveScreenshot(path);
}

// Capture screen to Pixels
inline bool grabScreen(Pixels& outPixels) {
    return platform::captureWindow(outPixels);
}

// ---------------------------------------------------------------------------
// Math utilities (provided by tcMath.h)
// ---------------------------------------------------------------------------
// Vec2, Vec3, Vec4, Mat3, Mat4, lerp, clamp, map, radians, degrees, etc.
// See tcMath.h for details

// ---------------------------------------------------------------------------
// Key code constants (wraps sokol_app key codes)
// ---------------------------------------------------------------------------

// Special keys
constexpr int KEY_SPACE = SAPP_KEYCODE_SPACE;
constexpr int KEY_ESCAPE = SAPP_KEYCODE_ESCAPE;
constexpr int KEY_ENTER = SAPP_KEYCODE_ENTER;
constexpr int KEY_TAB = SAPP_KEYCODE_TAB;
constexpr int KEY_BACKSPACE = SAPP_KEYCODE_BACKSPACE;
constexpr int KEY_DELETE = SAPP_KEYCODE_DELETE;

// Arrow keys
constexpr int KEY_RIGHT = SAPP_KEYCODE_RIGHT;
constexpr int KEY_LEFT = SAPP_KEYCODE_LEFT;
constexpr int KEY_DOWN = SAPP_KEYCODE_DOWN;
constexpr int KEY_UP = SAPP_KEYCODE_UP;

// Modifier keys
constexpr int KEY_LEFT_SHIFT = SAPP_KEYCODE_LEFT_SHIFT;
constexpr int KEY_RIGHT_SHIFT = SAPP_KEYCODE_RIGHT_SHIFT;
constexpr int KEY_LEFT_CONTROL = SAPP_KEYCODE_LEFT_CONTROL;
constexpr int KEY_RIGHT_CONTROL = SAPP_KEYCODE_RIGHT_CONTROL;
constexpr int KEY_LEFT_ALT = SAPP_KEYCODE_LEFT_ALT;
constexpr int KEY_RIGHT_ALT = SAPP_KEYCODE_RIGHT_ALT;
constexpr int KEY_LEFT_SUPER = SAPP_KEYCODE_LEFT_SUPER;
constexpr int KEY_RIGHT_SUPER = SAPP_KEYCODE_RIGHT_SUPER;

// Function keys
constexpr int KEY_F1 = SAPP_KEYCODE_F1;
constexpr int KEY_F2 = SAPP_KEYCODE_F2;
constexpr int KEY_F3 = SAPP_KEYCODE_F3;
constexpr int KEY_F4 = SAPP_KEYCODE_F4;
constexpr int KEY_F5 = SAPP_KEYCODE_F5;
constexpr int KEY_F6 = SAPP_KEYCODE_F6;
constexpr int KEY_F7 = SAPP_KEYCODE_F7;
constexpr int KEY_F8 = SAPP_KEYCODE_F8;
constexpr int KEY_F9 = SAPP_KEYCODE_F9;
constexpr int KEY_F10 = SAPP_KEYCODE_F10;
constexpr int KEY_F11 = SAPP_KEYCODE_F11;
constexpr int KEY_F12 = SAPP_KEYCODE_F12;

// Mouse buttons
constexpr int MOUSE_BUTTON_LEFT = SAPP_MOUSEBUTTON_LEFT;
constexpr int MOUSE_BUTTON_RIGHT = SAPP_MOUSEBUTTON_RIGHT;
constexpr int MOUSE_BUTTON_MIDDLE = SAPP_MOUSEBUTTON_MIDDLE;

// ---------------------------------------------------------------------------
// Window settings
// ---------------------------------------------------------------------------

struct WindowSettings {
    int width = 1280;
    int height = 720;
    std::string title = "TrussC App";
    bool highDpi = true;  // High DPI support (sharp rendering on Retina)
    bool pixelPerfect = false;  // true: coords = framebuffer size, false: coords = logical size
    int sampleCount = 4;  // MSAA (default 4x, 8x not supported on some devices)
    bool fullscreen = false;
    int clipboardSize = 65536;  // Clipboard buffer size (default 64KB)
    bool enableDebugInput = false;  // Enable tcdebug input simulation (security: opt-in)
    // bool headless = false;  // For future use

    WindowSettings& setSize(int w, int h) {
        width = w;
        height = h;
        return *this;
    }

    WindowSettings& setTitle(const std::string& t) {
        title = t;
        return *this;
    }

    WindowSettings& setHighDpi(bool enabled) {
        highDpi = enabled;
        return *this;
    }

    // Pixel perfect mode
    // true: coords match framebuffer size (2560x1440 coords on Retina)
    // false: coords are logical size (1280x720 coords even on Retina)
    WindowSettings& setPixelPerfect(bool enabled) {
        pixelPerfect = enabled;
        return *this;
    }

    WindowSettings& setSampleCount(int count) {
        sampleCount = count;
        return *this;
    }

    WindowSettings& setFullscreen(bool enabled) {
        fullscreen = enabled;
        return *this;
    }

    WindowSettings& setClipboardSize(int size) {
        clipboardSize = size;
        return *this;
    }

    WindowSettings& setEnableDebugInput(bool enabled) {
        enableDebugInput = enabled;
        return *this;
    }
};

// ---------------------------------------------------------------------------
// Application execution (internal implementation)
// ---------------------------------------------------------------------------

namespace internal {
    // App instance (held as void*)
    inline void* appInstance = nullptr;
    inline int currentMouseButton = -1;

    // Callback function pointers
    inline void (*appSetupFunc)() = nullptr;
    inline void (*appUpdateFunc)() = nullptr;
    inline void (*appDrawFunc)() = nullptr;
    inline void (*appCleanupFunc)() = nullptr;
    inline void (*appKeyPressedFunc)(int) = nullptr;
    inline void (*appKeyReleasedFunc)(int) = nullptr;
    inline void (*appMousePressedFunc)(int, int, int) = nullptr;
    inline void (*appMouseReleasedFunc)(int, int, int) = nullptr;
    inline void (*appMouseMovedFunc)(int, int) = nullptr;
    inline void (*appMouseDraggedFunc)(int, int, int) = nullptr;
    inline void (*appMouseScrolledFunc)(float, float) = nullptr;
    inline void (*appWindowResizedFunc)(int, int) = nullptr;
    inline void (*appFilesDroppedFunc)(const std::vector<std::string>&) = nullptr;

    inline void _setup_cb() {
        setup();

        // For macOS bundles, set default data path
        // Executable: bin/xxx.app/Contents/MacOS/xxx
        // data: bin/data/
        // ../../../ = bin/, so ../../../data/ is the correct path
        #ifdef __APPLE__
        setDataPathRoot("../../../data/");
        #endif

        // Start console input thread (enabled by default)
        // To disable, call console::stop() in setup()
        // Note: Console is not available on web platform (no stdin/threads)
        #ifndef __EMSCRIPTEN__
        console::start();

        // Register built-in command handler (hold listener in static)
        static EventListener consoleListener;
        events().console.listen(consoleListener, [](ConsoleEventArgs& e) {
            debuginput::handleCommand(e);
        });
        #endif

        if (appSetupFunc) appSetupFunc();
    }

    inline void _frame_cb() {
        auto now = std::chrono::high_resolution_clock::now();

        // Initialize timing
        if (!lastUpdateTimeInitialized) {
            lastUpdateTime = now;
            lastUpdateTimeInitialized = true;
        }
        if (!lastDrawTimeInitialized) {
            lastDrawTime = now;
            lastDrawTimeInitialized = true;
        }

        // Process console input (fire events)
        console::processQueue();

        // --- Update Loop processing ---
        if (updateSyncedToDraw) {
            // Synced to Draw: handled with shouldDraw below
        } else if (updateTargetFps == VSYNC) {
            // VSYNC mode (independent): update every frame
            if (appUpdateFunc) appUpdateFunc();
        } else if (updateTargetFps > 0) {
            // Independent fixed Hz Update
            double updateInterval = 1.0 / updateTargetFps;
            double elapsed = std::chrono::duration<double>(now - lastUpdateTime).count();
            updateAccumulator += elapsed;
            lastUpdateTime = now;

            while (updateAccumulator >= updateInterval) {
                if (appUpdateFunc) appUpdateFunc();
                updateAccumulator -= updateInterval;
            }
        }
        // If updateTargetFps == EVENT_DRIVEN (0), no Update (event-driven)

        // --- Draw Loop processing ---
        bool shouldDraw = false;

        if (drawTargetFps == VSYNC) {
            // VSync: draw every frame (sokol_app controls timing)
            shouldDraw = true;
        } else if (drawTargetFps > 0) {
            // Fixed FPS: controlled by frame skipping
            double drawInterval = 1.0 / drawTargetFps;
            double elapsed = std::chrono::duration<double>(now - lastDrawTime).count();
            drawAccumulator += elapsed;
            lastDrawTime = now;

            if (drawAccumulator >= drawInterval) {
                shouldDraw = true;
                // Consume only one frame (draw once even if multiple frames accumulated)
                drawAccumulator -= drawInterval;
                // Prevent over-accumulation
                if (drawAccumulator > drawInterval) {
                    drawAccumulator = 0.0;
                }
            }
        } else {
            // EVENT_DRIVEN (0): draw only on redraw()
            shouldDraw = (redrawCount > 0);
        }

        if (shouldDraw) {
            beginFrame();

            // If Update is synced to Draw, call Update here
            if (updateSyncedToDraw && appUpdateFunc) {
                appUpdateFunc();
            }

            if (appDrawFunc) appDrawFunc();
            present();

            // Decrement redrawCount (don't go below 0)
            if (redrawCount > 0) {
                redrawCount--;
            }
        } else {
            // Skip Present when not drawing (prevent double-buffer flickering)
            sapp_skip_present();
        }

        // Save previous frame's mouse position
        pmouseX = mouseX;
        pmouseY = mouseY;
    }

    inline void _cleanup_cb() {
        // Stop console input thread
        console::stop();

        if (appCleanupFunc) appCleanupFunc();
        cleanup();
    }

    inline void _event_cb(const sapp_event* ev) {
        // Pass event to ImGui
        if (imguiEnabled) {
            simgui_handle_event(ev);
        }

        // ev->mouse_x/y arrive in framebuffer coordinates
        // pixelPerfectMode = true: use as-is (coords = framebuffer size)
        // pixelPerfectMode = false: divide by DPI scale to get logical coords
        float scale = pixelPerfectMode ? 1.0f : (1.0f / sapp_dpi_scale());
        bool hasModShift = (ev->modifiers & SAPP_MODIFIER_SHIFT) != 0;
        bool hasModCtrl = (ev->modifiers & SAPP_MODIFIER_CTRL) != 0;
        bool hasModAlt = (ev->modifiers & SAPP_MODIFIER_ALT) != 0;
        bool hasModSuper = (ev->modifiers & SAPP_MODIFIER_SUPER) != 0;

        switch (ev->type) {
            case SAPP_EVENTTYPE_KEY_DOWN: {
                // Notify event system
                KeyEventArgs args;
                args.key = ev->key_code;
                args.isRepeat = ev->key_repeat;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().keyPressed.notify(args);

                // Stream output (only for real user input, not repeats)
                if (!ev->key_repeat) {
                    debuginput::streamOutputKey("key_press", ev->key_code);
                }

                // Legacy callback (for compatibility)
                if (!ev->key_repeat && appKeyPressedFunc) {
                    appKeyPressedFunc(ev->key_code);
                }
                break;
            }
            case SAPP_EVENTTYPE_KEY_UP: {
                KeyEventArgs args;
                args.key = ev->key_code;
                args.isRepeat = false;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().keyReleased.notify(args);

                // Stream output
                debuginput::streamOutputKey("key_release", ev->key_code);

                if (appKeyReleasedFunc) appKeyReleasedFunc(ev->key_code);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_DOWN: {
                currentMouseButton = ev->mouse_button;
                mouseX = ev->mouse_x * scale;
                mouseY = ev->mouse_y * scale;
                mouseButton = ev->mouse_button;
                mousePressed = true;

                MouseEventArgs args;
                args.x = mouseX;
                args.y = mouseY;
                args.button = ev->mouse_button;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().mousePressed.notify(args);

                // Stream output + drag tracking for normal mode
                debuginput::streamOutput("mouse_press", mouseX, mouseY, ev->mouse_button);
                debuginput::isDragging = true;
                debuginput::dragStartX = mouseX;
                debuginput::dragStartY = mouseY;
                debuginput::dragButton = ev->mouse_button;

                if (appMousePressedFunc) appMousePressedFunc((int)mouseX, (int)mouseY, ev->mouse_button);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_UP: {
                currentMouseButton = -1;
                mouseX = ev->mouse_x * scale;
                mouseY = ev->mouse_y * scale;
                mouseButton = -1;
                mousePressed = false;

                MouseEventArgs args;
                args.x = mouseX;
                args.y = mouseY;
                args.button = ev->mouse_button;
                args.shift = hasModShift;
                args.ctrl = hasModCtrl;
                args.alt = hasModAlt;
                args.super = hasModSuper;
                events().mouseReleased.notify(args);

                // Stream output
                debuginput::streamOutput("mouse_release", mouseX, mouseY, ev->mouse_button);
                debuginput::isDragging = false;

                if (appMouseReleasedFunc) appMouseReleasedFunc((int)mouseX, (int)mouseY, ev->mouse_button);
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_MOVE: {
                float prevX = mouseX;
                float prevY = mouseY;
                mouseX = ev->mouse_x * scale;
                mouseY = ev->mouse_y * scale;

                if (currentMouseButton >= 0) {
                    MouseDragEventArgs args;
                    args.x = mouseX;
                    args.y = mouseY;
                    args.deltaX = mouseX - prevX;
                    args.deltaY = mouseY - prevY;
                    args.button = currentMouseButton;
                    events().mouseDragged.notify(args);

                    // Stream output: detail mode outputs all moves, normal mode skips during drag
                    if (debuginput::streamMode == debuginput::StreamMode::Detail) {
                        debuginput::streamOutput("mouse_move", mouseX, mouseY, currentMouseButton);
                    }

                    if (appMouseDraggedFunc) appMouseDraggedFunc((int)mouseX, (int)mouseY, currentMouseButton);
                } else {
                    MouseMoveEventArgs args;
                    args.x = mouseX;
                    args.y = mouseY;
                    args.deltaX = mouseX - prevX;
                    args.deltaY = mouseY - prevY;
                    events().mouseMoved.notify(args);

                    // Stream output: detail mode only (normal mode ignores mouse move without button)
                    if (debuginput::streamMode == debuginput::StreamMode::Detail) {
                        debuginput::streamOutput("mouse_move", mouseX, mouseY);
                    }

                    if (appMouseMovedFunc) appMouseMovedFunc((int)mouseX, (int)mouseY);
                }
                break;
            }
            case SAPP_EVENTTYPE_MOUSE_SCROLL: {
                ScrollEventArgs args;
                args.scrollX = ev->scroll_x;
                args.scrollY = ev->scroll_y;
                events().mouseScrolled.notify(args);

                // Stream output
                debuginput::streamOutputScroll(ev->scroll_x, ev->scroll_y);

                if (appMouseScrolledFunc) appMouseScrolledFunc(ev->scroll_x, ev->scroll_y);
                break;
            }
            case SAPP_EVENTTYPE_RESIZED: {
                int w = static_cast<int>(ev->window_width * scale);
                int h = static_cast<int>(ev->window_height * scale);

                ResizeEventArgs args;
                args.width = w;
                args.height = h;
                events().windowResized.notify(args);

                if (appWindowResizedFunc) appWindowResizedFunc(w, h);
                break;
            }
            case SAPP_EVENTTYPE_FILES_DROPPED: {
                DragDropEventArgs args;
                args.x = mouseX;  // Last mouse position
                args.y = mouseY;
                int numFiles = sapp_get_num_dropped_files();
                for (int i = 0; i < numFiles; i++) {
                    args.files.push_back(sapp_get_dropped_file_path(i));
                }
                events().filesDropped.notify(args);

                // Stream output
                debuginput::streamOutputDrop(args.files);

                if (appFilesDroppedFunc) appFilesDroppedFunc(args.files);
                break;
            }
            default:
                break;
        }
    }
}

// ---------------------------------------------------------------------------
// Application execution
// ---------------------------------------------------------------------------

template<typename AppClass>
int runApp(const WindowSettings& settings = WindowSettings()) {
    // Set pixel perfect mode
    internal::pixelPerfectMode = settings.pixelPerfect;

    // Set debug input mode
    debuginput::enabled = settings.enableDebugInput;

    // Create app instance
    static AppClass* app = nullptr;

    // Set callbacks
    internal::appSetupFunc = []() {
        app = new AppClass();
        // Note: setup() is called automatically in updateTree() via setupCalled_ flag
    };
    internal::appUpdateFunc = []() {
        internal::updateFrameCount++;  // Update frame count
        if (app) {
            app->handleUpdate(internal::mouseX, internal::mouseY);
        }
    };
    internal::appDrawFunc = []() {
        if (app) app->handleDraw();
    };
    internal::appCleanupFunc = []() {
        if (app) {
            app->exit();
            app->cleanup();
            delete app;
            app = nullptr;
        }
    };
    internal::appKeyPressedFunc = [](int key) {
        if (app) app->handleKeyPressed(key);
    };
    internal::appKeyReleasedFunc = [](int key) {
        if (app) app->handleKeyReleased(key);
    };
    internal::appMousePressedFunc = [](int x, int y, int button) {
        if (app) app->handleMousePressed(x, y, button);
    };
    internal::appMouseReleasedFunc = [](int x, int y, int button) {
        if (app) app->handleMouseReleased(x, y, button);
    };
    internal::appMouseMovedFunc = [](int x, int y) {
        if (app) app->handleMouseMoved(x, y);
    };
    internal::appMouseDraggedFunc = [](int x, int y, int button) {
        if (app) app->handleMouseDragged(x, y, button);
    };
    internal::appMouseScrolledFunc = [](float dx, float dy) {
        if (app) app->handleMouseScrolled(dx, dy, internal::mouseX, internal::mouseY);
    };
    internal::appWindowResizedFunc = [](int w, int h) {
        if (app) app->windowResized(w, h);
    };
    internal::appFilesDroppedFunc = [](const std::vector<std::string>& files) {
        if (app) app->filesDropped(files);
    };

    // Build sapp_desc
    sapp_desc desc = {};
    if (settings.pixelPerfect) {
        // For pixel perfect, treat specified size as framebuffer size
        // and convert to logical window size
        float displayScale = platform::getDisplayScaleFactor();
        desc.width = static_cast<int>(settings.width / displayScale);
        desc.height = static_cast<int>(settings.height / displayScale);
    } else {
        desc.width = settings.width;
        desc.height = settings.height;
    }
    desc.window_title = settings.title.c_str();
    desc.high_dpi = settings.highDpi;
    desc.sample_count = settings.sampleCount;
    desc.fullscreen = settings.fullscreen;
    desc.init_cb = internal::_setup_cb;
    desc.frame_cb = internal::_frame_cb;
    desc.cleanup_cb = internal::_cleanup_cb;
    desc.event_cb = internal::_event_cb;
    desc.logger.func = slog_func;

    // Enable drag and drop
    desc.enable_dragndrop = true;
    desc.max_dropped_files = 16;           // Max 16 files
    desc.max_dropped_file_path_length = 2048;  // Max path length

    // Enable clipboard
    desc.enable_clipboard = true;
    desc.clipboard_size = settings.clipboardSize;
    internal::clipboardSize = settings.clipboardSize;

    // Run the app
    sapp_run(&desc);

    return 0;
}

} // namespace trussc

// TrussC shape drawing
#include "tc/graphics/tcShape.h"

// TrussC polyline
#include "tc/graphics/tcPolyline.h"

// TrussC lighting (must be included before tcMesh.h)
#include "tc/3d/tcLightingState.h"
#include "tc/3d/tcMaterial.h"
#include "tc/3d/tcLight.h"

// TrussC pixel buffer
#include "tc/graphics/tcPixels.h"

// TrussC texture (needed before tcMesh.h)
#include "tc/gpu/tcTexture.h"

// TrussC HasTexture interface
#include "tc/gpu/tcHasTexture.h"

// TrussC image (needed before tcMesh.h)
#include "tc/graphics/tcImage.h"

// TrussC mesh
#include "tc/graphics/tcMesh.h"

// TrussC stroke mesh (thick line drawing)
#include "tc/graphics/tcStrokeMesh.h"
#include "tc/graphics/tcFont.h"

// TrussC FBO (offscreen rendering)
#include "tc/gpu/tcFbo.h"

// TrussC custom shader
#include "tc/gpu/tcShader.h"

// TrussC video input (webcam)
#include "tc/video/tcVideoGrabber.h"

// TrussC video playback
#include "tc/video/tcVideoPlayer.h"

// TrussC 3D primitives
#include <map>
#include "tc/3d/tcPrimitives.h"

// TrussC lighting API
#include "tc/3d/tc3DGraphics.h"

// TrussC EasyCam (3D camera)
#include "tc/3d/tcEasyCam.h"

// TrussC ImGui integration
#include "tc/gui/tcImGui.h"

// TrussC network
#include "tc/network/tcUdpSocket.h"
#include "tc/network/tcTcpClient.h"
#include "tc/network/tcTcpServer.h"

// TrussC serial communication
#include "tc/comm/tcSerial.h"

// TrussC sound
#include "tc/sound/tcSound.h"
#include "tc/sound/tcChipSound.h"

// TrussC threading
#include "tc/utils/tcThread.h"
#include "tc/utils/tcThreadChannel.h"

// TrussC application base class
#include "tcBaseApp.h"

// TrussC headless mode (no window)
#include "tc/app/tcHeadlessApp.h"

// =============================================================================
// Standard library includes (convenience)
// =============================================================================

// Containers
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include <array>
#include <deque>
#include <queue>
#include <stack>

// Strings & streams
#include <string>
#include <sstream>
#include <iostream>
#include <fstream>

// Memory & smart pointers
#include <memory>

// Functional
#include <functional>

// Algorithms & utilities
#include <algorithm>
#include <utility>
#include <optional>
#include <variant>
#include <tuple>

// Numerics
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <random>

// Threading
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// =============================================================================
// Namespace alias
// =============================================================================
namespace tc = trussc;

// Users should add these in their code:
//   using namespace std;
//   using namespace tc;
