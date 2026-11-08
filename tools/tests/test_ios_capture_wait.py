"""Execute the iOS capture wait with recorded Metal/dispatch seams, without an SDK."""
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def capture_wait():
    text = (ROOT / 'core/platform/ios/tcPlatform_ios.mm').read_text(encoding='utf-8')
    start = text.index('    struct Result :', text.index('bool captureWindow(Pixels&'))
    text = text[start:text.index('\n    MTLRegion region', start)]
    # Translate native syntax only; keep the production acquisition, ownership,
    # completion, deadline and failure control flow in the compiled fixture.
    seams = {
        'id<MTLTexture>': 'std::shared_ptr<Texture>',
        '(__bridge id<MTLCommandBuffer>)sg_tc_mtl_command_buffer()': 'sg_tc_mtl_command_buffer()',
        'id<MTLCommandBuffer>': 'std::shared_ptr<CommandBuffer>',
        'id<MTLBlitCommandEncoder>': 'bool',
        '[cmdBuf blitCommandEncoder]': 'encoderAvailable',
        '''[blitEnc copyFromTexture:srcTexture
                 sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1)
                   toTexture:stagingTexture
            destinationSlice:0 destinationLevel:0
           destinationOrigin:MTLOriginMake(0, 0, 0)]''': 'encodeCopy(srcTexture, stagingTexture)',
        '[blitEnc endEncoding]': 'endEncoding()',
        '[cmdBuf addCompletedHandler:^(std::shared_ptr<CommandBuffer> cb) {':
            'cmdBuf->completion = [result, sem](std::shared_ptr<CommandBuffer> cb) {',
        '    }];': '    };',
        'cb.status': 'cb->status',
        'nil': 'nullptr',
    }
    for old, new in seams.items():
        if old not in text:
            raise AssertionError(f'missing iOS extraction seam: {old}')
        text = text.replace(old, new)
    return text


PREAMBLE = r'''
#include <atomic>
#include <cassert>
#include <functional>
#include <memory>
#include <sstream>
namespace mcp::internal {
GATE
}
struct Texture {};
int statusReads, submits, parks, copies, reads;
constexpr int MTLCommandBufferStatusCompleted = 1;
struct Status {
    int value = 1;
    bool operator==(int expected) const { ++statusReads; return value == expected; }
};
struct CommandBuffer {
    Status status;
    std::function<void(std::shared_ptr<CommandBuffer>)> completion;
};
std::shared_ptr<CommandBuffer> pending;
bool slotAvailable = true, encoderAvailable = true, timeout = false;
namespace internal { void parkWindowPass() { ++parks; } }
struct CapturePhase {
    bool parked = false;
    void leaveParked() { parked = true; }
};
struct Semaphore { bool signaled = false; };
using dispatch_semaphore_t = std::shared_ptr<Semaphore>;
constexpr long long DISPATCH_TIME_NOW = 123, NSEC_PER_SEC = 1000000000;
long long deadline;
dispatch_semaphore_t dispatch_semaphore_create(int count) {
    assert(count == 0); return std::make_shared<Semaphore>();
}
void dispatch_semaphore_signal(const dispatch_semaphore_t& sem) { sem->signaled = true; }
long long dispatch_time(long long base, long long offset) {
    assert(base == DISPATCH_TIME_NOW); return base + offset;
}
void finishNativeJob() {
    auto buffer = std::move(pending);
    auto completion = std::move(buffer->completion);
    completion(buffer);
}
int dispatch_semaphore_wait(const dispatch_semaphore_t& sem, long long when) {
    deadline = when;
    if (timeout) return 1;
    finishNativeJob();
    assert(sem->signaled);
    return 0;
}
std::shared_ptr<CommandBuffer> sg_tc_mtl_command_buffer() {
    return slotAvailable ? pending : nullptr;
}
void sg_tc_submit_prefix() { ++submits; }
void encodeCopy(const std::shared_ptr<Texture>&, const std::shared_ptr<Texture>&) { ++copies; }
void endEncoding() {}
std::ostringstream errors;
std::ostream& logError(const char*) { return errors; }
bool capture(int& outPixels, CapturePhase& capturePhase,
             std::shared_ptr<Texture> srcTexture, std::shared_ptr<Texture> stagingTexture) {
WAIT
    // Only the successful caller reads/copies pixels, as in the remaining body.
    assert(result->staging == stagingTexture);
    ++reads;
    outPixels = 42;
    return true;
}
'''

MAIN = r'''
int main() {
    for (int scenario = 0; scenario < 5; ++scenario) {
        slotAvailable = scenario != 3;
        encoderAvailable = scenario != 4;
        timeout = scenario == 2;
        statusReads = submits = parks = copies = reads = 0;
        errors.str("");
        pending = std::make_shared<CommandBuffer>();
        pending->status.value = scenario == 1 ? 2 : MTLCommandBufferStatusCompleted;
        std::weak_ptr<CommandBuffer> bufferLifetime = pending;
        auto source = std::make_shared<Texture>(), staging = std::make_shared<Texture>();
        std::weak_ptr<Texture> sourceLifetime = source, stagingLifetime = staging;
        int pixels = 7;
        CapturePhase phase;
        bool ok = capture(pixels, phase, std::move(source), std::move(staging));
        assert(ok == (scenario == 0));
        assert(pixels == (ok ? 42 : 7));
        assert(reads == (ok ? 1 : 0));
        assert(phase.parked == !ok);
        assert(parks == 1);
        assert(submits == (scenario < 3 ? 1 : 0));
        assert(copies == (scenario < 3 ? 1 : 0));
        if (scenario < 3) assert(deadline == DISPATCH_TIME_NOW + 10 * NSEC_PER_SEC);
        if (timeout) {
            assert(errors.str().find("completion timed out") != std::string::npos);
            assert(!sourceLifetime.expired() && !stagingLifetime.expired());
            assert(!bufferLifetime.expired());
            assert(statusReads == 0);
            // The caller and its textures are gone; native completion must
            // release the pinned resources without reading or writing pixels.
            pixels = 99;
            finishNativeJob();
            assert(statusReads == 0 && reads == 0 && pixels == 99);
        }
        if (scenario == 1) assert(errors.str().find("readback failed") != std::string::npos);
        pending.reset();
        assert(bufferLifetime.expired());
        assert(sourceLifetime.expired() && stagingLifetime.expired());
    }
}
'''


class IOSCaptureWait(unittest.TestCase):
    def test_capture_wait_and_late_completion(self):
        header = (ROOT / 'core/include/tc/utils/tcMCP.h').read_text(encoding='utf-8')
        start = header.index('class CaptureCompletionGate {')
        gate = header[start:header.index('\n};', start) + 3]
        source = PREAMBLE.replace('GATE', gate).replace('WAIT', capture_wait()) + MAIN
        with tempfile.TemporaryDirectory() as temporary:
            cpp = Path(temporary) / 'capture.cpp'
            binary = Path(temporary) / 'capture'
            cpp.write_text(source, encoding='utf-8')
            subprocess.run([shutil.which('c++') or 'c++', '-std=c++20', '-Wall', '-Wextra',
                            '-Werror', str(cpp), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


if __name__ == '__main__':
    unittest.main()
