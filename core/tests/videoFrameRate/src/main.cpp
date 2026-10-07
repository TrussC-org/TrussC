// Exercise the shared VideoPlayerBase frame-rate guard without a decoder (#289).
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <limits>

using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class FakeVideoPlayer : public VideoPlayerBase {
public:
    float frameRate = 0.0f;
    ~FakeVideoPlayer() { close(); }
    LoadResult load(const fs::path&) override {
        initialized_ = true;
        seconds_ = 1.0f;
        return LoadResult::success();
    }
    void close() override { initialized_ = false; }
    float getDuration() const override { return 2.0f; }
    float getPosition() const override { return seconds_ / getDuration(); }
    mutable int frameCalls = 0;
    mutable int rateQueries = 0;
    void update() override {}
    unsigned char* getPixels() override { return nullptr; }
    const unsigned char* getPixels() const override { return nullptr; }
    // Return raw loaded rates so the base guard itself must reject NaN/Inf.
    float getFrameRate() const override {
        ++rateQueries;
        return initialized_ ? frameRate : 0.0f;
    }

protected:
    int getCurrentFrameImpl() const override {
        ++frameCalls;
        return static_cast<int>(seconds_ * frameRate);
    }
    int getTotalFramesImpl() const override {
        ++frameCalls;
        return static_cast<int>(getDuration() * frameRate);
    }
    void setFrameImpl(int frame) override {
        ++frameCalls;
        seconds_ = frame / frameRate;
    }
    void nextFrameImpl() override {
        ++frameCalls;
        seconds_ += 1.0f / frameRate;
    }
    void previousFrameImpl() override {
        ++frameCalls;
        seconds_ -= 1.0f / frameRate;
    }

    void playImpl() override {}
    void stopImpl() override {}
    void setPausedImpl(bool) override {}
    void setVolumeImpl(float) override {}
    void setSpeedImpl(float) override {}
    void setPanImpl(float) override {}
    void setLoopImpl(bool) override {}
    void setPositionImpl(float pct) override { seconds_ = pct * getDuration(); }

private:
    float seconds_ = 1.0f;
};

bool near(float a, float b) { return std::abs(a - b) < 0.00001f; }

void exerciseUnknown(FakeVideoPlayer& player) {
    const int queriesBefore = player.rateQueries;
    for (int i = 0; i < 3; ++i) {
        check("unknown current and total frames return 0",
              player.getCurrentFrame() == 0 && player.getTotalFrames() == 0);
        player.setFrame(12);
        player.nextFrame();
        player.previousFrame();
        player.firstFrame();
    }
    check("unknown frame APIs each check the rate once", player.rateQueries == queriesBefore + 18);
    check("unknown frame operations never enter Impl", player.frameCalls == 0);
    check("unknown frame operations leave position unchanged", player.getPosition() == 0.5f);
}
} // namespace

TC_CORE_TEST_MAIN() {
    int warnings = 0;
    auto listener = getLogger().onLog.listen([&](LogEventArgs& args) {
        if (args.level == LogLevel::Warning &&
            args.message.find("Frame rate is unknown") != std::string::npos) ++warnings;
    });

    for (float rate : {24.0f, 30.0f, 60.0f, 30000.0f / 1001.0f}) {
        FakeVideoPlayer player;
        player.frameRate = rate;
        check("unloaded rate is 0", player.getFrameRate() == 0.0f);
        check("unloaded frame queries return 0",
              player.getCurrentFrame() == 0 && player.getTotalFrames() == 0);
        player.setFrame(10);
        player.nextFrame();
        player.previousFrame();
        player.firstFrame();
        check("unloaded frame operations do not perform frame arithmetic", player.frameCalls == 0);
        player.load({});
        check("known rate is preserved", player.getFrameRate() == rate);
        const int queriesBefore = player.rateQueries;
        check("known total frames match two-second duration",
              player.getTotalFrames() == static_cast<int>(2.0f * rate));
        check("known current frame matches one second",
              player.getCurrentFrame() == static_cast<int>(rate));
        player.setFrame(12);
        check("setFrame uses reported rate", near(player.getCurrentTime(), 12.0f / rate));
        player.nextFrame();
        check("nextFrame advances by one frame", near(player.getCurrentTime(), 13.0f / rate));
        player.previousFrame();
        check("previousFrame retreats by one frame", near(player.getCurrentTime(), 12.0f / rate));
        player.firstFrame();
        check("firstFrame seeks to zero", player.getPosition() == 0.0f);
        check("known frame APIs dispatch to Impl exactly once each", player.frameCalls == 6);
        check("known frame APIs each check the rate once", player.rateQueries == queriesBefore + 6);
        player.close();
        check("closed rate is 0", player.getFrameRate() == 0.0f);
    }
    check("known and unloaded players do not warn", warnings == 0);

    FakeVideoPlayer unknown;
    unknown.load({});
    check("querying unknown rate alone does not warn", unknown.getFrameRate() == 0 && warnings == 0);
    exerciseUnknown(unknown);
    check("all unknown frame APIs share one warning", warnings == 1);
    unknown.setPosition(0.25f);
    check("time-based seek and duration work with unknown rate",
          unknown.getPosition() == 0.25f && unknown.getCurrentTime() == 0.5f
          && unknown.getDuration() == 2.0f);
    unknown.close();
    unknown.load({});
    exerciseUnknown(unknown);
    check("reload does not spam warnings for the same player", warnings == 1);
    unknown.frameRate = 24.0f;
    check("a newly known rate enables frame APIs", unknown.getTotalFrames() == 48);
    unknown.frameRate = 0.0f;
    check("losing the rate again does not repeat the warning",
          unknown.getTotalFrames() == 0 && warnings == 1);

    for (float rate : {-1.0f, std::numeric_limits<float>::infinity(),
                       std::numeric_limits<float>::quiet_NaN(), 0.0f}) {
        int before = warnings;
        FakeVideoPlayer player;
        player.frameRate = rate;
        player.load({});
        exerciseUnknown(player);
        check("each player warns once for an unavailable rate", warnings == before + 1);
    }
    std::printf("%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
