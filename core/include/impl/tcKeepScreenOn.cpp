// Windows/Linux/Web pthread requests are applied by the host main loop, never the caller.
#if defined(_WIN32) || (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__)) || defined(__EMSCRIPTEN_PTHREADS__)
#include <atomic>
#include <mutex>

namespace trussc {
namespace internal {
void applyKeepScreenOn(bool enabled, bool headless);
}
namespace {
struct KeepScreenRequest {
    std::mutex mutex;
    bool requested = false; // Protected by mutex; public API accepts any thread.
    bool applied = false;  // Host main thread only.
};

// Retain inert storage to avoid destructor ordering and worker-thread OS calls.
std::atomic<KeepScreenRequest*> keepScreenStorage{nullptr};
KeepScreenRequest& keepScreenRequest() {
    static auto* request = [] {
        auto* value = new KeepScreenRequest;
        keepScreenStorage.store(value, std::memory_order_release);
        return value;
    }();
    return *request;
}

void applyRequest(KeepScreenRequest& request, bool headless, bool shutdown = false) {
    bool target;
    {
        std::lock_guard<std::mutex> lock(request.mutex);
        if (shutdown) request.requested = false;
        target = request.requested;
    }
    if (target == request.applied) return;
    request.applied = target;
    // No request lock during OS work or synchronous log listeners.
    internal::applyKeepScreenOn(target, headless);
}
} // namespace

void setKeepScreenOn(bool enabled) {
    auto& request = keepScreenRequest();
    std::lock_guard<std::mutex> lock(request.mutex);
    request.requested = enabled;
}
bool getKeepScreenOn() {
    auto& request = keepScreenRequest();
    std::lock_guard<std::mutex> lock(request.mutex);
    return request.requested;
}

namespace internal {
void updateKeepScreenOn(bool headless) noexcept {
    // C callbacks must not receive exceptions from locks, OS work or logging.
    try {
        if (auto* request = keepScreenStorage.load(std::memory_order_acquire))
            applyRequest(*request, headless);
    } catch (...) {}
}
void shutdownKeepScreenOn() noexcept {
    try {
        if (auto* request = keepScreenStorage.load(std::memory_order_acquire))
            applyRequest(*request, false, true);
    } catch (...) {}
}
} // namespace internal
} // namespace trussc
#endif
