// Deterministic GPU accounting regression for non-drawing ticks (#332).
// Validation must stay enabled even when build_all.py selects Release/NDEBUG.
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#define SOKOL_DEBUG
#define SOKOL_VALIDATE_NON_FATAL
#include "sokol/sokol_gfx.h"
#include "sokol/util/sokol_gl_tc.h"
#include "tc/app/tcGpuFrame.h"
#include "../common/tcCoreTest.h"
#include <cstdio>
#include <cstring>

extern "C" bool frame_gpu_work_from_c(void);

namespace {
int failures = 0;
int validationErrors = 0;
int repeatedUploads = 0;

void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

void logger(const char* tag, uint32_t level, uint32_t item, const char* message,
            uint32_t, const char*, void*) {
    if (std::strcmp(tag, "sg") == 0 && item == SG_LOGITEM_VALIDATE_UPDIMG_ONCE) {
        ++repeatedUploads;
    }
    if (level <= 1) {
        ++validationErrors;
        std::printf("%s validation item %u: %s\n", tag, item, message ? message : "");
    }
}

bool hasWork() {
    const bool cpp = tc_internal_gpu_frame_has_work();
    const bool c = frame_gpu_work_from_c();
    if (cpp != c) ++failures;
    return cpp;
}

bool endNonDrawingTick() {
    if (!hasWork()) return false;
    sg_commit();
    return true;
}

void drawOffscreen(sgl_context context, const sg_pass& pass) {
    sg_begin_pass(&pass);
    sgl_set_context(context);
    sgl_defaults();
    sgl_begin_triangles();
    sgl_v2f(0.0f, 0.0f);
    sgl_v2f(1.0f, 0.0f);
    sgl_v2f(0.0f, 1.0f);
    sgl_end();
    sgl_context_draw(context);
    sg_end_pass();
    // Fbo::end() reuses its shared context within the same frame this way.
    sgl_tc_context_reset(context);
}
} // namespace

TC_CORE_TEST_MAIN() {
    sg_desc desc = {};
    desc.logger.func = logger;
    sg_setup(&desc);
    sgl_desc_t glDesc = {};
    glDesc.logger.func = logger;
    sgl_setup(&glDesc);

    check("idle tick does not commit", !hasWork() && !endNonDrawingTick());

    sg_image_desc imageDesc = {};
    imageDesc.width = imageDesc.height = 2;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDesc.usage.stream_update = true;
    const sg_image texture = sg_make_image(&imageDesc);
    unsigned char pixels[16] = {};
    sg_image_data data = {};
    data.mip_levels[0] = {pixels, sizeof(pixels)};
    sg_update_image(texture, &data);
    check("image upload requests a commit", hasWork());
    endNonDrawingTick();
    check("image commit clears work stats", !hasWork());

    sg_buffer_desc bufferDesc = {};
    bufferDesc.size = 64;
    bufferDesc.usage.stream_update = true;
    const sg_buffer buffer = sg_make_buffer(&bufferDesc);
    const sg_range bytes = {pixels, sizeof(pixels)};
    sg_update_buffer(buffer, &bytes);
    check("buffer update requests a commit", hasWork());
    endNonDrawingTick();
    check("buffer update commit clears work stats", !hasWork());
    sg_append_buffer(buffer, &bytes);
    check("buffer append requests a commit", hasWork());
    endNonDrawingTick();
    check("buffer append commit clears work stats", !hasWork());

    imageDesc.usage = {};
    imageDesc.usage.color_attachment = true;
    const sg_image target = sg_make_image(&imageDesc);
    sg_view_desc viewDesc = {};
    viewDesc.color_attachment.image = target;
    const sg_view view = sg_make_view(&viewDesc);
    sg_pass pass = {};
    pass.attachments.colors[0] = view;
    sg_begin_pass(&pass);
    sg_end_pass();
    check("offscreen pass alone requests a commit", hasWork());
    endNonDrawingTick();
    check("offscreen pass commit clears work stats", !hasWork());

    sgl_context_desc_t contextDesc = {};
    contextDesc.max_vertices = 128;
    contextDesc.max_commands = 64;
    contextDesc.color_format = SG_PIXELFORMAT_RGBA8;
    contextDesc.depth_format = SG_PIXELFORMAT_NONE;
    contextDesc.sample_count = 1;
    const sgl_context context = sgl_make_context(&contextDesc);
    const sg_buffer vertices = _sgl_lookup_context(context.id)->vbuf;
    const size_t initialSize = sg_query_buffer_desc(vertices).size;
    bool bounded = initialSize > 0;
    bool ended = true;
    for (int tick = 0; tick < 1000; ++tick) {
        drawOffscreen(context, pass);
        ended = endNonDrawingTick() && !hasWork() && ended;
        const sg_buffer current = _sgl_lookup_context(context.id)->vbuf;
        bounded = bounded && sg_query_buffer_desc(current).size == initialSize;
    }
    check("1000 non-drawing ticks each end their GPU frame", ended);
    check("offscreen vertex buffer stays at its initial size", bounded);

    const int errorsBefore = repeatedUploads;
    for (int tick = 0; tick < 2; ++tick) {
        sg_update_image(texture, &data);
        if (tick == 1) drawOffscreen(context, pass);
        ended = endNonDrawingTick() && !hasWork() && ended;
    }
    check("consecutive uploads commit, including an upload-only tick", ended);
    check("consecutive uploads do not raise VALIDATE_UPDIMG_ONCE",
          repeatedUploads == errorsBefore);

    sgl_destroy_context(context);
    sg_destroy_view(view);
    sg_destroy_image(target);
    sg_destroy_buffer(buffer);
    sg_destroy_image(texture);
    sgl_shutdown();
    sg_shutdown();
    check("no sokol validation errors", validationErrors == 0);
    return failures ? 1 : 0;
}
