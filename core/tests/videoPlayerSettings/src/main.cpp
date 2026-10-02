// Persistent video settings (#282). No decoder, window, GPU or audio device.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"

#include <cstdio>
#include <vector>

using namespace tc;

namespace {

enum class Operation { Loop, Volume, Pan, Play, Speed, Pause };

struct Call {
    Operation operation;
    float value;
    bool loaded;
    bool operator==(const Call&) const = default;
};

class FakeVideoPlayer : public VideoPlayerBase {
public:
    std::vector<Call> calls;
    bool backendLoop = false;
    float backendVolume = 1.0f;
    float backendPan = 0.0f;
    float backendSpeed = 1.0f;

    LoadResult load(const fs::path&) override {
        close();
        // Each load creates a backend with fresh defaults.
        backendLoop = false;
        backendVolume = backendSpeed = 1.0f;
        backendPan = 0.0f;
        initialized_ = true;
        applyCachedStateToPlatform();
        return LoadResult::success();
    }

    void close() override {
        initialized_ = playing_ = paused_ = done_ = false;
    }

    void update() override {}
    float getDuration() const override { return 0.0f; }
    float getPosition() const override { return 0.0f; }
    int getCurrentFrame() const override { return 0; }
    int getTotalFrames() const override { return 0; }
    void setFrame(int) override {}
    void nextFrame() override {}
    void previousFrame() override {}
    unsigned char* getPixels() override { return nullptr; }
    const unsigned char* getPixels() const override { return nullptr; }

protected:
    void playImpl() override {
        calls.push_back({Operation::Play, 0.0f, initialized_});
        backendSpeed = 1.0f; // Model AVPlayer's play resetting the rate.
    }
    void stopImpl() override {}
    void setPausedImpl(bool paused) override {
        calls.push_back({Operation::Pause, paused ? 1.0f : 0.0f, initialized_});
        if (!paused) backendSpeed = 1.0f;
    }
    void setPositionImpl(float) override {}
    void setVolumeImpl(float volume) override {
        calls.push_back({Operation::Volume, volume, initialized_});
        backendVolume = volume;
    }
    void setSpeedImpl(float speed) override {
        calls.push_back({Operation::Speed, speed, initialized_});
        backendSpeed = speed;
    }
    void setPanImpl(float pan) override {
        calls.push_back({Operation::Pan, pan, initialized_});
        backendPan = pan;
    }
    void setLoopImpl(bool loop) override {
        calls.push_back({Operation::Loop, loop ? 1.0f : 0.0f, initialized_});
        backendLoop = loop;
    }
};

int failures = 0;

void check(const char* name, bool ok) {
    std::printf("%-72s %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

bool settingsMatch(const FakeVideoPlayer& player) {
    return player.backendLoop == player.isLoop()
        && player.backendVolume == player.getVolume()
        && player.backendPan == player.getPan();
}

} // namespace

TC_CORE_TEST_MAIN() {
    FakeVideoPlayer player;
    player.setLoop(true);
    player.setVolume(0.0f);
    player.setPan(-0.5f);
    player.setSpeed(2.0f);
    player.play();
    check("settings and play before load do not call the backend", player.calls.empty());

    const std::vector<Call> cachedCalls = {
        {Operation::Loop, 1.0f, true},
        {Operation::Volume, 0.0f, true},
        {Operation::Pan, -0.5f, true},
    };
    check("first load succeeds", static_cast<bool>(player.load("first")));
    check("load applies loop, volume and pan after initialization", player.calls == cachedCalls);
    check("backend settings match the getters", settingsMatch(player));
    check("load keeps playback stopped and defers speed", !player.isPlaying()
          && player.backendSpeed == 1.0f && player.getSpeed() == 2.0f);

    player.calls.clear();
    player.play();
    check("play applies cached speed after backend play", player.calls == std::vector<Call>{
        {Operation::Play, 0.0f, true}, {Operation::Speed, 2.0f, true}});
    check("play keeps the requested speed", player.isPlaying()
          && player.backendSpeed == player.getSpeed());

    player.calls.clear();
    check("second load succeeds", static_cast<bool>(player.load("second")));
    check("reload applies all cached settings again", player.calls == cachedCalls);
    check("reload preserves getters and backend settings", settingsMatch(player)
          && player.isLoop() && player.getVolume() == 0.0f
          && player.getPan() == -0.5f && player.getSpeed() == 2.0f);
    check("reload stays stopped", !player.isPlaying());

    player.setLoop(false);
    player.setVolume(0.25f);
    player.setPan(0.5f);
    player.setSpeed(0.5f);
    check("setters after load apply immediately", settingsMatch(player)
          && player.backendSpeed == player.getSpeed());
    player.calls.clear();
    player.play();
    check("setSpeed followed by play survives a backend rate reset",
          player.calls == std::vector<Call>{
              {Operation::Play, 0.0f, true}, {Operation::Speed, 0.5f, true}}
          && player.backendSpeed == player.getSpeed());

    player.setPaused(true);
    player.calls.clear();
    player.setPaused(false);
    check("resume keeps the requested speed", player.calls == std::vector<Call>{
        {Operation::Pause, 0.0f, true}, {Operation::Speed, 0.5f, true}}
        && player.backendSpeed == player.getSpeed());

    player.close();
    player.calls.clear();
    player.load("third");
    check("changed settings survive close and load", player.calls == std::vector<Call>{
        {Operation::Loop, 0.0f, true},
        {Operation::Volume, 0.25f, true},
        {Operation::Pan, 0.5f, true}}
        && settingsMatch(player) && player.getSpeed() == 0.5f);

    player.close();
    player.setSpeed(0.0f);
    player.load("zero-speed");
    player.calls.clear();
    player.play();
    check("zero speed is also applied at play", player.calls == std::vector<Call>{
        {Operation::Play, 0.0f, true}, {Operation::Speed, 0.0f, true}}
        && player.backendSpeed == player.getSpeed());

    std::printf("\n%s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
