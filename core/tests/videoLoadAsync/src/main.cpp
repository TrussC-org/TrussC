#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <fstream>

namespace {
using namespace tc;
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void deliver() {
    internal::deliverResourceLoadBatch(internal::takeResourceLoadBatch());
}

VideoPlayer* exitPlayer = nullptr;
struct ExitWithPendingLoad : App {
    void setup() override {
        exitPlayer->loadAsync("missing-at-exit.mp4");
        exitApp();
    }
};

struct FrameDelivery : App {
    VideoPlayer player;
    EventListener listener;
    bool settingUp = true;
    int events = 0;
    void setup() override {
        listener = player.onLoad.listen([this](LoadResult& result) {
            ++events;
            check("App loop delivers once on main after setup", !settingUp && isMainThread()
                  && !player.isLoading() && result.error == LoadError::FileNotFound && events == 1);
            exitApp();
        });
        player.loadAsync("missing-in-app.mp4");
        check("setup does not receive completion inline", player.isLoading() && events == 0);
        settingUp = false;
    }
    void cleanup() override { check("App received its completion", events == 1); }
};
} // namespace

TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::appRunToken() = std::make_shared<std::atomic<bool>>(true);
    VideoPlayer player;
    int events = 0;
    LoadError lastError = LoadError::None;
    auto listener = player.onLoad.listen([&](LoadResult& result) {
        ++events;
        lastError = result.error;
        if (result) check("success listener sees the committed video", player.isLoaded());
        check("completion has settled loading state on main", isMainThread() && !player.isLoading());
    });
    player.loadAsync("missing-first.mp4");
    check("loading begins without inline notification", player.isLoading() && events == 0);
    deliver(); deliver();
    check("failure is delivered exactly once", events == 1 && lastError == LoadError::FileNotFound);

    player.loadAsync("missing-stale.mp4");
    auto stale = internal::takeResourceLoadBatch();
    player.loadAsync("missing-current.mp4");
    internal::deliverResourceLoadBatch(std::move(stale));
    check("replacement suppresses already queued stale completion", events == 1 && player.isLoading());
    deliver();
    check("replacement gets one completion", events == 2 && !player.isLoading());

    player.loadAsync("missing-close.mp4"); player.close(); deliver();
    check("close cancels without notifying", events == 2 && !player.isLoading());
    player.loadAsync("missing-before-sync.mp4");
    player.load("missing-sync.mp4"); deliver();
    check("synchronous load cancels without notifying", events == 2 && !player.isLoading());

    player.loadAsync("missing-before-move.mp4");
    VideoPlayer moved(std::move(player)); deliver();
    check("moving cancels pending load and leaves destination idle", events == 2
          && !moved.isLoading() && !player.isLoading());
    {
        auto owned = std::make_unique<VideoPlayer>();
        auto dyingListener = owned->onLoad.listen([&](LoadResult&) { ++events; });
        owned->loadAsync("missing-before-destruction.mp4");
        owned.reset(); deliver();
        check("destruction suppresses completion", events == 2);
    }

