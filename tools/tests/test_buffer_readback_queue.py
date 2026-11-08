"""Readback ordering, update boundaries, ownership and shutdown without a GPU."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
import test_compute_shader as compute_tests

ROOT = Path(__file__).resolve().parents[2]
# These API-only fixtures have no App runtime. The window-binding regression in
# computeBuffer exercises the real owner cancellation and scope restoration.
PREAMBLE = compute_tests.PREAMBLE + r'''
namespace trussc::internal {
std::shared_ptr<AppOwner> currentAppOwner() noexcept { return {}; }
void ownAppCancellation(std::function<void()>) { std::abort(); }
AppOwnerScope::AppOwnerScope(std::shared_ptr<AppOwner>) noexcept {}
AppOwnerScope::~AppOwnerScope() noexcept = default;
WorkerAppAdmission::WorkerAppAdmission(const std::shared_ptr<AppOwner>&, bool) noexcept {}
WorkerAppAdmission::~WorkerAppAdmission() noexcept = default;
AppCallbackScope::AppCallbackScope(std::shared_ptr<AppOwner>) : worker_(nullptr), owner_(nullptr) { std::abort(); }
AppCallbackScope::~AppCallbackScope() noexcept = default;
}
'''


class BufferReadbackQueueTests(unittest.TestCase):
    def test_pending_queue(self):
        source = r'''
#include "tc/gpu/internal/tcBufferReadback.h"
#include <cassert>
#include <thread>
using namespace trussc;
using namespace trussc::internal;
struct trussc::internal::AppOwner {};
struct Request : BufferReadbackRequest {
    bool& ready;
    int& destroyed;
    Request(bool& flag, int& count) : ready(flag), destroyed(count) {}
    ~Request() { ++destroyed; }
    bool poll() override { return ready; }
};
int main() {
    BufferReadbackQueue queue;
    bool firstReady = false, secondReady = true;
    int destroyed = 0, callbacks = 0, warnings = 0;
    const auto thread = std::this_thread::get_id();
    ReadbackResult retained;
    auto first = std::make_unique<Request>(firstReady, destroyed);
    first->result = ReadbackResult({0, 1, 128, 255});
    first->callback = [&](ReadbackResult result) {
        assert(std::this_thread::get_id() == thread && callbacks++ == 0);
        assert(destroyed == 1);
        retained = std::move(result);
        auto next = std::make_unique<BufferReadbackRequest>();
        next->callback = [&](ReadbackResult result) {
            assert(!result.ok() && !result.size() && callbacks++ == 2);
        };
        queue.enqueue(std::move(next));
    };
    auto second = std::make_unique<Request>(secondReady, destroyed);
    second->warn = [&] { ++warnings; };
    second->callback = [&](ReadbackResult result) {
        assert(!result.ok() && !result.size() && callbacks++ == 1);
    };
    queue.enqueue(std::move(first));
    queue.enqueue(std::move(second));
    assert(!callbacks);
    queue.poll();
    assert(!callbacks && !destroyed); // A ready second request cannot overtake.
    firstReady = true;
    queue.poll();
    assert(callbacks == 2 && destroyed == 2 && warnings == 1);
    assert(retained.ok() && retained.size() == 4 && retained.getData()[3] == 255);
    queue.poll();
    assert(callbacks == 3); // Callback-enqueued work waits for another poll.
    auto pending = std::make_unique<Request>(firstReady, destroyed);
    pending->callback = [&](ReadbackResult) { assert(false); };
    queue.enqueue(std::move(pending));
    queue.shutdown();
    assert(destroyed == 3 && !queue.accepting());
    queue.poll();
    auto rejected = std::make_unique<Request>(firstReady, destroyed);
    rejected->callback = [&](ReadbackResult) { assert(false); };
    queue.enqueue(std::move(rejected));
    assert(destroyed == 4);
    queue.start();
    auto restarted = std::make_unique<BufferReadbackRequest>();
    restarted->callback = [&](ReadbackResult) { ++callbacks; };
    queue.enqueue(std::move(restarted));
    queue.poll();
    assert(callbacks == 4);

    auto owner = std::make_shared<AppOwner>();
    auto otherOwner = std::make_shared<AppOwner>();
    auto capture = std::make_shared<int>(42);
    std::weak_ptr<int> released = capture;
    bool ready = false;
    auto canceled = std::make_unique<Request>(ready, destroyed);
    canceled->owner = owner;
    canceled->callback = [capture](ReadbackResult) { assert(false); };
    capture.reset();
    auto other = std::make_unique<BufferReadbackRequest>();
    other->owner = otherOwner;
    other->callback = [&](ReadbackResult) { ++callbacks; };
    queue.enqueue(std::move(canceled));
    queue.enqueue(std::move(other));
    queue.cancelAppCallbacks(owner);
    assert(released.expired() && destroyed == 4);
    queue.poll();
    assert(destroyed == 4 && callbacks == 4); // Cancellation does not bypass readiness/order.
    ready = true;
    queue.poll();
    assert(destroyed == 5 && callbacks == 5); // Other Apps still deliver; native request retires.
}
'''
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            code = Path(directory) / 'queue.cpp'
            binary = Path(directory) / 'queue'
            code.write_text(source, encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                            '-Werror', '-I', str(ROOT / 'core/include'), str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_reentrant_capture_release(self):
        # #1295 review: a capture destructor that retires an App (or enqueues)
        # must not touch the deque that shutdown/cancellation is walking.
        source = r'''
#include "tc/gpu/internal/tcBufferReadback.h"
#include <cassert>
using namespace trussc;
using namespace trussc::internal;
struct trussc::internal::AppOwner {};
struct Retire {
    BufferReadbackQueue& queue;
    std::shared_ptr<AppOwner> owner;
    bool enqueue;
    ~Retire() {
        queue.cancelAppCallbacks(owner);
        if (enqueue) queue.enqueue(std::make_unique<BufferReadbackRequest>());
    }
};
int main() {
    auto owner = std::make_shared<AppOwner>();
    {
        BufferReadbackQueue queue;
        queue.enqueue(std::make_unique<BufferReadbackRequest>());
        auto retire = std::make_shared<Retire>(Retire{queue, owner, true});
        auto second = std::make_unique<BufferReadbackRequest>();
        second->callback = [retire](ReadbackResult) {};
        retire.reset();
        queue.enqueue(std::move(second));
        auto third = std::make_unique<BufferReadbackRequest>();
        third->owner = owner;
        queue.enqueue(std::move(third));
        queue.shutdown(); // Destroying the second callback retires an App mid-shutdown.
        assert(!queue.accepting());
    }
    {
        BufferReadbackQueue queue;
        auto retire = std::make_shared<Retire>(Retire{queue, std::make_shared<AppOwner>(), true});
        auto owned = std::make_unique<BufferReadbackRequest>();
        owned->owner = owner;
        owned->callback = [retire](ReadbackResult) { assert(false); };
        retire.reset();
        queue.enqueue(std::move(owned));
        queue.enqueue(std::make_unique<BufferReadbackRequest>());
        queue.cancelAppCallbacks(owner); // The released capture re-enters cancellation and enqueues.
        int delivered = 0;
        queue.poll();
        queue.poll();
        (void)delivered;
    }
}
'''
        with tempfile.TemporaryDirectory(dir=ROOT / '.build') as directory:
            code = Path(directory) / 'reentrant.cpp'
            binary = Path(directory) / 'reentrant'
            code.write_text(source, encoding='utf-8')
            subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra',
                            '-Werror', '-I', str(ROOT / 'core/include'), str(code), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)

    def test_deferred_failure_and_shared_warning_gate(self):
        source = r'''
namespace trussc {
bool StorageBuffer::readPlatform(void*, size_t) { return false; }
}
namespace trussc::internal {
BufferReadbackQueue& bufferReadbacks() { static BufferReadbackQueue queue; return queue; }
}
int main() {
    using namespace trussc;
    int calls = 0;
    {
        StorageBuffer buffer;
        int sentinel = 42;
        if (buffer.read(&sentinel, 4)) return 1;
        buffer.readAsync([&](ReadbackResult result) {
            if (result.ok() || result.size()) std::abort();
            ++calls;
        });
        buffer.readAsync([&](ReadbackResult result) {
            if (result.ok() || result.size()) std::abort();
            ++calls;
        });
    }
    if (calls) return 2;
    const auto before = warnings.str();
    internal::bufferReadbacks().poll();
    if (calls != 2 || warnings.str() != before) return 3;
    StorageBuffer buffer;
    buffer.readAsync([&](ReadbackResult) { std::abort(); });
    internal::bufferReadbacks().shutdown();
    internal::bufferReadbacks().poll();
}
'''
        compute_tests.ComputeShaderTests.compile_and_run(self, source, True, main='', preamble=PREAMBLE)

    def test_pending_backend_deferred_failure(self):
        source = r'''
namespace trussc::internal {
BufferReadbackQueue& bufferReadbacks() { static BufferReadbackQueue queue; return queue; }
}
int main() {
    using namespace trussc;
    sg_desc setup{}; sg_setup(&setup); _sg.features.compute = true;
    int calls = 0;
    {
        StorageBuffer buffer;
        if (!buffer.allocate(16)) return 1;
        buffer.readAsync([&](ReadbackResult result) {
            if (result.ok() || result.size()) std::abort();
            ++calls;
        });
    }
    if (calls) return 2;
    internal::bufferReadbacks().poll();
    sg_shutdown();
    return calls == 1 ? 0 : 3;
}
'''
        for backend in ('__EMSCRIPTEN__', 'SOKOL_METAL'):
            with self.subTest(backend=backend):
                preamble = PREAMBLE.replace(
                    '#include "tc/gpu/tcStorageBuffer.h"',
                    f'#define {backend}\n#include "tc/gpu/tcStorageBuffer.h"')
                compute_tests.ComputeShaderTests.compile_and_run(self, source, True, main='', preamble=preamble)


if __name__ == '__main__':
    unittest.main()
