#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <exception>
#include <utility>

namespace trussc {
class App;
namespace internal {
class WindowRequestAccess;
// Attribution only. C1 owns candidate validation, attachment and lifetime.
// Storage and scope operations are host compiled so guests share the stack.
struct AppConstructionTicket {
    std::shared_ptr<std::atomic<bool>> runToken;
    const void* registrationOwner = nullptr;
    uint64_t epoch = 0;
    App* app = nullptr;
};
class AppConstructionScope {
public:
    explicit AppConstructionScope(AppConstructionTicket&);
    ~AppConstructionScope() noexcept;
    AppConstructionScope(const AppConstructionScope&) = delete;
private:
    AppConstructionTicket& ticket_;
    AppConstructionTicket* previous_;
    const void* previousOwner_;
    int exceptions_;
};
AppConstructionTicket* currentAppConstruction() noexcept;
void completeAppConstruction(App&) noexcept;

// Publish only the requested, fully constructed candidate. Incidental Apps
// cannot claim a ticket; scope token/epoch/owner remain available throughout
// construction. This also handles virtual bases without pre-lifetime upcasts.
template<class T, class... Args> std::shared_ptr<T> makeConstructedApp(Args&&... args) {
    auto candidate = std::make_shared<T>(std::forward<Args>(args)...);
    completeAppConstruction(*candidate);
    return candidate;
}
std::shared_ptr<std::atomic<bool>> constructorRunToken();
uint64_t runtimeEpoch() noexcept;
void beginRuntimeRun();
}
}
