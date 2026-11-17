#pragma once

#include <cstdint>
#ifndef SOKOL_GFX_INCLUDED
#include "../../sokol/sokol_gfx.h"
#endif

namespace trussc { namespace internal {

// Apply a draw's StencilDesc value (low byte of its packed key) after a
// stencil pipeline is applied; pipelines are built with a fixed reference.
inline void applyDrawStencilRef(uint32_t key) {
    if (key) sg_apply_stencil_ref(uint8_t(key));
}

}} // namespace trussc::internal
