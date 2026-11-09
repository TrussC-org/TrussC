"""Exercise the production WebGPU readback with deterministic native callbacks."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

SOURCE = r'''
#include "sokol/sokol_gfx.h"
#include "tc/gpu/internal/tcBufferReadback.h"
#include <webgpu/webgpu.h>
#include <cassert>
#include <cstring>
#include <functional>
#include <vector>
struct WGPUBufferImpl {
    std::vector<unsigned char> bytes;
    bool released = false, destroyed = false;
    WGPUBufferMapCallbackInfo callback{};
};
struct WGPUDeviceImpl {} device;
struct WGPUCommandEncoderImpl {} encoder;
std::vector<std::unique_ptr<WGPUBufferImpl>> buffers;
std::vector<std::function<void()>> commands;
bool hasEncoder = false, inPass = false, nullRange = false;
uint32_t frame = 0;
int maps = 0, releases = 0, destroys = 0, unmaps = 0;
WGPUBuffer makeBuffer(size_t size) {
    buffers.push_back(std::make_unique<WGPUBufferImpl>());
    buffers.back()->bytes.resize(size);
    return buffers.back().get();
}
sg_stats sg_query_stats() { sg_stats s{}; s.prev_frame.frame_index = frame; return s; }
const void* sg_wgpu_device() { return &device; }
const void* sg_wgpu_command_encoder() { return hasEncoder ? &encoder : nullptr; }
sg_wgpu_buffer_info sg_wgpu_query_buffer_info(sg_buffer buffer) {
    sg_wgpu_buffer_info info{}; info.buf = buffers.at(buffer.id).get(); return info;
}
void sg_begin_pass(const sg_pass* pass) { assert(pass->compute && !inPass); inPass = true; hasEncoder = true; }
void sg_end_pass() { assert(inPass); inPass = false; }
WGPUBuffer wgpuDeviceCreateBuffer(WGPUDevice dev, const WGPUBufferDescriptor* desc) {
    assert(dev == &device && desc->usage == (WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead));
    return makeBuffer(desc->size);
}
void wgpuCommandEncoderCopyBufferToBuffer(WGPUCommandEncoder enc, WGPUBuffer source, uint64_t srcOffset,
                                         WGPUBuffer target, uint64_t dstOffset, uint64_t size) {
    assert(enc == &encoder && !inPass && !srcOffset && !dstOffset);
    commands.push_back([=] {
        assert(!target->destroyed); // Releasing before submission must not destroy.
        std::memcpy(target->bytes.data(), source->bytes.data(), size);
    });
}
void submit() {
    for (auto& command : commands) command();
    commands.clear(); hasEncoder = false; ++frame;
}
WGPUFuture wgpuBufferMapAsync(WGPUBuffer buffer, WGPUMapMode mode, size_t offset, size_t size,
                             WGPUBufferMapCallbackInfo callback) {
    assert(!buffer->released && !buffer->destroyed && mode == WGPUMapMode_Read && !offset);
    assert(size == buffer->bytes.size() && callback.mode == WGPUCallbackMode_AllowSpontaneous);
    ++maps; buffer->callback = callback; return {};
}
const void* wgpuBufferGetConstMappedRange(WGPUBuffer buffer, size_t offset, size_t size) {
    assert(!buffer->released && !offset && size == buffer->bytes.size());
    return nullRange ? nullptr : buffer->bytes.data();
}
void wgpuBufferUnmap(WGPUBuffer buffer) { assert(!buffer->released); ++unmaps; }
void wgpuBufferDestroy(WGPUBuffer buffer) { assert(!buffer->released); buffer->destroyed = true; ++destroys; }
void wgpuBufferRelease(WGPUBuffer buffer) { assert(!buffer->released); buffer->released = true; ++releases; }
void complete(WGPUBuffer buffer, WGPUMapAsyncStatus status = WGPUMapAsyncStatus_Success) {
    auto callback = std::exchange(buffer->callback, {});
    assert(callback.callback);
    callback.callback(status, {}, callback.userdata1, callback.userdata2);
}
namespace trussc {
class StorageBuffer {
public:
    sg_buffer buffer_{};
    size_t size_ = 4;
    std::unique_ptr<internal::BufferReadbackRequest> readAsyncPlatform();
};
}
#include "core/platform/web/tcStorageBufferReadback.h"
int main() {
    using namespace trussc;
    using namespace trussc::internal;
    BufferReadbackQueue queue;
    StorageBuffer buffer;
    auto source = makeBuffer(4);
    source->bytes = {1, 2, 3, 4};
    ReadbackResult retained;
    int callbacks = 0;
    auto first = buffer.readAsyncPlatform();
    auto staging = buffers.back().get();
    assert(hasEncoder); // No preceding pass: the production code acquired one.
    first->callback = [&](ReadbackResult result) { ++callbacks; retained = std::move(result); };
    queue.enqueue(std::move(first));
    queue.poll(); assert(maps == 0 && callbacks == 0); // Not submitted yet.
    submit();
    source->bytes = {9, 9, 9, 9}; // Snapshot remains the earlier copy.
    queue.poll(); assert(maps == 1 && callbacks == 0);
    complete(staging); assert(callbacks == 0 && staging->released && unmaps == 1);
    queue.poll();
    assert(callbacks == 1 && retained.ok() && retained.size() == 4 && retained.getData()[0] == 1);

    // Failure/device loss and an unavailable mapped range are deferred !ok().
    for (bool unavailable : {false, true}) {
        auto request = buffer.readAsyncPlatform();
        staging = buffers.back().get();
        request->callback = [&](ReadbackResult result) { assert(!result.ok() && !result.size()); ++callbacks; };
        queue.enqueue(std::move(request));
        submit(); queue.poll();
        nullRange = unavailable;
        complete(staging, unavailable ? WGPUMapAsyncStatus_Success : WGPUMapAsyncStatus_Error);
        nullRange = false;
        assert(callbacks == (unavailable ? 2 : 1));
        queue.poll();
    }
    assert(callbacks == 3);

    // App end drops captures immediately; staging retires after its map finishes.
    auto owner = std::make_shared<AppOwner>();
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> weakCapture = capture;
    auto canceled = buffer.readAsyncPlatform();
    staging = buffers.back().get();
    canceled->owner = owner;
    canceled->callback = [capture](ReadbackResult) { assert(false); };
    capture.reset(); queue.enqueue(std::move(canceled));
    queue.cancelAppCallbacks(owner);
    assert(weakCapture.expired() && !staging->released);
    submit(); queue.poll(); complete(staging); queue.poll();
    assert(staging->released && callbacks == 3);

    // Shutdown before submission releases the handle without destroying an
    // encoded copy, even if the ordinary frame submits after App teardown.
    auto dropped = buffer.readAsyncPlatform();
    staging = buffers.back().get();
    dropped->callback = [](ReadbackResult) { assert(false); };
    queue.enqueue(std::move(dropped)); queue.shutdown();
    assert(staging->released && !staging->destroyed);
    submit();

    // Shutdown while mapping cancels/releases now. A later callback has no
    // request, staging handle or user capture to touch (also checked by ASan).
    queue.start();
    auto pending = buffer.readAsyncPlatform();
    staging = buffers.back().get();
    pending->callback = [](ReadbackResult) { assert(false); };
    queue.enqueue(std::move(pending)); submit(); queue.poll();
    const int before = releases;
    queue.shutdown();
    assert(staging->released && staging->destroyed && destroys == 1 && releases == before + 1);
    complete(staging, WGPUMapAsyncStatus_Aborted);
    assert(releases == before + 1 && callbacks == 3);
}
'''


class WebGPUBufferReadbackTests(unittest.TestCase):
    def test_native_lifetime_and_submission(self):
        suffix = Path('ports/emdawnwebgpu/emdawnwebgpu_pkg/webgpu/include')
        candidates = [Path(os.environ.get('EM_CACHE', ROOT / '.build/emscripten-cache')) / suffix,
                      Path(os.environ.get('EMSDK', Path.home() / 'emsdk')) / 'upstream/emscripten/cache' / suffix]
        include = next((path for path in candidates if (path / 'webgpu/webgpu.h').is_file()), None)
        if include is None:
            self.skipTest('installed emdawn WebGPU headers unavailable; no downloads')
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            code = Path(directory) / 'readback.cpp'
            binary = Path(directory) / 'readback'
            code.write_text(SOURCE, encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g',
                            '-I', str(include), '-I', str(ROOT / 'core/include'), '-I', str(ROOT),
                            str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
