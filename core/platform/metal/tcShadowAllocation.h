#pragma once

// Host-only checks, included after SOKOL_IMPL in the macOS/iOS backend TU.
// This Sokol version exposes image/sampler queries but no Metal view query.
// Inspect the actual sampling view here rather than creating a second view.
namespace trussc::internal {
bool shadowNativeValid(sg_image image) {
    const auto info = sg_mtl_query_image_info(image);
    return info.tex[info.active_slot] != nullptr;
}

bool shadowNativeValid(sg_view handle) {
    const auto* view = _sg_lookup_view(handle.id);
    if (!view) return false;
    const auto* image = _sg_image_ref_ptr(&view->cmn.img.ref);
    if (!image || !shadowNativeValid(sg_image{image->slot.id})) return false;
    if (view->cmn.type == SG_VIEWTYPE_TEXTURE) {
        for (int slot = 0; slot < image->cmn.num_slots; ++slot) {
            const auto index = view->mtl.tex_view[slot];
            if (index == _SG_MTL_INVALID_SLOT_INDEX || _sg_mtl_id(index) == nil) return false;
        }
        return true;
    }
    // Attachment views borrow their image's native texture.
    return view->cmn.type == SG_VIEWTYPE_COLORATTACHMENT ||
        view->cmn.type == SG_VIEWTYPE_DEPTHSTENCILATTACHMENT;
}

bool shadowNativeValid(sg_sampler sampler) {
    return sg_mtl_query_sampler_info(sampler).smp != nullptr;
}
} // namespace trussc::internal
