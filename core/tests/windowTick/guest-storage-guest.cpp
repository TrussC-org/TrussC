#include <TrussC.h>
using namespace tc;
extern "C" {
extern Event<void>* storageVoid;
extern Event<int>* storageTyped;
extern int storageCalls, storageDrops, storageExits, storageCleanups;
extern unsigned storageEndingOrder[3];
}
namespace {
struct Capture { ~Capture() { ++storageDrops; } };
struct Guest : App {
    unsigned id = 0;
    EventListener first, second;
    Guest() {
        id = internal::WindowRequestAccess::creationId(*internal::currentAppOwner()->context->hostWindow);
        auto capture = std::make_shared<Capture>();
        first = storageVoid->listen([capture] { ++storageCalls; }, Deliver::Main);
        second = storageTyped->listen([capture](int&) { ++storageCalls; }, Deliver::Main);
    }
    void exit() override {
        const int index = storageExits++;
        if (index < 3) storageEndingOrder[index] = id;
        // Reenter the cohort while its retained snapshot still has a sibling.
        internal::retireAppGeneration(internal::appOwner(*this)->generation);
    }
    void cleanup() override { ++storageCleanups; }
};
}
extern "C" App* storageCreate() { return internal::constructAppStorage<Guest>(); }
extern "C" void storageDestroy(App* app) { internal::destroyAppStorage<Guest>(app); }

extern "C" std::weak_ptr<void> storageWeak() {
    struct State : tc::internal::ResourceLoadState {};
    auto value = tc::internal::makeCodeShared<State>();
    if (value->shared_from_this() != value) std::abort();
    return value;
}

extern "C" tc::mcp::internal::ReplyThunk storageWorker() {
    using namespace tc::mcp::internal;
    return retainDeferredCode([]() -> ReplyThunk {
        return [] { return std::string("retained guest worker"); };
    }, tc::internal::retainGuestCode(), tc::internal::currentAppOwner(), 239)();
}

extern "C" std::shared_ptr<Node> storageNode(App* app) {
    auto child = std::make_shared<RectNode>();
    app->addChild(child);
    return child;
}

extern "C" void storageAttach(Window* target) { target->setApp<Guest>(); }

extern "C" void storageQueue() {
    struct QueuedCapture : Capture {
        ~QueuedCapture() { internal::drainGuestCode(); }
    };
    auto capture = std::make_shared<QueuedCapture>();
    runOnMainThread([capture] { ++storageCalls; });
}
