#include <TrussC.h>
#include "tc/sound/tcMicInputInternal.h"
#include <thread>

namespace {
struct AllocationInputApp : tc::App {
    int calls = 0;
    void audioIn(const tc::AudioInBuffer&) override { ++calls; }
};
}

void allocProbeArm();
void allocProbeDisarm();
size_t allocProbeLargest();

// Reuse this own-binary test's global allocation probe. The capture worker
// arms it around the production ring + event path, including its first block.
bool audioInDeliveryDoesNotAllocate() {
#ifdef __EMSCRIPTEN__
    return true;
#else
    using namespace tc;
    auto& engine = AudioEngine::getInstance();
    auto app = internal::makeConstructedApp<AllocationInputApp>();
    internal::setupNodeOnce(*app);
    internal::MicState state;
    state.ring.resize(MicInput::BUFFER_SIZE);
    float samples[256]{};
    int calls = 0;
    auto listener = engine.audioIn.listen([&](AudioInBuffer&) { ++calls; });
    auto mainListener = engine.audioIn.listen([&](AudioInBuffer&) { ++calls; }, Deliver::Main);
    size_t largest = 0;
    std::thread capture([&] {
        allocProbeArm();
        for (int block = 0; block < 64; ++block) state.capture(samples, 256);
        allocProbeDisarm();
        largest = allocProbeLargest();
    });
    capture.join();
    internal::detachAppAudio(*app);
    return largest == 0 && calls == 128 && app->calls == 64;
#endif
}
