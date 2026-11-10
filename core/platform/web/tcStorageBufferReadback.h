#pragma once
#include "tc/gpu/internal/tcBufferReadback.h"
#include <webgpu/webgpu.h>
#include <cstring>

namespace trussc {
namespace internal {
namespace {
class WebGPUBufferReadback final : public BufferReadbackRequest {
    struct State {
        explicit State(size_t size) : bytes(size) {}
        ~State() {
            // Release an unsubmitted copy without invalidating its encoder.
            // Destroy cancels a pending map; its callback only holds a weak_ptr.
            if (staging) {
                if (mapping) wgpuBufferDestroy(staging);
                wgpuBufferRelease(staging);
            }
        }
        WGPUBuffer staging = nullptr;
        std::vector<unsigned char> bytes;
        ReadbackResult result;
        bool mapping = false, ready = false;
    };
public:
    explicit WebGPUBufferReadback(size_t size)
        : state_(std::make_shared<State>(size)), frame_(sg_query_stats().prev_frame.frame_index) {}
    WGPUBuffer& staging() { return state_->staging; }
    bool poll() override {
        if (!state_->mapping) {
            // The copy shares sokol's encoder with preceding compute/draw work.
            // Map only after the normal sg_commit has submitted that encoder.
            if (sg_query_stats().prev_frame.frame_index == frame_) return false;
            WGPUBufferMapCallbackInfo callback = {};
            callback.mode = WGPUCallbackMode_AllowSpontaneous;
            callback.userdata1 = new std::weak_ptr<State>(state_);
            callback.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* userdata, void*) {
                std::unique_ptr<std::weak_ptr<State>> weak(static_cast<std::weak_ptr<State>*>(userdata));
                auto state = weak->lock();
                if (!state) return; // Shutdown already released the request and staging.
                if (status == WGPUMapAsyncStatus_Success) {
                    const void* mapped = wgpuBufferGetConstMappedRange(state->staging, 0, state->bytes.size());
                    if (mapped) {
                        std::memcpy(state->bytes.data(), mapped, state->bytes.size());
                        state->result = ReadbackResult(std::move(state->bytes));
                    }
                    wgpuBufferUnmap(state->staging);
                }
                // Failure (including device loss) leaves an empty !ok() result.
                wgpuBufferRelease(state->staging);
                state->staging = nullptr;
                state->ready = true;
            };
            state_->mapping = true;
            wgpuBufferMapAsync(state_->staging, WGPUMapMode_Read, 0, state_->bytes.size(), callback);
        }
        if (!state_->ready) return false;
        result = std::move(state_->result);
        return true;
    }
private:
    // Browser callbacks run on the main thread. They never hold a request/App
    // pointer and cannot keep a canceled request's CPU or GPU storage alive.
    std::shared_ptr<State> state_;
    uint32_t frame_;
};
} // namespace
} // namespace internal

std::unique_ptr<internal::BufferReadbackRequest> StorageBuffer::readAsyncPlatform() {
    const auto source = (WGPUBuffer)sg_wgpu_query_buffer_info(buffer_).buf;
    if (!source) return {};
    auto request = std::make_unique<internal::WebGPUBufferReadback>(size_);
    WGPUBufferDescriptor desc = {};
    desc.size = size_;
    desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
    request->staging() = wgpuDeviceCreateBuffer((WGPUDevice)sg_wgpu_device(), &desc);
    if (!request->staging()) return {};
    if (!sg_wgpu_command_encoder()) {
        // A read of freshly allocated storage may be the frame's first command.
        // An empty pass acquires a sokol-owned encoder without a private seam.
        sg_pass pass = {};
        pass.compute = true;
        sg_begin_pass(&pass);
        sg_end_pass();
    }
    wgpuCommandEncoderCopyBufferToBuffer((WGPUCommandEncoder)sg_wgpu_command_encoder(),
        source, 0, request->staging(), 0, size_);
    return request;
}
} // namespace trussc
