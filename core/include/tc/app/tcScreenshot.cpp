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
    std::shared_ptr<size_t> admitted;
    Pixels pixels;
    // A discarded completion must release its slot too (on main at app exit).
    ~ScreenshotState() { if (admitted) --*admitted; }
};
namespace {
struct ScreenshotTestBackend { ScreenshotCapture capture; ScreenshotWriter writer; };
ScreenshotTestBackend& testBackend() {
    static auto* backend = new ScreenshotTestBackend;
    return *backend;
}
struct Writer {
    std::mutex mutex;
    // A nonempty queue includes the job currently being written. Only the
    // transition from empty to nonempty launches a G1-retired worker.
    std::deque<std::shared_ptr<ScreenshotState>> jobs;
};
Writer& writer() { static auto* value = new Writer; return *value; }
void complete(const std::shared_ptr<ScreenshotState>& state, SaveResult result) {
    // Release the expensive payload before making its admission slot reusable.
    state->pixels = Pixels{};
    enqueueResourceLoad([state, result = std::move(result)]() mutable {
        if (state->admitted) { --*state->admitted; state->admitted.reset(); }
        if (!appRunIsAlive(state->run)) return;
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
    try {
        probeResourceLoad("screenshot-writer-launch", state->path);
        startResourceLoad([&owner] {
            for (;;) {
                std::shared_ptr<ScreenshotState> job;
                {
                    std::lock_guard<std::mutex> lock(owner.mutex);
                    job = owner.jobs.front();
                }
                SaveResult result;
                try {
                    fs::path resolved;
                    result = prepareOutputPath(job->path, resolved, "Screenshot");
                    if (result) result = testBackend().writer ? testBackend().writer(job->pixels, job->path)
                                                   : writeScreenshotPixels(job->pixels, job->path);
                } catch (const std::exception& e) {
                    result = SaveResult::fail(SaveError::WriteFailed, e.what());
                } catch (...) {
                    result = SaveResult::fail(SaveError::WriteFailed, "Screenshot writer threw an unknown exception");
                }
                complete(job, std::move(result));
                std::lock_guard<std::mutex> lock(owner.mutex);
                auto& queue = owner.jobs;
                queue.pop_front();
                if (queue.empty()) return;
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(owner.mutex);
        owner.jobs.clear();
        throw;
    }
}
}
ScreenshotQueue::ScreenshotQueue() = default;
ScreenshotQueue::~ScreenshotQueue() { registration.reset(); closeScreenshots(*this); }
void closeScreenshots(ScreenshotQueue& window) noexcept {
    TeardownSteps steps;
    // Pop before attempting completion. Moving a deque can allocate (including
    // MSVC Debug proxies); moving a shared_ptr and popping cannot. If result
    // construction/enqueue fails, dropping this job releases its admission and
    // captures without a callback. Continue cancelling every remaining request.
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
    try { state->path = fs::absolute(resolveScreenshotPath(requested)).lexically_normal(); }
    catch (const std::exception& e) {
        state->path = requested;
        complete(state, SaveResult::fail(SaveError::WriteFailed, e.what()));
        return;
    }
#endif
    if (!window || *window->admitted >= 4
#ifndef __EMSCRIPTEN__
        || requested.empty()
#endif
        ) {
        complete(state, SaveResult::fail(SaveError::WriteFailed,
            !window ? "No render window for screenshot" : *window->admitted >= 4
                ? "Screenshot queue is full" : "No file name in path"));
        return;
    }
    // Merely creating a window must not start the async host/reaper.
    if (!window->registration)
        window->registration = registerResourceStop([window] {
            // std::exit may run on a worker. Its invalidated run silences
            // uncaptured jobs; window storage is touched only by main.
            if (isMainThread()) closeScreenshots(*window);
        });
    state->admitted = window->admitted;
    ++*state->admitted;
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
        try {
            if (!(testBackend().capture ? testBackend().capture(state->pixels) : captureWindow(state->pixels))) {
                complete(state, SaveResult::fail(SaveError::WriteFailed, "Screenshot capture failed"));
                continue;
            }
            write(state);
        } catch (const std::exception& e) {
            complete(state, SaveResult::fail(SaveError::WriteFailed, e.what()));
        }
#endif
    }
}
void setScreenshotBackendForTests(ScreenshotCapture capture, ScreenshotWriter writer) {
    testBackend().capture = std::move(capture); testBackend().writer = std::move(writer);
}
}
void saveScreenshotAsync(const fs::path& path) {
    auto* window = !headless::active && sapp_isvalid() ? &internal::currentWindowContext() : nullptr;
    if (window && !window->isMain && !internal::currentWindow()) window = nullptr;
    internal::submitScreenshot(window ? &window->asyncScreenshots : nullptr, path);
    if (window && window->isMain) redraw();
}
}
