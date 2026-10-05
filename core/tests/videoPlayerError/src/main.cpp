#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <thread>

using namespace std;
using namespace tc;

namespace {
int failures = 0;
void check(const char* name, bool ok) {
    printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}

class FakePlayer : public VideoPlayerBase {
public:
    array<unsigned char, 4> pixels{255, 0, 0, 255};
    int pauses = 0;
    int stops = 0;
    float position = 0.5f;
    LoadResult load(const fs::path&) override {
        clearPlaybackError();
        initialized_ = true;
        markFrameNew();
        return LoadResult::success();
    }
    void close() override {
        clearPlaybackError();
        initialized_ = playing_ = firstFrameReceived_ = false;
    }
    void update() override { dispatchPlaybackError(); }
    void fail() { reportPlaybackError("decoder failed", -42); }
    void moveErrorFrom(FakePlayer& other) { movePlaybackErrorFrom(other); }
    float getDuration() const override { return 2; }
    float getPosition() const override { return position; }
    int getCurrentFrame() const override { return 1; }
    int getTotalFrames() const override { return 2; }
    void setFrame(int) override {}
    void nextFrame() override {}
    void previousFrame() override {}
    unsigned char* getPixels() override { return pixels.data(); }
    const unsigned char* getPixels() const override { return pixels.data(); }
protected:
    void playImpl() override {}
    void stopImpl() override { ++stops; position = 0; pixels.fill(0); }
    void setPausedImpl(bool paused) override { if (paused) ++pauses; }
    void setPositionImpl(float p) override { position = p; }
    void setVolumeImpl(float) override {}
    void setSpeedImpl(float) override {}
    void setPanImpl(float) override {}
    void setLoopImpl(bool) override {}
};

void checkState() {
    FakePlayer player;
    check("new player has no error", !player.hasError() && player.getErrorMessage().empty());
    player.load({});
    player.play();
    const auto lastPixels = player.pixels;
    const auto mainThread = this_thread::get_id();
    int events = 0;
    auto listener = player.onError.listen([&](VideoErrorEventArgs& error) {
        ++events;
        check("error callback runs on main thread", this_thread::get_id() == mainThread);
        check("message and backend code survive", error.message == "decoder failed" && error.errorCode == -42);
        check("state already stopped inside callback", player.hasError() && !player.isPlaying() && !player.isDone());
    });
    thread decoder([&] { player.fail(); player.fail(); });
    decoder.join();
    check("worker does not invoke listeners", events == 0);
    player.update();
    player.fail();
    player.update();
    player.update();
    check("one event for the stopped playback", events == 1 && player.pauses == 1);
    check("polling retains message", player.getErrorMessage() == "decoder failed");
    check("loaded and last picture retained", player.isLoaded() && player.isReady() && player.pixels == lastPixels);
    check("error neither rewinds nor announces a frame", player.stops == 0 && player.getPosition() == 0.5f && !player.isFrameNew());
    player.setPosition(0.25f);
    player.play();
    check("app can seek and retry without clearing error", player.isPlaying() && player.hasError() && player.getPosition() == 0.25f);
    player.fail();
    player.update();
    check("a failed retry is reported", events == 2);
    player.load({});
    check("successful load clears error", !player.hasError() && player.getErrorMessage().empty());
    player.fail();
    player.close();
    player.update();
    check("close discards queued error", !player.hasError() && events == 2);

    FakePlayer moved;
    player.load({});
    player.fail();
    moved.load({});
    moved.moveErrorFrom(player);
    moved.update();
    check("move retains pending failure", moved.hasError() && !player.hasError());
    moved.close();
    check("close clears delivered error", !moved.hasError());

    player.load({});
    listener.disconnect();
    auto closer = player.onError.listen([&](VideoErrorEventArgs&) { player.close(); });
    player.fail();
    player.update();
    check("listener may close player", !player.isLoaded() && !player.hasError());
    closer.disconnect();
    auto reloader = player.onError.listen([&](VideoErrorEventArgs&) { player.load({}); player.play(); });
    player.load({});
    player.fail();
    player.update();
    check("listener may reload and play", player.isLoaded() && player.isPlaying() && !player.hasError());
}

class FrameCheckApp : public App {
public:
    void draw() override {
        FakePlayer player;
        player.load({});
        Pixels poster;
        poster.setFromPixels(player.pixels.data(), 1, 1, 4);
        player.getTexture().allocate(poster);
        const auto image = player.getTexture().getImage();
        Fbo fbo;
        fbo.allocate(4, 4);
        auto read = [&]() {
            fbo.begin(0, 0, 0, 0);
            setColor(1.0f);
            player.draw(0, 0, 4, 4);
            fbo.end();
            array<unsigned char, 64> pixels{};
            check("read last frame", fbo.readPixels(pixels.data()));
            return pixels;
        };
        const auto before = read();
        player.play();
        player.fail();
        player.update();
        check("valid texture survives error", image.id == player.getTexture().getImage().id && sg_query_image_state(image) == SG_RESOURCESTATE_VALID);
        check("rendered last frame survives error", before == read() && before[0] == 255 && before[1] == 0);
        exitApp();
    }
};
fs::path videoPath;
bool expectVideoError = false;
bool expectBadPacket = false;
class VideoCheckApp : public App {
public:
    void setup() override {
        logger = getLogger().onLog.listen([&](LogEventArgs& log) {
            if (log.message.find("[VideoPlayer]") != 0) return;
            if (log.level == LogLevel::Error) ++errorLogs;
            if (log.level == LogLevel::Warning && log.message.find("skipping invalid") != string::npos) ++badPackets;
        });
        player.setUseHwAccel(false);
        player.setResyncThreshold(0);
        listener = player.onError.listen([&](VideoErrorEventArgs& error) {
            ++events;
            check("real backend logs before notification", errorLogs == 1);
            check("real backend provides message and code", !error.message.empty() && error.errorCode != 0);
        });
        if (!player.load(videoPath)) {
            check("real video loads", false);
            exitApp();
            return;
        }
        player.play();
    }
    void update() override {
        if (checkingRecoverySeek) {
            player.update();
            check("seek after failure uploads before play", !player.isPlaying() && player.hasError() &&
                  sg_query_image_info(player.getTexture().getImage()).upd_frame_index != seekUploadFrame);
            check("recovery seek does not repeat error", events == 1 && errorLogs == 1);
            finish();
            return;
        }
        const auto texture = player.getTexture().getImage();
        vector<unsigned char> before;
        if (player.getPixels()) {
            const size_t size = static_cast<size_t>(player.getWidth() * player.getHeight() * 4);
            before.assign(player.getPixels(), player.getPixels() + size);
        }
        player.update();
        if (!player.hasError() && !player.isDone()) return;
        if (player.hasError()) {
            check("real failure keeps last pixels and texture", !before.empty() &&
                  equal(before.begin(), before.end(), player.getPixels()) &&
                  texture.id == player.getTexture().getImage().id);
        }
        check("real backend distinguishes failure from EOF", player.hasError() == expectVideoError);
        check("real backend stops", !player.isPlaying());
        check("real backend retains loaded frame", player.isLoaded() && player.isReady());
        for (int i = 0; i < 3; ++i) player.update();
        check("real backend reports once", events == (expectVideoError ? 1 : 0));
        check("real backend logs once per error stop", errorLogs == (expectVideoError ? 1 : 0));
        if (expectBadPacket) {
            check("one invalid packet was skipped", badPackets == 1);
            check("frames after bad packet reach EOF", player.isDone() && player.getCurrentFrame() >= player.getTotalFrames() - 1);
        }
        if (player.hasError()) {
            // Two seeks in one app frame force the second poster upload to
            // defer to update(), exercising the removed errorStopped_ guard.
            player.setPosition(0);
            player.setPosition(0);
            seekUploadFrame = sg_query_image_info(player.getTexture().getImage()).upd_frame_index;
            checkingRecoverySeek = true;
            return;
        }
        finish();
    }
private:
    void finish() {
        if (player.hasError()) {
            auto message = player.getErrorMessage();
            check("failed reload stays a load failure", !player.load(videoPath / "missing-file"));
            check("failed reload retains runtime message", player.getErrorMessage() == message);
        }
        player.close();
        check("real close clears error", !player.hasError());
        exitApp();
    }
private:
    VideoPlayer player;
    EventListener listener;
    EventListener logger;
    atomic<int> errorLogs{0};
    atomic<int> badPackets{0};
    int events = 0;
    bool checkingRecoverySeek = false;
    uint32_t seekUploadFrame = 0;
};
} // namespace

TC_CORE_TEST_MAIN(int argc, char** argv) {
    if (argc > 2 && strcmp(argv[1], "--video-check") == 0) {
        videoPath = fs::absolute(argv[2]);
        expectVideoError = argc > 3 && strcmp(argv[3], "error") == 0;
        expectBadPacket = argc > 3 && strcmp(argv[3], "bad-packet") == 0;
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        runApp<VideoCheckApp>(settings);
    } else if (argc > 1 && strcmp(argv[1], "--gpu-check") == 0) {
        WindowSettings settings;
        settings.setSize(64, 64);
        settings.setHighDpi(false);
        runApp<FrameCheckApp>(settings);
    } else {
        checkState();
    }
    return failures ? 1 : 0;
}
