#pragma once

#include <sokol_gfx.h>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace trussc::internal {

// Device-owned high-water storage. Each copy consumes one ordinal until the
// next sg_commit: sokol forbids rewriting a copy source in the same frame.
class TextureUploadStaging {
public:
    TextureUploadStaging() = default;
    TextureUploadStaging(const TextureUploadStaging&) = delete;
    TextureUploadStaging& operator=(const TextureUploadStaging&) = delete;
    ~TextureUploadStaging() {
        for (const auto& slot : slots_) sg_destroy_buffer(slot.buffer);
    }

    sg_buffer acquire(size_t bytes, bool gpuStaging, uint64_t deviceFrame) {
        if (frame_ != deviceFrame) {
            frame_ = deviceFrame;
            cursor_ = 0;
        }
        if (cursor_ == slots_.size()) slots_.emplace_back();
        auto& slot = slots_[cursor_++];
        const size_t capacity = (bytes + 3) & ~size_t(3);
        if (slot.capacity < capacity || slot.gpuStaging != gpuStaging) {
            sg_destroy_buffer(slot.buffer);
            slot = {};
        }
        if (!slot.buffer.id) {
            sg_buffer_desc desc = {};
            desc.size = capacity;
            desc.usage.staging_buffer = !gpuStaging;
            desc.usage.copy_src = true;
            desc.usage.write_transient = true;
            desc.label = "tc-texture-upload";
            slot.buffer = sg_make_buffer(&desc);
            if (sg_query_buffer_state(slot.buffer) != SG_RESOURCESTATE_VALID) {
                // Preserve the old failed-allocation path: skip the copy and
                // release even a failed handle, allowing a later upload to retry.
                sg_destroy_buffer(slot.buffer);
                slot = {};
                return {};
            }
            slot.capacity = capacity;
            slot.gpuStaging = gpuStaging;
        }
        return slot.buffer;
    }

    // Queue/buffer writes consume these bytes before loadData returns. No
    // caller-owned pointer is retained, and resize preserves capacity only.
    std::vector<uint8_t> padded;

private:
    struct Slot {
        sg_buffer buffer = {};
        size_t capacity = 0;
        bool gpuStaging = false;
    };
    std::vector<Slot> slots_;
    size_t cursor_ = 0;
    uint64_t frame_ = UINT64_MAX;
};

} // namespace trussc::internal
