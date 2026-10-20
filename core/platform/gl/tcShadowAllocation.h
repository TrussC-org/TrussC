#pragma once
#include <cstdio>

// Only the host's shadow creation calls use this observation window. In
// particular, pending errors from application GL calls are not allocation errors.
#if defined(SOKOL_GLES3)
#include <GLES3/gl3.h>
#else
#ifndef GL_GLEXT_PROTOTYPES
#define GL_GLEXT_PROTOTYPES
#endif
#include <GL/gl.h>
#include <GL/glext.h>
#endif

namespace trussc::internal {
struct GLPendingErrors {
    // Bound (platform): GL has fewer error kinds than this; a faulty driver's extra codes are drained unreported. Overflow drops those Warnings and is not reported.
    GLenum codes[8];
    int count = 0;
};

// Draining isolates creation errors, but application diagnostics must stay
// observable. Capture them here and report them only after the creation, so
// a log listener that leaves its own GL error cannot restart this loop.
static GLPendingErrors beginGLShadowCreation() {
    GLPendingErrors pending;
    // Bound (platform): GL keeps one flag per error kind, so a real driver stops after a few reads. Overflow leaves a faulty driver's remaining errors to the creation check, which fails this allocation and reports it.
    for (int i = 0; i < 64; ++i) {
        const GLenum error = glGetError();
        if (error == GL_NO_ERROR) break;
        if (pending.count < 8) pending.codes[pending.count++] = error;
    }
    return pending;
}

static bool endGLShadowCreation() {
    bool valid = true;
    // Bound (platform): as above. Overflow leaves a faulty driver's remaining errors pending and is reported by this allocation failing.
    for (int i = 0; i < 64 && glGetError() != GL_NO_ERROR; ++i) valid = false;
    return valid;
}

// Errors a listener raises while handling these Warnings stay pending for the
// application, like any other GL error after shadow allocation.
static void reportGLPendingErrors(const GLPendingErrors& pending) {
    for (int i = 0; i < pending.count; ++i) {
        char message[128];
        std::snprintf(message, sizeof(message),
            "GL error 0x%04X was left pending by earlier GL code, not by shadows; cleared before shadow allocation",
            unsigned(pending.codes[i]));
        try { getLogger().log(LogLevel::Warning, message); }
        catch (...) { std::fprintf(stderr, "[WARNING] %s\n", message); } // log-check: allow (allocation-free fallback)
    }
}

bool shadowNativeValid(sg_image image) {
    if (sg_query_backend() != SG_BACKEND_GLCORE && sg_query_backend() != SG_BACKEND_GLES3) return true;
    const auto info = sg_gl_query_image_info(image);
    return info.tex[info.active_slot] != 0;
}

bool shadowNativeValid(sg_view view) {
    if (sg_query_backend() != SG_BACKEND_GLCORE && sg_query_backend() != SG_BACKEND_GLES3) return true;
    const auto desc = sg_query_view_desc(view);
    if (desc.texture.image.id && sg_query_features().gl_texture_views)
        return sg_gl_query_view_info(view).tex_view[0] != 0;
    // Single-sample attachment views (and GLES sampling views) borrow the
    // image's native name; they have no separately allocated native object.
    return shadowNativeValid(desc.texture.image.id ? desc.texture.image :
        desc.color_attachment.image.id ? desc.color_attachment.image : desc.depth_stencil_attachment.image);
}

bool shadowNativeValid(sg_sampler sampler) {
    if (sg_query_backend() != SG_BACKEND_GLCORE && sg_query_backend() != SG_BACKEND_GLES3) return true;
    return sg_gl_query_sampler_info(sampler).smp != 0;
}
} // namespace trussc::internal
