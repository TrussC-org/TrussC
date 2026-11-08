#pragma once
// Included by the macOS and iOS platform translation units (ARC).
#include "TrussC.h"
#import <Metal/Metal.h>
#include <TargetConditionals.h>
#include <atomic>

namespace trussc {
namespace internal {
namespace {
class MetalBufferReadback final : public BufferReadbackRequest {
public:
    struct State {
        // Sokol uses commandBufferWithUnretainedReferences by default. Keep
        // both buffers alive through completion, even if shutdown drops us.
        id<MTLBuffer> source = nil;
        id<MTLBuffer> staging = nil;
        std::atomic<bool> ready{false};
    };
    MetalBufferReadback(id<MTLCommandBuffer> command, std::shared_ptr<State> state, size_t size)
        : command_(command), state_(std::move(state)), bytes_(size) {}
    bool poll() override {
        if (!state_->ready.load(std::memory_order_acquire)) return false;
        if (command_.status == MTLCommandBufferStatusCompleted && state_->staging.contents) {
            std::memcpy(bytes_.data(), state_->staging.contents, bytes_.size());
            result = ReadbackResult(std::move(bytes_));
        } // Command-buffer errors leave the default !ok() result.
        state_->staging = nil;
        state_->source = nil;
        return true;
    }
private:
    // The handler owns only State, never the request, command buffer or App.
    id<MTLCommandBuffer> command_;
    std::shared_ptr<State> state_;
    std::vector<unsigned char> bytes_;
};
} // namespace
} // namespace internal

std::unique_ptr<internal::BufferReadbackRequest> StorageBuffer::readAsyncPlatform() {
    const auto info = sg_mtl_query_buffer_info(buffer_);
    id<MTLBuffer> source = (__bridge id<MTLBuffer>)info.buf[info.active_slot];
    if (!source) return {};
    // Acquire through the bounded seam first, so the pass below reuses it.
    id<MTLCommandBuffer> command = (__bridge id<MTLCommandBuffer>)sg_tc_mtl_command_buffer();
    if (!command) return {};
    // Register the native blit as frame work for deferred no-draw submission.
    sg_pass pass = {};
    pass.compute = true;
    sg_begin_pass(&pass);
    sg_end_pass();
    auto state = std::make_shared<internal::MetalBufferReadback::State>();
    state->source = source;
    state->staging = [source.device newBufferWithLength:size_ options:MTLResourceStorageModeShared];
    if (!state->staging) return {};
    auto request = std::make_unique<internal::MetalBufferReadback>(command, state, size_);
    id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
    if (!blit) return {};
    // Read the GPU copy of a managed source into shared staging. Only direct
    // CPU reads of managed storage need synchronizeResource.
    // https://developer.apple.com/documentation/metal/synchronizing-a-managed-resource-in-macos
    [blit copyFromBuffer:source sourceOffset:0 toBuffer:state->staging destinationOffset:0 size:size_];
    [blit endEncoding];
    [command addCompletedHandler:^(id<MTLCommandBuffer>) {
        state->ready.store(true, std::memory_order_release);
    }];
    // The ordinary frame commit submits this copy, in order with compute work.
    return request;
}

bool StorageBuffer::readPlatform(void* destination, size_t bytes) {
    const auto info = sg_mtl_query_buffer_info(buffer_);
    id<MTLBuffer> source = (__bridge id<MTLBuffer>)info.buf[info.active_slot];
    if (!source) return false;
    id<MTLCommandBuffer> command = (__bridge id<MTLCommandBuffer>)sg_tc_mtl_command_buffer();
    if (!command) return false;
    // StorageBuffer owns its allocation: sokol uses shared/managed memory.
    // Managed storage needs an explicit GPU-to-CPU synchronization on macOS.
#if TARGET_OS_OSX
    if (source.storageMode == MTLStorageModeManaged) {
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!blit) return false;
        [blit synchronizeResource:source];
        [blit endEncoding];
    }
#endif
    sg_tc_mtl_flush();
    if (command.status != MTLCommandBufferStatusCompleted || !source.contents) return false;
    std::memcpy(destination, source.contents, bytes);
    return true;
}
} // namespace trussc
