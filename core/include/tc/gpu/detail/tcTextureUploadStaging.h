#pragma once

#include <sokol_gfx.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace trussc::internal {

// Device-owned storage for the first few upload ordinals in each device frame.
class TextureUploadStaging {
public:
    static constexpr size_t slotsPerFrame = 4;
    static constexpr size_t frameSets = SG_NUM_INFLIGHT_FRAMES + 1;
    static constexpr uint64_t idleFrames = 120;

    struct Upload {
        sg_buffer buffer = {};
        bool temporary = false;
    };

    TextureUploadStaging() = default;
    TextureUploadStaging(const TextureUploadStaging&) = delete;
    TextureUploadStaging& operator=(const TextureUploadStaging&) = delete;
    ~TextureUploadStaging() {
        for (const auto& slot : slots_) {
            if (slot.buffer.id) sg_destroy_buffer(slot.buffer);
        }
    }

    Upload acquire(size_t bytes, bool gpuStaging, uint64_t deviceFrame) {
        if (frame_ != deviceFrame) {
            frame_ = deviceFrame;
            cursor_ = 0;
            for (auto& slot : slots_) {
                if (slot.buffer.id && deviceFrame - slot.lastUsed >= idleFrames) {
                    sg_destroy_buffer(slot.buffer);
                    slot = {};
                }
            }
        }
        const size_t ordinal = cursor_++;
        Slot temporary;
        // Metal writes CPU storage before its command-buffer in-flight wait.
        // Revisit a set only after SG_NUM_INFLIGHT_FRAMES + 1 device frames,
        // when its prior copy has completed, even before this frame's wait.
        // Staging forbids write_unsealed; sokol offers no sg_update_buffer,
        // so keep write_transient (two native backings) with these extra sets.
        auto& slot = ordinal < slotsPerFrame
            ? slots_[(deviceFrame % frameSets) * slotsPerFrame + ordinal]
            : temporary;
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
        slot.lastUsed = deviceFrame;
        return {slot.buffer, ordinal >= slotsPerFrame};
    }

    static void release(Upload upload) {
        // Burst uploads retain the original make/write/copy/destroy lifetime.
        if (upload.temporary) sg_destroy_buffer(upload.buffer);
    }

    // Queue/buffer writes consume these bytes before loadData returns. No
    // caller-owned pointer is retained, and resize preserves capacity only.
    std::vector<uint8_t> padded;

private:
    struct Slot {
        sg_buffer buffer = {};
        size_t capacity = 0;
        bool gpuStaging = false;
        uint64_t lastUsed = 0;
    };
    std::array<Slot, slotsPerFrame * frameSets> slots_ = {};
    size_t cursor_ = 0;
    uint64_t frame_ = UINT64_MAX;
};

} // namespace trussc::internal
