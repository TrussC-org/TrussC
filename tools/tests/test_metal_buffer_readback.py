"""Exercise Metal request C++ with native calls replaced by deterministic seams.

This does not compile Objective-C++ or validate Apple's API/ARC implementation.
The block seam captures the same independent shared State as the Metal block.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]

SOURCE = r'''
#define SOKOL_IMPL
#define SOKOL_DUMMY_BACKEND
#define SOKOL_TRACE_HOOKS
#include "sokol/sokol_gfx.h"
#include "tc/app/tcGpuFrame.h"
#include "tc/gpu/internal/tcBufferReadback.h"
#include <atomic>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
using namespace trussc;
int buffers = 0, commands = 0, copies = 0, cpuReads = 0;
bool failStaging = false, failBlit = false, nullContents = false;
constexpr int MTLResourceStorageModeShared = 1;
enum { MTLCommandBufferStatusNotEnqueued, MTLCommandBufferStatusEnqueued,
       MTLCommandBufferStatusCommitted, MTLCommandBufferStatusScheduled,
       MTLCommandBufferStatusCompleted, MTLCommandBufferStatusError };
struct MockBuffer {
    std::vector<unsigned char> bytes;
    explicit MockBuffer(size_t size) : bytes(size) { ++buffers; }
    ~MockBuffer() { --buffers; }
    std::shared_ptr<MockBuffer> newSharedBuffer(size_t size, int options) {
        assert(size == bytes.size() && options == MTLResourceStorageModeShared);
        return failStaging ? nullptr : std::make_shared<MockBuffer>(size);
    }
    const void* contents() { ++cpuReads; return nullContents ? nullptr : bytes.data(); }
};
struct MockBlit {
    bool ended = false;
    std::vector<std::function<void()>>& work;
    explicit MockBlit(std::vector<std::function<void()>>& workItems) : work(workItems) {}
    void copyFromBuffer(std::shared_ptr<MockBuffer> source, size_t sourceOffset,
                        std::shared_ptr<MockBuffer> staging, size_t destinationOffset, size_t size) {
        assert(!ended && sourceOffset == 0 && destinationOffset == 0);
        assert(size == source->bytes.size() && size == staging->bytes.size());
        // Model unretained Metal command-buffer resource references.
        work.push_back([input = std::weak_ptr<MockBuffer>(source),
                        output = std::weak_ptr<MockBuffer>(staging)] {
            auto sourceBuffer = input.lock(), destinationBuffer = output.lock();
            assert(sourceBuffer && destinationBuffer);
            destinationBuffer->bytes = sourceBuffer->bytes; ++copies;
        });
    }
    void endEncoding() { assert(!ended); ended = true; }
};
struct MockCommand {
    int status = MTLCommandBufferStatusEnqueued;
    std::vector<std::function<void()>> work;
    std::vector<std::function<void(std::shared_ptr<MockCommand>)>> handlers;
    MockCommand() { ++commands; }
    ~MockCommand() { --commands; }
    std::shared_ptr<MockBlit> blitCommandEncoder() {
        return failBlit ? nullptr : std::make_shared<MockBlit>(work);
    }
    void addCompletedHandler(std::function<void(std::shared_ptr<MockCommand>)> handler) {
        handlers.push_back(std::move(handler));
    }
    void finish(int finalStatus) {
        if (finalStatus == MTLCommandBufferStatusCompleted) for (auto& item : work) item();
        work.clear(); status = finalStatus;
        auto completedHandlers = std::move(handlers);
        for (auto& handler : completedHandlers) handler(nullptr);
    }
};
std::shared_ptr<MockBuffer> slots[3];
std::shared_ptr<MockCommand> current;
struct BufferInfo { std::shared_ptr<MockBuffer> buf[3]; int active_slot = 1; };
BufferInfo mock_query_buffer_info(int) { return {{slots[0], slots[1], slots[2]}}; }
std::shared_ptr<MockCommand> mock_command_buffer() { return current; }
namespace trussc {
class StorageBuffer {
public:
    int buffer_ = 0;
    size_t size_ = 4;
    std::unique_ptr<internal::BufferReadbackRequest> readAsyncPlatform();
};
}
// PRODUCTION_REQUEST
struct trussc::internal::AppOwner {};
void reset() {
    for (auto& slot : slots) slot.reset();
    current.reset();
    assert(buffers == 0 && commands == 0);
    copies = cpuReads = 0;
    slots[0] = std::make_shared<MockBuffer>(4);
    slots[0]->bytes = {9, 9, 9, 9}; // Wrong slot must never be read.
    slots[1] = std::make_shared<MockBuffer>(4);
    slots[1]->bytes = {1, 2, 3, 4};
    current = std::make_shared<MockCommand>();
}
int main() {
    sg_desc desc = {};
    sg_setup(&desc);
    using namespace trussc::internal;
    BufferReadbackQueue queue;
    StorageBuffer buffer;
    ReadbackResult retained;
    int callbacks = 0;
    reset();
    auto request = buffer.readAsyncPlatform();
    assert(request && current->work.size() == 1 && current->handlers.size() == 1);
    assert(!copies && !cpuReads); // No submit, wait or CPU read at request time.
    auto source = std::weak_ptr<MockBuffer>(slots[1]);
    current->work.push_back([source] { source.lock()->bytes = {5, 6, 7, 8}; });
    slots[1].reset(); // Request/handler keep native source alive, not StorageBuffer.
    request->callback = [&](ReadbackResult result) {
        assert(source.expired() && buffers == 1); // Staging released before user callback.
        retained = std::move(result); ++callbacks;
    };
    queue.enqueue(std::move(request));
    for (int status : {MTLCommandBufferStatusNotEnqueued, MTLCommandBufferStatusEnqueued,
                       MTLCommandBufferStatusCommitted, MTLCommandBufferStatusScheduled}) {
        current->status = status; queue.poll();
        assert(!callbacks && !cpuReads && !copies);
    }
    std::thread completion([] { current->finish(MTLCommandBufferStatusCompleted); });
    completion.join();
    assert(callbacks == 0); // Native completion never invokes user code.
    queue.poll();
    assert(callbacks == 1 && retained.ok() && retained.size() == 4);
    assert(std::memcmp(retained.getData(), "\1\2\3\4", 4) == 0 && copies == 1);

    // Command errors and absent CPU storage return failure, without stale bytes.
    for (bool missingContents : {false, true}) {
        reset(); nullContents = missingContents;
        auto failed = buffer.readAsyncPlatform();
        failed->callback = [&](ReadbackResult result) { assert(!result.ok() && !result.size()); ++callbacks; };
        queue.enqueue(std::move(failed));
        current->finish(missingContents ? MTLCommandBufferStatusCompleted : MTLCommandBufferStatusError);
        queue.poll(); assert(buffers == 2);
        nullContents = false;
    }
    assert(callbacks == 3);
    for (bool encoderFailure : {false, true}) {
        reset(); failBlit = encoderFailure; failStaging = !encoderFailure;
        assert(!buffer.readAsyncPlatform());
        assert(buffers == 2 && current->work.empty() && current->handlers.empty());
        failBlit = failStaging = false;
    }
    reset(); current.reset(); assert(!buffer.readAsyncPlatform());
    reset(); slots[1].reset(); assert(!buffer.readAsyncPlatform());

    // App end releases callback captures; GPU resources survive until completion.
    reset();
    auto owner = std::make_shared<AppOwner>();
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> weakCapture = capture;
    auto canceled = buffer.readAsyncPlatform();
    canceled->owner = owner;
    canceled->callback = [capture](ReadbackResult) { assert(false); };
    capture.reset(); queue.enqueue(std::move(canceled));
    queue.cancelAppCallbacks(owner);
    assert(weakCapture.expired() && buffers == 3);
    queue.poll(); assert(buffers == 3 && !cpuReads);
    current->finish(MTLCommandBufferStatusCompleted); queue.poll();
    assert(buffers == 2 && callbacks == 3);

    // Drop directly or shut down before submission, then finish the native work.
    for (bool shutdown : {false, true}) {
        reset(); queue.start();
        auto pending = buffer.readAsyncPlatform();
        pending->callback = [](ReadbackResult) { assert(false); };
        slots[1].reset();
        if (shutdown) { queue.enqueue(std::move(pending)); queue.shutdown(); }
        else pending.reset();
        assert(buffers == 3 && commands == 1 && !cpuReads);
        current->finish(MTLCommandBufferStatusCompleted);
        assert(buffers == 1 && !cpuReads && copies == 1);
        current.reset(); assert(commands == 0); // No command -> state -> command cycle.
        queue.poll(); assert(callbacks == 3);
    }
    // An abandoned, never-submitted command also releases the captured buffers.
    reset(); auto abandoned = buffer.readAsyncPlatform();
    abandoned.reset(); slots[1].reset(); current.reset();
    assert(buffers == 1 && commands == 0);
    assert(retained.ok() && retained.getData()[0] == 1);
    slots[0].reset(); assert(buffers == 0);
    sg_shutdown();
}
'''

NO_DRAW_MAIN = r'''
int main() {
    sg_desc desc = {};
    sg_setup(&desc);
    int commits = 0;
    sg_trace_hooks hooks = {};
    hooks.user_data = &commits;
    hooks.commit = [](void* data) {
        ++*static_cast<int*>(data);
        // Mock GPU execution is driven only by sokol submission.
        current->finish(MTLCommandBufferStatusCompleted);
    };
    sg_install_trace_hooks(&hooks);
    reset();
    assert(!tc_internal_gpu_frame_has_work());
    StorageBuffer buffer;
    trussc::internal::BufferReadbackQueue queue;
    int callbacks = 0;
    auto request = buffer.readAsyncPlatform();
    assert(request && current->work.size() == 1);
    assert(commits == 0 && copies == 0 && cpuReads == 0);
    request->callback = [&](ReadbackResult result) {
        assert(result.ok() && result.size() == 4);
        assert(std::memcmp(result.getData(), "\1\2\3\4", 4) == 0);
        ++callbacks;
    };
    queue.enqueue(std::move(request));
    for (int tick = 0; tick < 120; ++tick) {
        queue.poll();
        // Production no-draw/host-turn submission decision; no draw or upload.
        if (tc_internal_gpu_frame_has_work()) sg_commit();
    }
    assert(commits == 1 && copies == 1 && callbacks == 1);
    assert(current->work.empty() && !tc_internal_gpu_frame_has_work());
    std::printf("120 no-draw ticks: commits=%d, copies=%d, callbacks=%d\n", commits, copies, callbacks);
    queue.shutdown();
    for (auto& slot : slots) slot.reset();
    current.reset();
    assert(buffers == 0 && commands == 0);
    sg_shutdown();
}
'''


def request_with_seams():
    source = (ROOT / 'core/platform/metal/tcStorageBufferReadback.h').read_text(encoding='utf-8')
    source = source[source.index('namespace trussc {'):source.index('bool StorageBuffer::readPlatform(')]
    source += '} // namespace trussc\n'
    replacements = {
        'sg_mtl_query_buffer_info': 'mock_query_buffer_info',
        'sg_tc_mtl_command_buffer': 'mock_command_buffer',
        '(__bridge id<MTLBuffer>)': '',
        '(__bridge id<MTLCommandBuffer>)': '',
        'id<MTLBuffer>': 'std::shared_ptr<MockBuffer>',
        'id<MTLCommandBuffer>': 'std::shared_ptr<MockCommand>',
        'id<MTLBlitCommandEncoder>': 'std::shared_ptr<MockBlit>',
        ' = nil': ' = nullptr',
        'command_.status': 'command_->status',
        'state_->staging.contents': 'state_->staging->contents()',
        '[source.device newBufferWithLength:size_ options:MTLResourceStorageModeShared]':
            'source->newSharedBuffer(size_, MTLResourceStorageModeShared)',
        '[command blitCommandEncoder]': 'command->blitCommandEncoder()',
        '[blit copyFromBuffer:source sourceOffset:0 toBuffer:state->staging destinationOffset:0 size:size_]':
            'blit->copyFromBuffer(source, 0, state->staging, 0, size_)',
        '[blit endEncoding]': 'blit->endEncoding()',
        '[command addCompletedHandler:^(std::shared_ptr<MockCommand>) {':
            'command->addCompletedHandler([state](std::shared_ptr<MockCommand>) {',
        '    }];': '    });',
    }
    for native, seam in replacements.items():
        assert native in source, native
        source = source.replace(native, seam)
    return source


class MetalBufferReadbackTests(unittest.TestCase):
    def test_snapshot_completion_errors_and_lifetime(self):
        self.compile_and_run(SOURCE)

    def test_readback_only_no_draw_ticks_submit_and_complete(self):
        self.compile_and_run(SOURCE[:SOURCE.index('int main()')] + NO_DRAW_MAIN)

    def compile_and_run(self, source):
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            folder = Path(directory)
            code, binary = folder / 'readback.cpp', folder / 'readback'
            code.write_text(source.replace('// PRODUCTION_REQUEST', request_with_seams()), encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-g', '-pthread',
                            '-I', str(ROOT / 'core/include'), str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == '__main__':
    unittest.main()
