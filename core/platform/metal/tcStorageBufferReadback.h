#pragma once
// Included by the macOS and iOS platform translation units (ARC).
#include "TrussC.h"
#import <Metal/Metal.h>
#include <TargetConditionals.h>

namespace trussc {
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
