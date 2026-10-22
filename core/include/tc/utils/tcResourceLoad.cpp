#include <TrussC.h>
#include "tcKeptThreads.h"
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
struct LoadHost;
LoadHost& host();
struct LoadHost {
    std::mutex mutex;
    ResourceLoadBatch mailbox;
    ResourceLoadProbe probe;
    std::vector<std::weak_ptr<std::function<void()>>> stops;
    size_t stopDrains = 0;
    std::atomic<MainThreadSlot*> completions{nullptr};
    struct Job { std::shared_ptr<ResourceLoadState> state; std::function<void()> work; };
    std::shared_ptr<ResourceLoadState> pendingHead, pendingTail;
    size_t pendingCount = 0;
    // Bound (security): file/network preparations must not multiply decoder workers indefinitely. Local overflow waits; URL/network-buffer overflow waits in the bounded pending queue, then rejects whole requests and is counted.
    size_t activeLimit = 4, active = 0;
    // Bound (security): URL/network-buffer loads retain paths and preparation state. Overflow rejects whole new requests and is counted. Local files wait in a growable queue; app CPU jobs are unaffected.
    size_t pendingLimit = 64;
    // Bound (security): tiny URL/network-buffer results must not accumulate unlimited completion bookkeeping. Active + pending request credits stay held through application/cancellation; overflow rejects whole new URL/network-buffer loads and is counted. Local files wait without a request cap.
    size_t outstanding = 0;
    // Bound (security): decoded URL/network-buffer results waiting for main must not retain unlimited memory. Overflow rejects the whole result and is counted, without dropping its failure notification. Local files keep their results and log one Notice per run on overflow.
    size_t byteLimit = size_t(1024) * 1024 * 1024, retained = 0;
    uint64_t rejected = 0;
    bool failLaunch = false;
    const char* rejectedStore = "";
    size_t rejectedLimit = 0, rejectedNeeded = 0;
    MainThreadSlot warning{[](MainThreadSlot&) {
        auto& owner = host();
        logWarning("ResourceLoad") << owner.rejectedStore << " admission rejected a whole load: capacity "
            << owner.rejectedLimit << ", needed " << owner.rejectedNeeded
            << "; further rejections are counted";
    }};
    std::shared_ptr<void> warningOwner;
    std::weak_ptr<std::atomic<bool>> localNoticeRun;
    bool localNoticeIssued = false;
    bool localNoticePending = false;
    // Posting is allocation-free. Only the current run may consume this flag;
    // shutdown omits an undelivered Notice rather than carrying it into a run.
    MainThreadSlot localNotice{[](MainThreadSlot&) {
        auto& owner = host();
        bool pending;
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            const auto& run = appRunToken();
            pending = std::exchange(owner.localNoticePending, false) &&
                !owner.localNoticeRun.owner_before(run) && !run.owner_before(owner.localNoticeRun) &&
                appRunIsAlive(owner.localNoticeRun);
        }
        if (pending)
            logNotice("ResourceLoad") << "Local-file retained results exceeded the resource-load byte budget; "
                << "results are kept and further local overages are silent for this run";
    }, [](MainThreadSlot& slot) {
        auto& owner = host();
        std::lock_guard<std::mutex> lock(owner.mutex);
        const auto& run = appRunToken();
        // An old snapshot can hold this slot across shutdown and prevent the
        // current run's post. Discard releases it before this callback runs.
        if (owner.localNoticePending &&
            !owner.localNoticeRun.owner_before(run) && !run.owner_before(owner.localNoticeRun) &&
            appRunIsAlive(owner.localNoticeRun))
            slot.post(owner.warningOwner, owner.completions);
    }};
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
        value->warningOwner = std::shared_ptr<void>(value, [](void*) {});
        std::atexit(finishResourceLoadsAtExit);
        return value;
    }();
    return *owner;
}
} // namespace

