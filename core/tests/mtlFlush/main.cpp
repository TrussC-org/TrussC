// #270: shared frame bookkeeping on dummy; actual submission/uniform checks
// on Metal. No display is needed. Does not test drawable presentation.
#define SOKOL_IMPL
#include "sokol_gfx.h"
#include "sokol_log.h"
#include "util/sokol_gl_tc.h"
#include <cstdio>

namespace {
int failures = 0;
int commits = 0;
void check(const char* name, bool ok) {
    std::printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void onCommit(void*) { ++commits; }
void triangle() {
    sgl_begin_triangles();
    sgl_v2f(-1, -1); sgl_v2f(1, -1); sgl_v2f(0, 1);
    sgl_end();
}
}

int main() {
#if defined(SOKOL_METAL)
    @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (!device) { std::printf("SKIP: no Metal device\n"); return 0; }
#endif
    sg_desc desc = {};
    desc.logger.func = slog_func;
    desc.environment.defaults.color_format = SG_PIXELFORMAT_RGBA8;
    desc.environment.defaults.depth_format = SG_PIXELFORMAT_NONE;
    desc.environment.defaults.sample_count = 1;
#if defined(SOKOL_METAL)
    desc.environment.metal.device = (__bridge const void*)device;
#endif
    sg_setup(&desc);
    sgl_desc_t glDesc = {};
    glDesc.logger.func = slog_func;
    sgl_setup(&glDesc);
    const sg_commit_listener listener = {onCommit, nullptr};
    check("commit listener installed", sg_add_commit_listener(listener));
    sg_image_desc imageDesc = {};
    imageDesc.width = 8;
    imageDesc.height = 8;
    imageDesc.pixel_format = SG_PIXELFORMAT_RGBA8;
    imageDesc.usage.color_attachment = true;
    const sg_image image = sg_make_image(&imageDesc);
    sg_view_desc viewDesc = {};
    viewDesc.color_attachment.image = image;
    const sg_view view = sg_make_view(&viewDesc);
    sg_pass pass = {};
    pass.attachments.colors[0] = view;

    for (int frame = 0; frame < 6; ++frame) {
        const uint32_t frameIndex = _sg.frame_index;
        triangle();
        sg_tc_mtl_flush(); // also valid before the first pass / with no buffer
        for (int read = 0; read < 4; ++read) {
            sg_begin_pass(&pass);
            sgl_draw();
            sg_end_pass();
            const int vertices = sgl_num_vertices();
#if defined(SOKOL_METAL)
            const auto slot = _sg.mtl.cur_frame_rotate_index;
            const int offset = _sg.mtl.cur_ub_offset;
            const auto base = _sg.mtl.cur_ub_base_ptr;
            id<MTLCommandBuffer> submitted = _sg.mtl.cmd_buffer;
            check("draw consumed uniform space", offset > 0);
#endif
            sg_tc_mtl_flush();
            check("flush preserves recorded 2D vertices", sgl_num_vertices() == vertices && vertices > 0);
            check("flush preserves frame index", _sg.frame_index == frameIndex);
            check("flush does not notify commit listeners", commits == frame);
#if defined(SOKOL_METAL)
            check("submitted Metal work completed", submitted.status == MTLCommandBufferStatusCompleted);
            check("next command buffer is created lazily", _sg.mtl.cmd_buffer == nil);
            check("uniform slot, offset and base are unchanged",
                _sg.mtl.cur_frame_rotate_index == slot && _sg.mtl.cur_ub_offset == offset
                && _sg.mtl.cur_ub_base_ptr == base);
#endif
            triangle();
        }
        // Frame end directly after a flush (no pending command buffer).
        sg_commit();
        check("one listener notification per frame", commits == frame + 1);
        check("only commit advances the frame index", _sg.frame_index == frameIndex + 1);
        check("only commit rewinds 2D vertices", sgl_num_vertices() == 0);
    }
    sg_remove_commit_listener(listener);
    sg_destroy_view(view);
    sg_destroy_image(image);
    sgl_shutdown();
    sg_shutdown();
#if defined(SOKOL_METAL)
    }
#endif
    return failures ? 1 : 0;
}
