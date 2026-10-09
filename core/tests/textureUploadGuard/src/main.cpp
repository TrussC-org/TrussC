// #686: run with --gpu-check under Xvfb on Linux.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <cstring>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
bool completed = false;

void check(const char* name, bool ok) {
    printf("%-64s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class UploadGuardApp : public App {
public:
    void setup() override {
        setFps(VSYNC);
        fbo_.allocate(4, 4);
        a_.allocate(4, 4, 4);
        b_.allocate(4, 4, 4);
        for (int y = 0; y < 4; ++y) {
            for (int x = 0; x < 4; ++x) {
                a_.setColor(x, y, Color(1, 0, 0, 1));
                b_.setColor(x, y, Color(0, 1, 0, 1));
            }
        }
    }

    void draw() override {
        if (completed) return;
        int warnings = 0;
        EventListener logs = getLogger().onLog.listen([&](LogEventArgs& e) {
            if (e.level == LogLevel::Warning &&
                e.message == "[Texture] loadData() called twice in same frame, skipped") ++warnings;
        });
        if (secondFrame_) {
            check("device frame advanced", sapp_frame_count() != firstFrame_);
            texture_.loadData(a_);
            check("same image accepts upload on next frame", warnings == 0);
            expectPixels("next frame reads A", texture_, a_);
            completed = true;
            exitApp();
            return;
        }
        firstFrame_ = sapp_frame_count();

        // Cover each 2D allocation overload, without advancing the device frame.
        for (int allocation = 0; allocation < 3; ++allocation) {
            warnings = 0;
            auto allocate = [&] {
                if (allocation == 0) texture_.allocate(4, 4, 4, TextureUsage::Stream);
                if (allocation == 1) texture_.allocate(4, 4, TextureFormat::RGBA8, TextureUsage::Stream);
                if (allocation == 2) texture_.allocate(a_, TextureUsage::Stream);
            };
            allocate();
            texture_.loadData(a_);
            const uint32_t oldImage = texture_.getImage().id;
            expectPixels("initial upload reads A", texture_, a_);
            texture_.clear();
            allocate();
            check("reallocation creates a different image", texture_.getImage().id != oldImage);
            texture_.loadData(b_.getData(), 4, 4, 4);
            check("first upload to new image has no warning", warnings == 0);
            expectPixels("new image reads B in the same frame", texture_, b_);
            texture_.loadData(a_);
            check("duplicate upload emits the existing warning", warnings == 1);
            expectPixels("duplicate upload keeps B", texture_, b_);
        }

        warnings = 0;
        Texture moved(std::move(texture_));
        moved.loadData(a_);
        check("move construction preserves upload guard", warnings == 1);
        expectPixels("move construction keeps B", moved, b_);
        texture_ = std::move(moved);
        texture_.loadData(a_);
        check("move assignment preserves upload guard", warnings == 2);
        expectPixels("move assignment keeps B", texture_, b_);
        moved.allocate(4, 4, 4, TextureUsage::Stream);
        moved.loadData(a_);
        check("moved-from texture accepts a new image", warnings == 2);
        expectPixels("moved-from new image reads A", moved, a_);

        warnings = 0;
        Texture mipmapped;
        mipmapped.allocate(a_, TextureUsage::Dynamic, true);
        mipmapped.loadData(a_);
        const uint32_t firstMipImage = mipmapped.getImage().id;
        mipmapped.loadData(b_);
        check("mipmapped update recreates image again", mipmapped.getImage().id != firstMipImage);
        check("mipmapped new image has no warning", warnings == 0);
        expectPixels("mipmapped second update reads B", mipmapped, b_);
        check("all uploads occurred in one device frame", sapp_frame_count() == firstFrame_);
        secondFrame_ = true;
    }

private:
    void expectPixels(const char* name, const Texture& texture, const Pixels& expected) {
        fbo_.begin(0, 0, 0, 0);
        setColor(1.0f);
        texture.draw(0, 0);
        fbo_.end();
        vector<unsigned char> bytes(4 * 4 * 4);
        check("FBO readback succeeds", fbo_.readPixels(bytes.data()));
        check(name, memcmp(bytes.data(), expected.getData(), bytes.size()) == 0);
    }

    Fbo fbo_;
    Texture texture_;
    Pixels a_, b_;
    uint64_t firstFrame_ = 0;
    bool secondFrame_ = false;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc <= 1 || strcmp(argv[1], "--gpu-check") != 0) {
        printf("SKIP: textureUploadGuard needs --gpu-check and a display\n");
        return 0;
    }
    WindowSettings settings;
    settings.setSize(32, 32);
    settings.setHighDpi(false);
    runApp<UploadGuardApp>(settings);
    check("both frames completed", completed);
    return failures ? 1 : 0;
}
