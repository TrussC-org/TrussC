#include <TrussC.h>

namespace trussc {
Event<ScreenshotSaveEventArgs>& onScreenshotSave() {
    static auto* event = new Event<ScreenshotSaveEventArgs>;
    return *event;
}
namespace internal {
std::FILE* openScreenshotTemporary(const fs::path& path, fs::path& temporary) {
    static std::atomic<uint64_t> next{0};
    for (;;) {
        temporary = path;
        temporary += "." + std::to_string(next.fetch_add(1)) + ".tmp";
        // Exclusive creation also prevents collisions with other processes or
        // stale temporaries. Never truncate somebody else's in-flight encoder.
        auto* file = openFile(temporary, "wbx");
        if (file || errno != EEXIST) return file;
    }
}
struct ScreenshotState {
    fs::path path;
    std::weak_ptr<std::atomic<bool>> run = appRunToken();
    std::weak_ptr<AppOwner> owner = currentAppOwner();
    Pixels pixels;
};
namespace {
struct Writer {
    std::mutex mutex;
    // A nonempty queue includes the job currently being written. Only the
    // transition from empty to nonempty launches a G1-retired worker.
    std::deque<std::shared_ptr<ScreenshotState>> jobs;
};
Writer& writer() { static auto* value = new Writer; return *value; }
void complete(const std::shared_ptr<ScreenshotState>& state, SaveResult result) {
    // Release the captured pixels before posting completion.
    state->pixels = Pixels{};
    enqueueResourceLoad([state, result = std::move(result)]() mutable {
        if (!appRunIsAlive(state->run)) return;
        AppOwnerScope scope(state->owner.lock());
        ScreenshotSaveEventArgs args{state->path, std::move(result)};
        onScreenshotSave().notify(args); // snapshot survives a listener deleting itself
    });
}
void write(const std::shared_ptr<ScreenshotState>& state) {
    auto& owner = writer();
    bool launch;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        auto& queue = owner.jobs;
        launch = queue.empty();
        queue.push_back(state);
    }
    if (!launch) return;
    startResourceLoad([&owner] {
        for (;;) {
            std::shared_ptr<ScreenshotState> job;
            {
                std::lock_guard<std::mutex> lock(owner.mutex);
                job = owner.jobs.front();
            }
            fs::path resolved;
            auto result = prepareOutputPath(job->path, resolved, "Screenshot");
            if (result) result = writeScreenshotPixels(job->pixels, job->path);
            complete(job, std::move(result));
            std::lock_guard<std::mutex> lock(owner.mutex);
            owner.jobs.pop_front();
            if (owner.jobs.empty()) return;
        }
    });
}
}
ScreenshotQueue::ScreenshotQueue() = default;
ScreenshotQueue::~ScreenshotQueue() { registration.reset(); closeScreenshots(*this); }
void closeScreenshots(ScreenshotQueue& window) noexcept {
    if (frameworkShutdown()) return;
    TeardownSteps steps;
    while (!window.pending.empty()) {
        auto job = std::move(window.pending.front());
        window.pending.pop_front();
        steps.run("screenshot cancellation", [&] {
            complete(job, SaveResult::fail(SaveError::WriteFailed,
                                         "Window closed before screenshot capture"));
        });
    }
}
void submitScreenshot(ScreenshotQueue* window, const fs::path& requested) {
    auto state = std::make_shared<ScreenshotState>();
#ifdef __EMSCRIPTEN__
    state->path = resolveScreenshotDownloadName(requested);
#else
    std::error_code ec;
    state->path = fs::absolute(resolveScreenshotPath(requested), ec).lexically_normal();
    if (ec) {
        state->path = requested;
        complete(state, SaveResult::fail(SaveError::WriteFailed, ec.message()));
        return;
    }
#endif
    if (!window
#ifndef __EMSCRIPTEN__
        || requested.empty()
#endif
        ) {
        complete(state, SaveResult::fail(SaveError::WriteFailed,
            !window ? "No render window for screenshot" : "No file name in path"));
        return;
    }
    // Merely creating a window must not start the async host/reaper.
    if (!window->registration)
        window->registration = registerResourceStop([window] {
            closeScreenshots(*window);
        });
    window->pending.push_back(state);
}
void captureScreenshots(ScreenshotQueue& window) {
    auto jobs = std::move(window.pending);
    window.pending.clear();
    for (auto& state : jobs) {
        if (!appRunIsAlive(state->run)) { complete(state, SaveResult::fail(SaveError::WriteFailed, "App run ended")); continue; }
#ifdef __EMSCRIPTEN__
        captureScreenshotDownload(state->path, [state](SaveResult result) { complete(state, std::move(result)); });
#else
        if (!captureWindow(state->pixels)) {
            complete(state, SaveResult::fail(SaveError::WriteFailed, "Screenshot capture failed"));
            continue;
        }
        write(state);
#endif
    }
}

}
void saveScreenshotAsync(const fs::path& path) {
    if (headless::isActive()) { internal::submitScreenshot(nullptr, path); return; }
    auto* window = internal::findWindowApiContext();
    if (!window) return;
    internal::submitScreenshot(&window->asyncScreenshots, path);
    window->loop.redrawCount = std::max(window->loop.redrawCount, 1);
}
}
