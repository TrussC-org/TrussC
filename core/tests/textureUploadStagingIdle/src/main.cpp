// #1557: idle device frames retire upload staging without another upload.
#include <TrussC.h>
#include <tc/gpu/detail/tcTextureUploadStaging.h>
#include "../../common/tcCoreTest.h"
#include "../../common/tcDisplayTest.h"
#include <array>
#include <cstdio>
#include <cstring>

using namespace tc;

namespace {
using Pool = internal::TextureUploadStaging;
int failures = 0;
bool completed = false;

void check(const char* label, bool ok) {
    std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class IdleStagingApp : public App {
public:
    void setup() override {
        sg_enable_stats();
        pixels_.allocate(16, 16, 4);
        std::memset(pixels_.getData(), 255, 16 * 16 * 4);
        for (auto& texture : textures_)
            texture.allocate(16, 16, 4, TextureUsage::Stream);
    }

    void draw() override {
        const auto frame = internal::deviceFrameCount();
        if (draws_++ == 0) {
            baseline_ = sg_query_stats().total.buffers.alive;
            firstUpload_ = frame;
        }
        if (frame - firstUpload_ < Pool::idleFrames + 2 * Pool::frameSets) {
            const auto before = sg_query_stats().total.buffers;
            for (auto& texture : textures_) texture.loadData(pixels_);
            const auto after = sg_query_stats().total.buffers;
            if (frame - firstUpload_ >= Pool::frameSets) {
                stable_ &= before.allocated == after.allocated &&
                    before.deallocated == after.deallocated &&
                    after.alive == baseline_ + Pool::slotsPerFrame * Pool::frameSets;
            }
            lastUpload_ = frame;
            return;
        }
        const auto buffers = sg_query_stats().total.buffers;
        if (frame - lastUpload_ < Pool::frameSets)
            safe_ &= buffers.alive == baseline_ + Pool::slotsPerFrame * Pool::frameSets;
        // The oldest rotating set expires first; even it must stay until idleFrames.
        if (frame - lastUpload_ == Pool::idleFrames - Pool::frameSets)
            check("staging retained until the idle threshold", buffers.alive == baseline_ + Pool::slotsPerFrame * Pool::frameSets);
        if (frame - lastUpload_ <= Pool::idleFrames + Pool::frameSets) return;
        std::printf("buffers: baseline=%u, streaming=%zu, idle=%u\n", baseline_,
            baseline_ + Pool::slotsPerFrame * Pool::frameSets, buffers.alive);
        check("continuous streaming reuses every warmed slot", stable_);
        check("recent in-flight staging remains alive", safe_);
        check("idle frames restore the pre-upload buffer count", buffers.alive == baseline_);
        completed = true;
        exitApp();
    }

private:
    std::array<Texture, Pool::slotsPerFrame> textures_;
    Pixels pixels_;
    uint32_t baseline_ = 0;
    uint64_t firstUpload_ = 0, lastUpload_ = 0;
    unsigned draws_ = 0;
    bool stable_ = true, safe_ = true;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], "--gpu-check") != 0) {
        std::printf("SKIP: textureUploadStagingIdle needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(32, 32);
    settings.setHighDpi(false);
    settings.setSwapInterval(0);
    return tcCoreTest::runDisplayTest<IdleStagingApp>(settings, failures, [] {
        check("streaming and idle phases completed", completed);
    });
}