namespace {
// owner.mutex is held; process-lifetime totals never reset on query.
void reject(LoadHost& owner, const char* store, size_t limit, size_t needed) {
    if (++owner.rejected == 1) {
        owner.rejectedStore = store; owner.rejectedLimit = limit; owner.rejectedNeeded = needed;
        owner.warning.post(owner.warningOwner, owner.completions);
    }
}
void releaseCredits(LoadHost& owner, ResourceLoadState& state) {
    owner.retained -= state.credits;
    state.credits = 0;
    if (state.requestCredit) { --owner.outstanding; state.requestCredit = false; }
}
void deliver(ResourceLoadState& state) {
    // Worker publication is synchronized by the mailbox. No worker touches
    // the payload after publication, and all handle operations stay on main.
    bool alive;
    {
        std::lock_guard<std::mutex> lock(host().mutex);
        alive = !state.cancelled.load() && appRunIsAlive(state.run);
        state.delivering = alive;
    }
    struct Retirement {
        ResourceLoadState& state;
        ~Retirement() {
            { std::lock_guard<std::mutex> lock(host().mutex); state.delivering = false; }
            state.cancel();
        }
    } retirement{state};
    if (alive) {
        if (auto target = state.apply.lock()) (*target)(state);
    }
}
void pumpExternalLoads();
void runExternalJob(LoadHost::Job& job) {
    {
        auto& owner = host();
        std::lock_guard<std::mutex> lock(owner.mutex);
        if (!job.state->localFile && owner.retained >= owner.byteLimit && !job.state->cancelled.load()) {
            reject(owner, "retained result bytes", owner.byteLimit, owner.retained + 1);
            job.state->result.error = LoadError::TooLarge;
        }
    }
    if (!job.state->cancelled.load() && job.state->result)
        (void)prepareResourceLoad(*job.state, job.work);
    if (!job.state->backendPending) completeResourceLoad(job.state);
    job.work = {};
}
LoadHost::Job takePending(LoadHost& owner) {
    auto state = std::move(owner.pendingHead);
    owner.pendingHead = std::move(state->nextPending);
    if (!owner.pendingHead) owner.pendingTail.reset();
    --owner.pendingCount;
    return {state, std::move(state->preparation)};
}
void pumpExternalLoads() {
    auto& owner = host();
    for (;;) {
        LoadHost::Job job;
#ifdef __EMSCRIPTEN__
        bool failLaunch;
#endif
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (!owner.pendingHead || owner.active >= owner.activeLimit) return;
            job = takePending(owner);
            ++owner.active;
#ifdef __EMSCRIPTEN__
            job.state->externalActive = true;
            failLaunch = owner.failLaunch;
#endif
        }
        auto failedState = job.state;
        auto fail = [&] {
            {
                std::lock_guard<std::mutex> lock(owner.mutex);
                --owner.active;
                failedState->externalActive = false;
                if (!failedState->localFile)
                    reject(owner, "preparation workers", owner.activeLimit, owner.active + 1);
            }
            failedState->result.error = LoadError::Unknown;
            completeResourceLoad(failedState);
        };
#ifdef __EMSCRIPTEN__
        if (failLaunch) { fail(); continue; }
        // VFS and browser preparations use the existing preallocated slot,
        // independently snapshotted with resource results. No queue allocation
        // or exception support is needed to schedule or report failure.
        job.state->preparing = true;
        job.state->preparation = std::move(job.work);
        job.state->post(job.state, owner.completions);
#else
        try {
            startResourceLoad([job = std::move(job), &owner]() mutable {
                for (;;) {
                    runExternalJob(job);
                    job = {};
                    std::lock_guard<std::mutex> lock(owner.mutex);
                    if (!owner.pendingHead) {
                        --owner.active;
                        owner.changed.notify_all();
                        return;
                    }
                    job = takePending(owner);
                }
            });
        } catch (...) { fail(); }
#endif
    }
}
}

bool prepareResourceLoad(ResourceLoadState& state, const std::function<void()>& preparation) {
    std::function<void()> backend;
    try {
#ifdef __APPLE__
        resourceLoadAutoreleasePool(preparation);
#else
        preparation();
#endif
        return true;
    } catch (...) {
        std::lock_guard<std::mutex> lock(host().mutex);
        if (!state.completed || (state.delivering && isMainThread())) {
            state.backendPending = false;
            state.result.error = LoadError::Unknown;
            state.result.message.clear();
            backend = std::move(state.cancelBackend);
            state.discardPayload();
        }
    }
    if (backend) backend();
    return false;
}

