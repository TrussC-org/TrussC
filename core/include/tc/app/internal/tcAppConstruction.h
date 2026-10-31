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
#include <string>
#include "../../utils/tcAtomicSharedPtr.h"
#include "../tcExit.h"

namespace trussc {
class App;
class Window;
namespace internal {
class WindowRequestAccess;
struct WindowContext;
// One host-owned lifetime for construction, pending publication and attachment.
struct AppOwner {
    std::shared_ptr<std::atomic<bool>> token = std::make_shared<std::atomic<bool>>(true);
    uint64_t epoch = 0;
    const void* generation = nullptr;
    App* app = nullptr;
    Window* window = nullptr;
    WindowContext* context = nullptr;
    std::atomic<bool> retiring{false}, ended{false}, callbacksClosing{false};
    bool exitDelivered = false, notifyWindowExit = false;
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
// Worker callbacks revalidate the ordinary owner lifetime. Cleanup remains allowed.
class WorkerAppAdmission {
public:
    explicit WorkerAppAdmission(const std::shared_ptr<AppOwner>&, bool cleanup = false) noexcept;
    ~WorkerAppAdmission() noexcept;
    explicit operator bool() const noexcept { return admitted_; }
    WorkerAppAdmission(const WorkerAppAdmission&) = delete;
private:
    bool admitted_ = false;
};
// Main-thread callbacks restore the caller's physical rendering context too.
class AppCallbackScope {
public:
    explicit AppCallbackScope(std::shared_ptr<AppOwner>);
    ~AppCallbackScope() noexcept;
    AppCallbackScope(const AppCallbackScope&) = delete;
    bool admitted() const noexcept { return bool(worker_); }
private:
    WorkerAppAdmission worker_;
    AppOwnerScope owner_;
    WindowContext* previous_ = nullptr;
    void* display_ = nullptr;
    void* context_ = nullptr;
    uintptr_t draw_ = 0, read_ = 0;
    uint32_t drawWindow_ = 0, readWindow_ = 0, sgl_ = 0;
    bool entered_ = false, mainEntry_ = false;
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
void deliverAppExit(App&);
void endAppAttachment(const std::shared_ptr<App>&, WindowContext&, bool virtualOnly = false) noexcept;

// Retain attribution for callbacks held outside Event (tools, dialogs, providers).
template<class R, class... Args>
std::function<R(Args...)> appOwnedCallback(std::function<R(Args...)> callback, bool exitEvent = false) {
    auto owner = currentAppOwner();
    if (!owner || !callback) return callback;
    if (owner->ended || owner->retiring) return {};
    using Callback = std::function<R(Args...)>;
    auto target = std::make_shared<AtomicSharedPtr<Callback>>(std::make_shared<Callback>(std::move(callback)));
    ownAppCancellation([weak = std::weak_ptr(target)] {
        if (auto live = weak.lock()) sharedStore(*live, std::shared_ptr<Callback>{});
    });
    return [weak = std::weak_ptr(owner), target, exitEvent](Args... args) -> R {
        auto live = weak.lock();
        auto call = sharedLoad(*target);
        if (!live || (!live->token->load() && !(exitEvent && live->ended)) || live->callbacksClosing || !call) {
            if constexpr (!std::is_void_v<R>) return R{};
            else return;
        }
        AppCallbackScope scope(live);
        if (!scope.admitted() ||
            (!live->token->load() && !(exitEvent && live->ended)) || live->callbacksClosing) {
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
bool processPendingQuit() noexcept;
}
}
