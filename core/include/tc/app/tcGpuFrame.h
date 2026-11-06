#ifndef TC_GPU_FRAME_H
#define TC_GPU_FRAME_H

// Internal frame-end predicate shared by the loop and dummy-backend tests.
// Keep this header usable from C and independent of the window/swapchain.
// This check relies on sokol frame stats (enabled by default in sg_setup and
// never disabled by TrussC), so an app calling sg_disable_stats() itself turns
// the check off.
#ifndef SOKOL_GFX_INCLUDED
#include "../../sokol/sokol_gfx.h"
#endif

static inline bool tc_internal_gpu_frame_has_work(void) {
    const sg_frame_stats frame = sg_query_stats().cur_frame;
    return frame.num_passes > 0 || frame.num_write_buffer_transient > 0
        || frame.num_write_image_transient > 0 || frame.num_write_buffer_unsealed > 0
        || frame.num_write_image_unsealed > 0 || frame.num_copy_buffer_to_buffer > 0
        || frame.num_copy_buffer_to_image > 0 || frame.num_copy_image_to_image > 0
        || frame.num_seal_buffer > 0 || frame.num_seal_image > 0;
}

#endif
