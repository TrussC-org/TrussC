#include <TrussC.h>
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
    std::atomic<MainThreadSlot*> completions{nullptr};
    struct Job { std::shared_ptr<ResourceLoadState> state; std::function<void()> work; };
    std::shared_ptr<ResourceLoadState> pendingHead, pendingTail;
    size_t pendingCount = 0;
    // Shared preparation slots, configurable on main. Queued work waits for a slot.
    size_t activeLimit = 4, active = 0;
    // Bound (security): URL/network-buffer loads retain paths and preparation state. Overflow rejects whole new requests and is counted. Local files wait in a growable queue; app CPU jobs are unaffected.
    size_t pendingLimit = 64;
    // Bound (security): tiny URL/network-buffer results must not accumulate unlimited completion bookkeeping. Active + pending request credits stay held through application/cancellation; overflow rejects whole new URL/network-buffer loads and is counted. Local files wait without a request cap.
    size_t outstanding = 0;
    size_t requestLimit = 68; // Admission budget is independent of public concurrency changes.
    // Bound (security): decoded URL/network-buffer results waiting for main must not retain unlimited memory. Overflow rejects the whole result and is counted, without dropping its failure notification. Local files keep their results and log one Notice per run on overflow.
    size_t byteLimit = size_t(1024) * 1024 * 1024, retained = 0;
    uint64_t rejected = 0;
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
#ifndef __EMSCRIPTEN__
    struct Worker { std::thread thread; bool done = false; };
    std::list<std::shared_ptr<Worker>> workers;
    std::condition_variable changed;
    std::thread reaper;
    bool exiting = false;

    LoadHost() {
        reaper = std::thread([this] {
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
                    worker->thread.join();
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
    }
    struct Retirement {
        ResourceLoadState& state;
        ~Retirement() {
            state.cancel();
        }
    } retirement{state};
    if (alive) {
        AppOwnerScope appScope(state.appOwner.lock());
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
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            if (!owner.pendingHead || owner.active >= owner.activeLimit) return;
            job = takePending(owner);
            ++owner.active;
#ifdef __EMSCRIPTEN__
            job.state->externalActive = true;
#endif
        }
#ifdef __EMSCRIPTEN__
        // VFS preparation waits for a later main-thread resource pass.
        {
            std::lock_guard<std::mutex> lock(owner.mutex);
            job.state->preparing = true;
            job.state->preparation = std::move(job.work);
            owner.mailbox.results.push_back(job.state);
        }
#else
        startResourceLoad([job = std::move(job), &owner]() mutable {
            for (;;) {
                runExternalJob(job);
                job = {};
                std::lock_guard<std::mutex> lock(owner.mutex);
                if (!owner.pendingHead || owner.active > owner.activeLimit) {
                    --owner.active;
                    owner.changed.notify_all();
                    return;
                }
                job = takePending(owner);
            }
        });
#endif
    }
}
}

bool prepareResourceLoad(ResourceLoadState& state, const std::function<void()>& preparation) {
    try {
#ifdef __APPLE__
        resourceLoadAutoreleasePool(preparation);
#else
        preparation();
#endif
        return true;
    } catch (const std::bad_alloc&) {
        logError("ResourceLoad") << "Out of memory while preparing a resource";
        state.result = LoadResult::fail(LoadError::Unknown, "Out of memory while preparing a resource");
    } catch (const std::exception& error) {
        logError("ResourceLoad") << error.what();
        state.result = LoadResult::fail(LoadError::Unknown, error.what());
    }
    std::function<void()> backend;
    {
        std::lock_guard<std::mutex> lock(host().mutex);
        state.backendPending = false;
        backend = std::move(state.cancelBackend);
        state.discardPayload();
    }
    if (backend) backend();
    return false;
}
ResourceLoadBatch::~ResourceLoadBatch() {
    for (auto& state : results) if (state) state->cancel();
}
ResourceLoadBatch& ResourceLoadBatch::operator=(ResourceLoadBatch&& other) {
    if (this != &other) {
        for (auto& state : results) if (state) state->cancel();
        callbacks = std::move(other.callbacks);
        results = std::move(other.results);
        warnings = std::move(other.warnings);
    }
    return *this;
}
ResourceLoadState::~ResourceLoadState() {
    if (frameworkShutdown()) return;
    if (!credits && !requestCredit) return; // setup may have failed before the host existed
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    releaseCredits(owner, *this);
}
void ResourceLoadState::cancel() {
    if (frameworkShutdown()) return;
    cancelled.store(true);
    auto& owner = host();
    bool releasedActive = false;
    std::function<void()> backend;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        backend = std::move(cancelBackend);
        if (completed) discardPayload();
        releaseCredits(owner, *this);
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
                }
                if (!owner.localNoticeIssued) {
                    owner.localNoticeIssued = true;
                    owner.mailbox.callbacks.push_back([run = state->run] {
                        if (appRunIsAlive(run))
                            logNotice("ResourceLoad") << "Local-file retained results exceeded the resource-load byte budget; "
                                << "results are kept and further local overages are silent for this run";
                    });
                }
            }
        }
        if (!state->cancelled.load()) owner.mailbox.results.push_back(state);
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
        const size_t requestLimit = owner.requestLimit;
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
    owner.requestLimit = pending > SIZE_MAX - active ? SIZE_MAX : pending + active;
}

void enqueueResourceLoad(std::function<void()> completion) {
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    owner.mailbox.callbacks.push_back(std::move(completion));
}
ResourceLoadBatch takeResourceLoadBatch() {
    auto& owner = host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    ResourceLoadBatch batch;
    batch.callbacks.swap(owner.mailbox.callbacks);
    batch.results.swap(owner.mailbox.results);
    batch.warnings = MainThreadSlotBatch(owner.completions);
    return batch;
}
void deliverResourceLoadBatch(ResourceLoadBatch batch) {
    for (auto& slot : batch.callbacks) {
        auto fn = std::move(slot);
        if (fn) fn();
    }
    for (auto& slot : batch.results) {
        auto state = std::move(slot);
#ifdef __EMSCRIPTEN__
        if (state->preparing) {
            state->preparing = false;
            LoadHost::Job job{state, std::move(state->preparation)};
            if (!state->cancelled.load()) runExternalJob(job);
            continue;
        }
#endif
        deliver(*state);
    }
    batch.warnings.deliver();
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
        worker->thread = std::thread([worker, preparation = std::move(preparation), &owner]() mutable {
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
    auto& owner = host();
    std::vector<std::weak_ptr<std::function<void()>>> stops;
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        stops = owner.stops;
    }
    for (auto& entry : stops) {
        if (auto stop = entry.lock()) (*stop)();
    }
}
void finishResourceLoadsAtExit() {
    if (auto token = appRunToken()) token->store(false);
    cancelResourceBackends();
    auto& owner = host();
#ifndef __EMSCRIPTEN__
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        owner.exiting = true;
        owner.changed.notify_all();
    }
    if (owner.reaper.joinable()) owner.reaper.join();
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

namespace trussc {
void setResourceLoadConcurrency(int count) {
    if (count < 1) {
        logNotice("ResourceLoad") << "Resource load concurrency below 1; using 1";
        count = 1;
    }
    auto& owner = internal::host();
    {
        std::lock_guard<std::mutex> lock(owner.mutex);
        owner.activeLimit = static_cast<size_t>(count);
    }
    internal::pumpExternalLoads();
}
int getResourceLoadConcurrency() {
    auto& owner = internal::host();
    std::lock_guard<std::mutex> lock(owner.mutex);
    return static_cast<int>(owner.activeLimit);
}
} // namespace trussc
