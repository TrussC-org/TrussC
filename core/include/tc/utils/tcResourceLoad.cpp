#include "tcResourceLoad.h"
#include "tcKeptThreads.h"
namespace trussc { class Logger; Logger& getLogger(); }
#ifdef _WIN32
#include <objbase.h>
#endif
#include <condition_variable>
#include <cstdlib>
#include <list>

namespace trussc::internal {
#ifdef __APPLE__
void resourceLoadAutoreleasePool(const std::function<void()>& work);
#endif
namespace {
struct LoadHost {
    std::mutex mutex;
    ResourceLoadBatch mailbox;
    ResourceLoadProbe probe;
    std::vector<std::weak_ptr<std::function<void()>>> stops;
#ifndef __EMSCRIPTEN__
    struct Worker { std::thread thread; bool done = false; };
    std::list<std::shared_ptr<Worker>> workers;
    std::condition_variable changed;
    KeptThreads kept;
    std::thread reaper;
    bool exiting = false;

    LoadHost() {
        kept.start(reaper, [this] {
            std::unique_lock<std::mutex> lock(mutex);
            for (;;) {
                changed.wait(lock, [&] {
                    if (exiting && workers.empty()) return true;
                    for (auto& worker : workers) if (worker->done) return true;
                    return false;
                });
                if (exiting && workers.empty()) return;
                for (auto it = workers.begin(); it != workers.end();) {
                    auto worker = *it;
                    if (!worker->done) { ++it; continue; }
                    // Keep it in the list until the join has completed, so
                    // explicit test drains also wait for State destruction.
                    lock.unlock();
                    kept.release(worker->thread);
                    lock.lock();
                    it = workers.erase(it);
                    changed.notify_all();
                }
            }
        });
    }
#endif
};

// Leaked, like the logger/main-thread queue: late workers and hot reload
// guests must still be able to use the host during static teardown.
LoadHost& host() {
    static auto* owner = [] {
        (void)getLogger(); // register retirement after logger's teardown hooks
        auto* value = new LoadHost;
        std::atexit(finishResourceLoadsAtExit);
        return value;
    }();
    return *owner;
}
} // namespace

void enqueueResourceLoad(std::function<void()> completion) {
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    owner.mailbox.push_back(std::move(completion));
}
ResourceLoadBatch takeResourceLoadBatch() {
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    ResourceLoadBatch batch;
    batch.swap(owner.mailbox);
    return batch;
}
void deliverResourceLoadBatch(ResourceLoadBatch batch) {
    for (auto& slot : batch) {
        auto fn = std::move(slot);
        if (fn) fn();
    }
}
void startResourceLoad(std::function<void()> preparation) {
#ifdef __EMSCRIPTEN__
    // VFS reading/CPU decode starts in a later pass. Its completion enters
    // the same mailbox, and is therefore applied in a subsequent pass.
    enqueueResourceLoad(std::move(preparation));
#else
    auto& owner = host();
    auto worker = std::make_shared<LoadHost::Worker>();
    std::lock_guard<std::mutex> lock(owner.mutex);
    owner.workers.push_back(worker);
    try {
        owner.kept.start(worker->thread, [worker, preparation = std::move(preparation), &owner]() mutable {
#ifdef _WIN32
            const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
#endif
#ifdef __APPLE__
            resourceLoadAutoreleasePool(preparation);
#else
            preparation();
#endif
#ifdef _WIN32
            if (SUCCEEDED(com)) CoUninitialize();
#endif
            preparation = {}; // release CPU payload on the worker when cancelled
            std::lock_guard<std::mutex> finished(owner.mutex);
            worker->done = true;
            owner.changed.notify_all();
        });
    } catch (...) {
        // A failed OS thread launch published no worker. Do not leave an
        // unfinished entry that would make exit wait forever.
        owner.workers.remove(worker);
        owner.changed.notify_all();
        throw;
    }
#endif
}
std::shared_ptr<std::function<void()>> registerResourceStop(std::function<void()> stop) {
    auto registration = std::make_shared<std::function<void()>>(std::move(stop));
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    std::erase_if(owner.stops, [](const auto& entry) { return entry.expired(); });
    owner.stops.push_back(registration);
    return registration;
}
void cancelResourceBackends() {
    std::vector<std::shared_ptr<std::function<void()>>> stops;
    {
        auto& owner = host();
        std::lock_guard<std::mutex> lock(owner.mutex);
        for (auto& entry : owner.stops) if (auto stop = entry.lock()) stops.push_back(stop);
    }
    for (auto& stop : stops) (*stop)();
}
void finishResourceLoadsAtExit() {
    if (auto token = appRunToken()) token->store(false);
    cancelResourceBackends();
    auto& owner = host();
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        // std::exit may itself be called on a worker. Keep its handle without
        // joining it; the reaper joins every other worker before returning.
        for (auto it = owner.workers.begin(); it != owner.workers.end(); ++it) {
            if ((*it)->thread.get_id() == std::this_thread::get_id()) {
                owner.kept.release((*it)->thread);
                owner.workers.erase(it);
                break;
            }
        }
        owner.exiting = true;
        owner.changed.notify_all();
    }
    owner.kept.release(owner.reaper);
#endif
    // Destroy queued CPU payloads before app/GPU/logger teardown, without
    // executing application callbacks from exit.
    if (isMainThread()) {
        auto discarded = takeResourceLoadBatch();
    }
}
void setResourceLoadProbeForTests(ResourceLoadProbe probe) { host().probe = std::move(probe); }
void probeResourceLoad(const char* stage, const std::filesystem::path& path) {
    if (host().probe) host().probe(stage, path);
}
void waitForResourceLoadsForTests() {
#ifndef __EMSCRIPTEN__
    auto& owner = host();
    std::unique_lock<std::mutex> lock(owner.mutex);
    owner.changed.wait(lock, [&] { return owner.workers.empty(); });
#endif
}
void waitForResourceLoadsAtAppExit() {
    cancelResourceBackends();
    waitForResourceLoadsForTests();
    // Cache hits can retain a committed GPU atlas. Release invalidated queue
    // entries here, on main and before the old GPU context is torn down.
    auto discarded = takeResourceLoadBatch();
}
size_t resourceLoadWorkersForTests() {
#ifndef __EMSCRIPTEN__
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    return owner.workers.size();
#else
    return 0;
#endif
}
} // namespace trussc::internal