#if defined(__linux__) && !defined(__ANDROID__)
    // Match Image's admission with no pending capacity. Every URL must fail
    // before a decoder (or network operation) starts, and notify only later.
    internal::setResourceLoadLimitsForTests(1, 0, SIZE_MAX);
    const auto rejectedBefore = internal::resourceLoadUsageForTests().rejected;
    int rejectedEvents = 0;
    bool rejectionState = true;
    std::vector<std::unique_ptr<VideoPlayer>> urls;
    std::vector<EventListener> urlListeners;
    Image image;
    auto imageListener = image.onLoad.listen([&](LoadResult& result) {
        ++rejectedEvents;
        rejectionState &= result.error == LoadError::TooLarge && !image.isLoading();
    });
    image.loadAsync("https://admission.invalid/image.png");
    for (int i = 0; i < 8; ++i) {
        auto url = std::make_unique<VideoPlayer>();
        urlListeners.push_back(url->onLoad.listen([&, raw = url.get()](LoadResult& result) {
            ++rejectedEvents;
            rejectionState &= result.error == LoadError::TooLarge && !raw->isLoading();
        }));
        url->loadAsync("https://admission.invalid/movie.mp4");
        urls.push_back(std::move(url));
    }
    check("URL rejection is not delivered inline", rejectedEvents == 0);
    deliver(); deliver();
    const auto rejectedUsage = internal::resourceLoadUsageForTests();
    check("video URL loads use Image's shared request admission", rejectedEvents == 9
          && rejectionState && rejectedUsage.rejected == rejectedBefore + 9
          && rejectedUsage.outstanding == 0 && rejectedUsage.active == 0 && rejectedUsage.pending == 0);
    player.loadAsync("missing-local-above-limits.mp4"); deliver();
    check("local video keeps its existing admission policy", events == 3
          && lastError == LoadError::FileNotFound);
    internal::setResourceLoadLimitsForTests(1, 1, SIZE_MAX);
    urls[0]->loadAsync("https://admission.invalid/movie.mp4");
    check("admitted video URL holds a request credit without a decoder worker",
          internal::resourceLoadUsageForTests().outstanding == 1
          && internal::resourceLoadUsageForTests().active == 0);
    urls[0]->close(); deliver();
    check("cancel returns video URL admission before application",
          rejectedEvents == 9 && internal::resourceLoadUsageForTests().outstanding == 0);

    // Preserve a shared mailbox rejection before the deferred decoder runs.
    struct RetainedLoad : internal::ResourceLoadState {
        size_t retainedBytes() const noexcept override { return 1; }
    };
    internal::setResourceLoadLimitsForTests(4, 64, 0);
    auto retained = std::make_shared<RetainedLoad>();
    retained->run = internal::appRunToken();
    retained->localFile = true;
    internal::completeResourceLoad(retained);
    player.loadAsync("https://"); // No host: never requires a network connection.
    deliver();
    check("fallback preserves shared load rejection", events == 4 && lastError == LoadError::TooLarge
          && !player.isLoaded() && !player.isLoading());
    internal::setResourceLoadLimitsForTests(4, 64, size_t(1024) * 1024 * 1024);

    // A tiny local Y4M stream needs no external encoder or downloads. C444
    // exercises the real FFmpeg RGBA path without a GPU/NV12 shader.
    const auto path = fs::absolute("video-load-async.y4m");
    {
        std::ofstream file(path, std::ios::binary);
        file << "YUV4MPEG2 W16 H16 F25:1 Ip A1:1 C444\n";
        const std::string plane(16 * 16, char(128));
        for (int i = 0; i < 3; ++i) {
            file << "FRAME\n";
            for (int j = 0; j < 3; ++j) file.write(plane.data(), plane.size());
        }
    }
    player.setAutoPoster(false);
    player.loadAsync(path);
    check("Linux fallback does not open the decoder inline", !player.isLoaded() && player.isLoading());
    deliver();
    check("Linux backend loads local video before success notification", events == 5
          && lastError == LoadError::None && player.isLoaded() && player.getWidth() == 16
          && player.getPath() == path && !player.isLoading());
    player.close();
    player.setAutoPoster(true);
    player.loadAsync(path); deliver();
    check("Linux fallback retains automatic poster behavior", events == 6 && player.isReady()
          && player.getPixels() != nullptr);
    player.close();
    fs::remove(path);
#endif

    // A completion created by a listener belongs to the next batch.
    int chainedEvents = 0;
    auto chain = moved.onLoad.listen([&](LoadResult&) {
        if (++chainedEvents == 1) moved.loadAsync("missing-chained.mp4");
    });
    moved.loadAsync("missing-chain-start.mp4"); deliver();
    check("listener can restart without inline delivery", chainedEvents == 1 && moved.isLoading());
    deliver();
    check("restarted load arrives in the next pass", chainedEvents == 2 && !moved.isLoading());

    auto self = std::make_unique<VideoPlayer>();
    bool selfNotified = false;
    auto selfListener = self->onLoad.listen([&](LoadResult&) { selfNotified = true; self.reset(); });
    self->loadAsync("missing-self.mp4"); deliver();
    check("completion may destroy its player", selfNotified && !self);

    runHeadlessApp<FrameDelivery>();
    const int beforeExit = events;
    exitPlayer = &player;
    runHeadlessApp<ExitWithPendingLoad>();
    deliver();
    check("App exit drops pending completion", events == beforeExit && !player.isLoading());
    return failures ? 1 : 0;
}