ResourceLoadState::ResourceLoadState() : MainThreadSlot([](MainThreadSlot& slot) {
    auto& state = static_cast<ResourceLoadState&>(slot);
#ifdef __EMSCRIPTEN__
    if (state.preparing) {
        state.preparing = false;
        LoadHost::Job job{std::static_pointer_cast<ResourceLoadState>(state.shared_from_this()),
                          std::move(state.preparation)};
        runExternalJob(job);
        return;
    }
#endif
    deliver(state);
}, [](MainThreadSlot& slot) { static_cast<ResourceLoadState&>(slot).cancel(); }) {}
ResourceLoadState::~ResourceLoadState() {
    if (!credits && !requestCredit) return; // setup may have failed before the host existed
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    releaseCredits(owner, *this);
}
void ResourceLoadState::cancel() {
    cancelled.store(true);
    auto& owner = host();
    bool releasedActive = false;
    std::function<void()> backend;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        backend = std::move(cancelBackend);
        // std::exit can cancel from a worker while main is applying a result.
        // Main owns that payload until its delivery guard retires it.
        if (!delivering || isMainThread()) {
            if (completed) discardPayload();
            releaseCredits(owner, *this);
        }
#ifdef __EMSCRIPTEN__
        if (preparing) {
            preparing = false;
            preparation = {};
            completed = true;
            if (externalActive) { externalActive = false; --owner.active; releasedActive = true; }
        }
#endif
        // Remove queued preparations too: cancellation must not leave paths and
        // captured input owned until another frame/decoder happens to run.
        auto* link = &owner.pendingHead;
        std::shared_ptr<ResourceLoadState> previous;
        while (*link) {
            if (link->get() == this) {
                auto removed = std::move(*link);
                *link = std::move(removed->nextPending);
                if (owner.pendingTail.get() == this) owner.pendingTail = previous;
                removed->preparation = {};
                --owner.pendingCount;
                break;
            }
            previous = *link;
            link = &(*link)->nextPending;
        }
    }
    if (backend) backend(); // callbacks can complete/release an active slot
    if (releasedActive) pumpExternalLoads();
}
void completeResourceLoad(const std::shared_ptr<ResourceLoadState>& state) {
    auto& owner = host();
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        if (state->completed) return;
        state->completed = true;
        const size_t bytes = state->retainedBytes();
        const bool overBudget = owner.retained > owner.byteLimit ||
            bytes > owner.byteLimit - std::min(owner.retained, owner.byteLimit);
        if (state->cancelled.load() || !state->result || (!state->localFile && overBudget)) {
            if (!state->cancelled.load() && state->result && !state->localFile && overBudget) {
                reject(owner, "retained result bytes", owner.byteLimit,
                       bytes > SIZE_MAX - owner.retained ? SIZE_MAX : bytes + owner.retained);
                state->result.error = LoadError::TooLarge;
                state->result.message.clear();
            }
            state->discardPayload();
            releaseCredits(owner, *state);
        } else {
            owner.retained += bytes;
            state->credits = bytes;
            if (state->localFile && overBudget) {
                if (owner.localNoticeRun.owner_before(state->run) || state->run.owner_before(owner.localNoticeRun)) {
                    owner.localNoticeRun = state->run;
                    owner.localNoticeIssued = false;
                    owner.localNoticePending = false;
                }
                if (!owner.localNoticeIssued) {
                    owner.localNoticeIssued = true;
                    owner.localNoticePending = true;
                    owner.localNotice.post(owner.warningOwner, owner.completions);
                }
            }
        }
        if (!state->cancelled.load()) state->post(state, owner.completions);
#ifdef __EMSCRIPTEN__
        if (state->externalActive) {
            state->backendPending = false;
            state->externalActive = false;
            --owner.active;
        }
#endif
    }
#ifdef __EMSCRIPTEN__
    pumpExternalLoads();
