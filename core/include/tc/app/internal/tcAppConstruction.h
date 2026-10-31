#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <exception>
#include <stdexcept>
#include <functional>
#include <utility>
#include <vector>
#include <type_traits>
#include "../../utils/tcAtomicSharedPtr.h"
#include "../tcExit.h"

namespace trussc {
class App;
class Window;
namespace internal {
class WindowRequestAccess;
struct WindowContext;
// Host control blocks retain the defining library until erased storage has
// finished its guest destructor. Physical unloading runs only at a host boundary.
std::shared_ptr<void> retainGuestCode();
std::shared_ptr<void> retainGuestCodeFor(const void*);
std::shared_ptr<void> combineGuestCode(std::shared_ptr<void>, std::shared_ptr<void>);
const void* guestCodeGeneration(const std::shared_ptr<void>&);
std::shared_ptr<void> adoptGuestStorage(void*, void (*)(void*), std::shared_ptr<void>);
void registerGuestCode(const void*, void*, void (*)(void*), const void*);
void releaseGuestCode(const void*) noexcept;
void drainGuestCode() noexcept;
struct ResourceLoadState;
struct ExternalDeliveryStore;
std::shared_ptr<ResourceLoadState> adoptGuestResourceStorage(ResourceLoadState*, void*, void (*)(void*), std::shared_ptr<void>);
std::shared_ptr<ExternalDeliveryStore> adoptGuestEventStorage(ExternalDeliveryStore*, void*, void (*)(void*), std::shared_ptr<void>);
template<class T, class... Args> std::shared_ptr<T> makeCodeShared(Args&&... args) {
    auto destroy = +[](void* p) { delete static_cast<T*>(p); };
    auto code = retainGuestCodeFor(reinterpret_cast<const void*>(destroy));
    auto* value = new T(std::forward<Args>(args)...);
    // All control blocks and deleter managers live in the host. In particular,
    // dropping the last weak reference on a worker never returns through an
    // unloaded guest control-block destructor. Known framework bases initialize
    // enable_shared_from_this in the host's typed adoption constructor.
    if constexpr (std::is_base_of_v<ResourceLoadState, T>) {
        auto storage = adoptGuestResourceStorage(value, value, destroy, std::move(code));
        return std::shared_ptr<T>(std::move(storage), value);
    } else if constexpr (std::is_base_of_v<ExternalDeliveryStore, T>) {
        auto storage = adoptGuestEventStorage(value, value, destroy, std::move(code));
        return std::shared_ptr<T>(std::move(storage), value);
    } else {
        static_assert(!requires(T& v) { v.shared_from_this(); }, "shared ownership requires a host adoption overload");
        auto storage = adoptGuestStorage(value, destroy, std::move(code));
        return std::shared_ptr<T>(std::move(storage), value);
    }
}
std::function<void()> eventDisconnect(void*, uint64_t, std::weak_ptr<std::atomic<bool>>,
                                    void (*)(void*, uint64_t));
// One host-owned lifetime for construction, pending publication and attachment.
struct AppOwner {
    std::shared_ptr<std::atomic<bool>> token = std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<void> code;
    uint64_t epoch = 0;
    const void* generation = nullptr;
    App* app = nullptr;
    Window* window = nullptr;
    WindowContext* context = nullptr;
    std::atomic<bool> retiring{false}, ended{false}, callbacksClosing{false};
    bool exitDelivered = false, notifyWindowExit = false;
    std::atomic<bool> abandoned{false};
    // Serializes registration/extraction; invoke cancellations outside this lock.
    std::mutex cancellationsMutex;
    std::vector<std::function<void()>> cancellations;
};
struct AppConstructionTicket {
    std::shared_ptr<std::atomic<bool>> runToken;
    const void* registrationOwner = nullptr;
    uint64_t epoch = 0;
    App* app = nullptr;
    WindowContext* context = nullptr;
    std::shared_ptr<AppOwner> owner;
    std::shared_ptr<void> code;
    bool claimed = false;
    const void* allocation = nullptr;
    size_t allocationSize = 0;
};
std::shared_ptr<AppOwner> currentAppOwner() noexcept;
bool isAppExitEvent(const void*) noexcept;
void retireAppGeneration(const void*) noexcept;
const void* nextAppGeneration();
void retireAppOwner(const std::shared_ptr<AppOwner>&) noexcept;
void ownAppCancellation(std::function<void()>);
// Restores nested ownership without changing the caller's rendering lease.
class AppOwnerScope {
public:
    explicit AppOwnerScope(std::shared_ptr<AppOwner>) noexcept;
    ~AppOwnerScope() noexcept;
    AppOwnerScope(const AppOwnerScope&) = delete;
private:
    friend std::shared_ptr<AppOwner> currentAppOwner() noexcept;
    AppOwnerScope* previous_;
    std::shared_ptr<AppOwner> owner_;
    const void* previousRegistration_;
};
// Main-thread callbacks restore the caller's physical rendering context too.
class AppCallbackScope {
public:
    explicit AppCallbackScope(std::shared_ptr<AppOwner>);
    ~AppCallbackScope() noexcept;
    AppCallbackScope(const AppCallbackScope&) = delete;
private:
    AppOwnerScope owner_;
    WindowContext* previous_ = nullptr;
    void* display_ = nullptr;
    void* context_ = nullptr;
    uintptr_t draw_ = 0, read_ = 0;
    uint32_t drawWindow_ = 0, readWindow_ = 0, sgl_ = 0;
    bool entered_ = false, workerEntry_ = false, mainEntry_ = false;
};
class AppConstructionScope {
public:
    explicit AppConstructionScope(AppConstructionTicket&);
    ~AppConstructionScope() noexcept;
    AppConstructionScope(const AppConstructionScope&) = delete;
private:
    AppConstructionTicket& ticket_;
    AppConstructionTicket* previous_;
    std::unique_ptr<AppOwnerScope> ownerScope_;
};
AppConstructionTicket* currentAppConstruction() noexcept;
void requireAppConstruction(const App*);
void completeAppConstruction(App&) noexcept;
std::shared_ptr<App> adoptConstructedApp(App*, void (*destroy)(App*) = nullptr);
std::shared_ptr<AppOwner> appOwner(const App&) noexcept;
bool admitAppEndingEvent(const AppOwner*) noexcept;
void deliverAppExit(App&);
void endAppAttachment(const std::shared_ptr<App>&, WindowContext&, bool virtualOnly = false, EndingPolicy policy = EndingPolicy::Normal) noexcept;

// Retain attribution for callbacks held outside Event (tools, dialogs, providers).
template<class R, class... Args>
std::function<R(Args...)> appOwnedCallback(std::function<R(Args...)> callback, bool exitEvent = false) {
    auto owner = currentAppOwner();
    if (!owner || !callback) return callback;
    if (owner->ended || owner->retiring) return {};
    using Callback = std::function<R(Args...)>;
    auto target = makeCodeShared<AtomicSharedPtr<Callback>>(makeCodeShared<Callback>(std::move(callback)));
    ownAppCancellation([weak = std::weak_ptr(target)] {
        if (auto live = weak.lock()) sharedStore(*live, std::shared_ptr<Callback>{});
    });
    return [weak = std::weak_ptr(owner), target, exitEvent](Args... args) -> R {
        if (sessionEndExit() && !(exitEvent && admitAppEndingEvent(weak.lock().get()))) {
            if constexpr (!std::is_void_v<R>) return R{};
            else return;
        }
        auto live = weak.lock();
        auto call = sharedLoad(*target);
        if (!live || (!live->token->load() && !(exitEvent && live->ended)) || live->callbacksClosing || !call) {
            if constexpr (!std::is_void_v<R>) return R{};
            else return;
        }
        AppCallbackScope scope(live);
        // Pair callback activity admission with Runtime's freeze-before-count
        // snapshot. A worker that lost the race never enters App code.
        if (sessionEndExit() && !(exitEvent && admitAppEndingEvent(weak.lock().get()))) {
            if constexpr (!std::is_void_v<R>) return R{};
            else return;
        }
        return (*call)(std::forward<Args>(args)...);
    };
}

// Raw guest and typed factories share allocation admission, including virtual bases.
// Class allocation overrides are bypassed; storage is paired with this deleter.
template<class T> void destroyAppStorage(App* app) {
    auto* value = dynamic_cast<T*>(app);
    std::destroy_at(value);
    std::allocator<T>{}.deallocate(value, 1);
}
template<class T, class... Args> T* constructAppStorage(Args&&... args) {
    auto* ticket = currentAppConstruction();
    if (!ticket || ticket->allocation) throw std::logic_error("App storage requires a fresh factory ticket");
    auto* storage = std::allocator<T>{}.allocate(1);
    ticket->allocation = storage; ticket->allocationSize = sizeof(T);
    try { return std::construct_at(storage, std::forward<Args>(args)...); }
    catch (...) { std::allocator<T>{}.deallocate(storage, 1); throw; }
}

// Internal only: the scope encloses allocation and the entire derived constructor.
template<class T, class... Args> std::shared_ptr<T> makeConstructedAppAt(WindowContext* context, Args&&... args) {
    AppConstructionTicket ticket;
    ticket.context = context;
    auto definingCode = &destroyAppStorage<T>;
    ticket.code = retainGuestCodeFor(reinterpret_cast<const void*>(definingCode));
    AppConstructionScope construction(ticket);
    static_assert(std::is_base_of_v<App, T>, "T must derive from App");
    auto* candidate = constructAppStorage<T>(std::forward<Args>(args)...);
    completeAppConstruction(*candidate);
    auto owner = adoptConstructedApp(candidate, definingCode);
    return std::shared_ptr<T>(std::move(owner), candidate);
}
template<class T, class... Args> std::shared_ptr<T> makeConstructedApp(Args&&... args) {
    return makeConstructedAppAt<T>(nullptr, std::forward<Args>(args)...);
}
std::shared_ptr<std::atomic<bool>> constructorRunToken();
uint64_t runtimeEpoch() noexcept;
void beginRuntimeRun();
}
}
