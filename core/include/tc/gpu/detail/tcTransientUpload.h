#pragma once

namespace trussc::internal {
// Owned by the upload target; moved/reset with its GPU resources.
struct TransientUploadState {
    size_t skipped = 0;
#if defined(SOKOL_VULKAN)
    bool write(const sg_write_buffer_desc& desc, const char* target, int width = 0, int height = 0);
#else
    bool write(const sg_write_buffer_desc& desc, const char*, int = 0, int = 0) {
        sg_write_buffer_transient(&desc);
        return true;
    }
#endif
};
} // namespace trussc::internal