#endif
}
bool resourceLoadIsLocalPath(const std::filesystem::path& path,
                             const std::filesystem::path& resolvedPath) {
    // Anything passed as a file path is local, including UNC/network-share
    // paths; URLs and buffers received from the network are network/external.
    // Only http:// and https:// are fetched by the current network loader.
    // Match its prefixes using native characters (also wchar_t on Windows),
    // without allocating or inspecting the filesystem/share. Unsupported URI
    // forms and font names follow normal file resolution and error handling.
    auto local = [](const std::filesystem::path& value) {
        const auto& input = value.native();
        auto startsWith = [&](const char* prefix) {
            for (size_t i = 0; prefix[i]; ++i)
                if (i >= input.size() || input[i] != prefix[i]) return false;
            return true;
        };
        return !startsWith("http://") && !startsWith("https://");
    };
    return local(path) && local(resolvedPath);
}
void startExternalResourceLoad(std::shared_ptr<ResourceLoadState> state, std::function<void()> preparation) {
    auto& owner = host();
    bool accepted = false;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        const size_t requestLimit = owner.pendingLimit > SIZE_MAX - owner.activeLimit
            ? SIZE_MAX : owner.pendingLimit + owner.activeLimit;
        if (state->localFile || (owner.outstanding < requestLimit && owner.pendingCount < owner.pendingLimit && owner.retained < owner.byteLimit)) {
            state->preparation = std::move(preparation);
            if (owner.pendingTail) owner.pendingTail->nextPending = state;
            else owner.pendingHead = state;
            owner.pendingTail = state;
            ++owner.pendingCount;
            ++owner.outstanding;
            state->requestCredit = true;
            accepted = true;
        }
        if (!accepted) {
            if (owner.retained >= owner.byteLimit)
                reject(owner, "retained result bytes", owner.byteLimit, owner.retained + 1);
            else if (owner.outstanding >= requestLimit)
                reject(owner, "live external load requests", requestLimit, owner.outstanding + 1);
            else reject(owner, "pending load requests", owner.pendingLimit, owner.pendingCount + 1);
        }
    }
    if (!accepted) {
        state->result.error = LoadError::TooLarge;
        completeResourceLoad(state);
        return;
    }
    pumpExternalLoads();
}
ResourceLoadUsage resourceLoadUsageForTests() {
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    return {owner.active, owner.pendingCount, owner.retained, owner.rejected, owner.outstanding};
}
void setResourceLoadLimitsForTests(size_t active, size_t pending, size_t bytes) {
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    owner.activeLimit = active; owner.pendingLimit = pending; owner.byteLimit = bytes;
}
void failResourceLoadLaunchForTests(bool fail) { host().failLaunch = fail; }

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
    batch.results = MainThreadSlotBatch(owner.completions);
    return batch;
}
void deliverResourceLoadBatch(ResourceLoadBatch batch) {
    for (auto& slot : batch) {
        auto fn = std::move(slot);
        if (fn) fn();
    }
    batch.results.deliver();
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
        if (owner.failLaunch) throw std::bad_alloc();
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
    // A shutdown pass indexes this registry without copying it. New entries
    // may append, but existing indices stay stable until every pass returns.
    if (!owner.stopDrains)
        std::erase_if(owner.stops, [](const auto& entry) { return entry.expired(); });
    owner.stops.push_back(registration);
    return registration;
}
void cancelResourceBackends() {
    auto& owner = host();
    size_t count;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        ++owner.stopDrains;
        count = owner.stops.size();
    }
    struct Drain {
        LoadHost& owner;
        ~Drain() { std::lock_guard<std::mutex> lock(owner.mutex); --owner.stopDrains; }
    } drain{owner};
    // Keep persistent registrations (for example a window reused after a
    // run change). Acquiring one weak target at a time never allocates.
    for (size_t i = 0; i < count; ++i) {
        std::shared_ptr<std::function<void()>> stop;
        { std::lock_guard<std::mutex> lock(owner.mutex); stop = owner.stops[i].lock(); }
        if (stop) (*stop)();
    }
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
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        owner.localNoticePending = false;
    }
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
    {
        auto& owner = host();
        std::lock_guard<std::mutex> lock(owner.mutex);
        owner.localNoticePending = false;
    }
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
