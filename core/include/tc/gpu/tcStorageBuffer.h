#pragma once

#include "../utils/tcAnnotations.h"
#include "../utils/tcOnceGate.h"
#include "../app/internal/tcAppConstruction.h"
#include "internal/tcBufferReadback.h"
#include <utility>
#include <climits>
#include <new>
#include <stdexcept>

namespace trussc {

// Persistent, byte-sized GPU storage. All operations belong on the main thread.
class TC_LUA_SKIP StorageBuffer {
public:
    StorageBuffer() = default;
    ~StorageBuffer() { clear(); }
    StorageBuffer(const StorageBuffer&) = delete;
    StorageBuffer& operator=(const StorageBuffer&) = delete;
    StorageBuffer(StorageBuffer&& other) noexcept : warnings_(other.warnings_) { moveFrom(std::move(other)); }
    StorageBuffer& operator=(StorageBuffer&& other) noexcept {
        if (this != &other) { clear(); moveFrom(std::move(other)); }
        return *this;
    }

    bool allocate(size_t size, const void* data = nullptr) {
        clear();
        if (!available()) return false;
        // Bound (representation): sokol requires 4-byte storage sizes representable by INT_MAX.
        if (!size || size % 4 || size > INT_MAX) { warn("Size must be positive, a multiple of 4 bytes and representable by sokol (INT_MAX)"); return false; }
        sg_buffer_desc desc = {};
        desc.size = size;
        desc.usage.storage_buffer = true;
        desc.usage.copy_dst = true;
        desc.usage.copy_src = true;
        if (data) desc.data = {data, size};
        buffer_ = sg_make_buffer(&desc);
        if (sg_query_buffer_state(buffer_) == SG_RESOURCESTATE_VALID) {
            sg_view_desc view = {};
            view.storage_buffer.buffer = buffer_;
            view_ = sg_make_view(&view);
        }
        if (!isAllocated()) { warn("Failed to create storage buffer or view"); clear(); return false; }
        size_ = size;
        return true;
    }

    // One upload per device frame (sg_commit), starting at byte zero. The rest
    // of the buffer is preserved. Sokol's transient staging source cannot be
    // rewritten after its first copy in that frame.
    bool update(const void* data, size_t size) {
        if (!available()) return false;
        // Bound (copy range): the nonempty upload must be 4-byte aligned and fit the allocation.
        if (!isAllocated() || !data || !size || size > size_ || size % 4) {
            warn("Update requires allocated storage and a nonempty, 4-byte aligned range within its size");
            return false;
        }
        const auto frame = sg_query_stats().prev_frame.frame_index;
        // Bound (sokol copy source): the transient upload buffer can be written only once per frame.
        if (updated_ && uploadFrame_ == frame) { warn("CPU update is allowed once per device frame"); return false; }
        if (!staging_.id) {
            sg_buffer_desc desc = {};
            desc.size = size_;
            // Use a real GPU upload buffer: WebGPU's CPU-only staging path
            // writes the destination queue directly, before pending draws.
            // A GPU-to-GPU copy instead preserves command-encoder ordering.
            desc.usage.vertex_buffer = true;
            desc.usage.write_transient = true;
            desc.usage.copy_src = true;
            staging_ = sg_make_buffer(&desc);
        }
        if (sg_query_buffer_state(staging_) != SG_RESOURCESTATE_VALID) { warn("Failed to create upload staging buffer"); return false; }
        internal::suspendForImageUpload();
        sg_write_buffer_desc write = {};
        write.src.data = {data, size};
        write.dst.buffer = staging_;
        sg_write_buffer_transient(&write);
        sg_copy_buffer_to_buffer_desc copy = {};
        copy.src.buffer = staging_;
        copy.dst.buffer = buffer_;
        copy.size = size;
        sg_copy_buffer_to_buffer(&copy);
        updated_ = true;
        uploadFrame_ = frame;
        return true;
    }

#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    // Reads a prefix synchronously. Zero bytes succeeds without a GPU or destination.
    // Failure leaves destination untouched and warns (rate-limited: often called per frame).
    TC_PLATFORMS("macos,windows,linux,ios,android") bool read(void* destination, size_t bytes) {
        if (!bytes) return true;
        // Bound (copy range): the requested prefix must fit the allocation.
        if (!isAllocated() || !destination || bytes > size_) {
            warn("read requires allocated storage, a destination and bytes <= getSize()");
            return false;
        }
        try {
            internal::suspendForImageUpload();
            if (readPlatform(destination, bytes)) return true;
        } catch (const std::bad_alloc&) {
            // Keep allocation failures inside the API, including native frame callbacks.
        } catch (const std::length_error&) {
        }
        warn("Failed to read storage buffer");
        return false;
    }
#endif

