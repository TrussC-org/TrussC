#pragma once

// Included after SOKOL_IMPL: Sokol exposes no Metal texture-view query.
namespace trussc::internal {
bool shadowTextureViewCreated(sg_view handle) {
    const auto* view = _sg_lookup_view(handle.id);
    const auto index = view->mtl.tex_view[0];
    return index != _SG_MTL_INVALID_SLOT_INDEX && _sg_mtl_id(index) != nil;
}
} // namespace trussc::internal