    // Whole-buffer snapshot, delivered in request order in a later main update.
    // App-owned callbacks restore its scope and are dropped when that App ends.
    // Shutdown drops pending callbacks. Neither request nor result borrows this object.
    void readAsync(std::function<void(ReadbackResult)> callback) {
        auto& queue = internal::bufferReadbacks();
        if (!queue.accepting()) return;
        std::unique_ptr<internal::BufferReadbackRequest> request;
        if (isAllocated()) {
            try {
                internal::suspendForImageUpload();
                request = readAsyncPlatform();
            } catch (const std::bad_alloc&) {
            } catch (const std::length_error&) {
            }
        }
        if (!request) request = std::make_unique<internal::BufferReadbackRequest>();
        auto owner = internal::currentAppOwner();
        request->owner = owner;
        if (owner && (owner->ended || owner->retiring)) callback = {};
        if (owner && callback) {
            request->callback = [weak = request->owner, callback = std::move(callback)](ReadbackResult result) {
                auto owner = weak.lock();
                if (!owner || !owner->token->load() || owner->callbacksClosing) return;
                internal::AppCallbackScope scope(owner);
                if (scope.admitted() && owner->token->load() && !owner->callbacksClosing)
                    callback(std::move(result));
            };
        } else {
            request->callback = std::move(callback);
        }
        request->warn = [gate = warnings_] {
            if (gate->isFirstTime()) logWarning("StorageBuffer") << "Failed to read storage buffer asynchronously";
        };
        queue.enqueue(std::move(request));
    }

    void clear() noexcept {
        internal::deferGpuDestroy(view_);
        internal::deferGpuDestroy(buffer_);
        internal::deferGpuDestroy(staging_);
        view_ = {}; buffer_ = {}; staging_ = {};
        size_ = 0; updated_ = false;
    }
    bool isAllocated() const { return sg_isvalid() && buffer_.id && view_.id &&
        sg_query_buffer_state(buffer_) == SG_RESOURCESTATE_VALID &&
        sg_query_view_state(view_) == SG_RESOURCESTATE_VALID; }
    size_t getSize() const { return size_; }
    sg_buffer getBuffer() const { return buffer_; }
    sg_view getView() const { return view_; }

private:
#if (defined(SOKOL_GLCORE) || defined(SOKOL_GLES3)) && !defined(__EMSCRIPTEN__)
    std::unique_ptr<internal::BufferReadbackRequest> readAsyncPlatform();
#else
    // Subsequent backend slices replace this deferred failure, never fake success.
    std::unique_ptr<internal::BufferReadbackRequest> readAsyncPlatform() { return {}; }
#endif
#if !defined(__EMSCRIPTEN__) && !defined(SOKOL_WGPU)
    bool readPlatform(void* destination, size_t bytes);
#endif
    sg_buffer buffer_ = {}, staging_ = {};
    sg_view view_ = {};
    size_t size_ = 0;
    uint32_t uploadFrame_ = 0;
    bool updated_ = false;
    // A request can warn after the StorageBuffer has been destroyed.
    std::shared_ptr<OnceGate> warnings_ = std::make_shared<OnceGate>(5.0);
    void warn(const char* message) {
        if (warnings_->isFirstTime()) logWarning("StorageBuffer") << message;
    }
    bool available() {
        if (!sg_isvalid()) { warn("Storage buffers are unavailable without an initialized GPU (including headless apps)"); return false; }
        if (!sg_query_features().compute) { warn("Storage buffers are unsupported by this backend or device"); return false; }
        return true;
    }
    void moveFrom(StorageBuffer&& other) {
        warnings_ = other.warnings_;
        buffer_ = std::exchange(other.buffer_, {});
        staging_ = std::exchange(other.staging_, {});
        view_ = std::exchange(other.view_, {});
        size_ = std::exchange(other.size_, 0);
        uploadFrame_ = other.uploadFrame_;
        updated_ = std::exchange(other.updated_, false);
    }
};

namespace internal {
// Only storage declarations are checked here; existing image binding behavior
// stays in Shader/ComputeShader. Access is a shader property, not a buffer flag.
inline const char* storageBufferBindingsError(const sg_shader_desc& desc, const sg_bindings& bindings, bool compute) {
    for (int slot = 0; slot < SG_MAX_VIEW_BINDSLOTS; ++slot) {
        const auto& required = desc.views[slot].storage_buffer;
        if (required.stage == SG_SHADERSTAGE_NONE) continue;
        if ((compute && required.stage != SG_SHADERSTAGE_COMPUTE) ||
            (!compute && (required.stage == SG_SHADERSTAGE_COMPUTE || !required.readonly)))
            return "Storage buffer shader stage or read-only access does not match";
        const auto view = bindings.views[slot];
        if (sg_query_view_state(view) != SG_RESOURCESTATE_VALID || sg_query_view_type(view) != SG_VIEWTYPE_STORAGEBUFFER)
            return "Missing or wrong storage buffer view type";
        const auto buffer = sg_query_view_buffer(view);
        if (sg_query_buffer_state(buffer) != SG_RESOURCESTATE_VALID || !sg_query_buffer_usage(buffer).storage_buffer)
            return "Storage buffer binding has a retired or wrong buffer type";
        if (!required.readonly && sg_query_buffer_usage(buffer).write_transient)
            return "Read/write storage cannot use a transient CPU buffer";
        for (int other = 0; other < slot; ++other) {
            const auto& previous = desc.views[other].storage_buffer;
            if (previous.stage != SG_SHADERSTAGE_NONE && (!previous.readonly || !required.readonly) &&
                sg_query_view_state(bindings.views[other]) == SG_RESOURCESTATE_VALID &&
                sg_query_view_buffer(bindings.views[other]).id == buffer.id)
                return "Writable storage bindings must use separate buffers";
        }
    }
    return nullptr;
}
} // namespace internal
} // namespace trussc
