#include <TrussC.h>
#include <dlfcn.h>
#include <cstdlib>
#include <cstdio>
#include <new>
#include <thread>
using namespace tc;
extern "C" {
Event<void>* storageVoid = nullptr;
Event<int>* storageTyped = nullptr;
int storageCalls = 0, storageDrops = 0, storageExits = 0, storageCleanups = 0;
unsigned storageEndingOrder[3]{};
}
namespace {
bool failAllocation = false;
int failures = 0, unloads = 0;
void check(const char* label, bool ok) {
    std::printf("guest-storage: %s: %s\n", label, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
void closeGuest(void* handle) { ++unloads; dlclose(handle); }
}
void* operator new(std::size_t size) {
    if (failAllocation) throw std::bad_alloc();
    if (void* p = std::malloc(size ? size : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    getMainThreadId();
    for (bool runtimeStop : {false, true}) for (bool oom : {false, true}) {
        internal::beginRuntimeRun();
        internal::beginFrameworkLifetime();
        auto window = internal::prepareLaunchWindow(WindowSettings{});
        internal::WindowRequestAccess::bindForTests(*window, 101);
        Window peer; // retained unbound CPU storage also participates in retirement
        auto peerNode = std::make_shared<RectNode>();
        peer.context().selectedNode = peerNode;
        Event<void> zero;
        Event<int> typed;
        storageVoid = &zero; storageTyped = &typed;
        storageCalls = storageDrops = storageExits = storageCleanups = 0;
        for (auto& id : storageEndingOrder) id = 0;
        void* handle = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
        check("real guest loads", handle != nullptr);
        if (!handle) { std::puts(dlerror()); return 2; }
        auto create = reinterpret_cast<App* (*)()>(dlsym(handle, "storageCreate"));
        auto destroy = reinterpret_cast<void (*)(App*)>(dlsym(handle, "storageDestroy"));
        const auto generation = internal::nextAppGeneration();
        internal::registerGuestCode(generation, handle, closeGuest, reinterpret_cast<const void*>(create));
        auto createAt = [&](Window& target, internal::AppConstructionTicket& ticket) {
            ticket.registrationOwner = generation;
            ticket.context = &target.context();
            std::shared_ptr<App> app;
            {
                internal::AppConstructionScope scope(ticket);
                auto* raw = create(); internal::completeAppConstruction(*raw);
                app = internal::adoptConstructedApp(raw, destroy);
            }
            internal::WindowRequestAccess::attachCandidate(target, app);
            internal::applyPendingAppForTests(target);
            return app;
        };
        internal::AppConstructionTicket ticket, siblingTicket;
        auto app = createAt(*window, ticket);
        auto siblingWindow = internal::prepareLaunchWindow(WindowSettings{});
        internal::WindowRequestAccess::bindForTests(*siblingWindow, 202);
        auto sibling = createAt(*siblingWindow, siblingTicket);
        auto youngestWindow = internal::prepareLaunchWindow(WindowSettings{});
        internal::WindowRequestAccess::bindForTests(*youngestWindow, 303);
        auto attach = reinterpret_cast<void (*)(Window*)>(dlsym(handle, "storageAttach"));
        attach(youngestWindow.get()); // guest typed construction outside any App owner
        internal::applyPendingAppForTests(*youngestWindow);
        auto youngest = youngestWindow->getApp();
        check("unowned guest typed construction publishes its own cohort App", bool(youngest));
        if (!youngest) return 2;
        auto addNode = reinterpret_cast<std::shared_ptr<Node> (*)(App*)>(dlsym(handle, "storageNode"));
        { auto node = addNode(app.get()); peer.context().hoveredNode = node; peer.context().grabbedNode = node; peer.context().grabbedButton = 0; }
        auto makeWeak = reinterpret_cast<std::weak_ptr<void> (*)()>(dlsym(handle, "storageWeak"));
        auto weak = makeWeak();
        auto makeWorker = reinterpret_cast<mcp::internal::ReplyThunk (*)()>(dlsym(handle, "storageWorker"));
        mcp::internal::ReplyThunk reply;
        { internal::AppOwnerScope scope(ticket.owner); reply = makeWorker(); }
        check("guest enable_shared_from_this initializes and its object expires", weak.expired());
        check("live retained worker runs under its owner", reply() == "retained guest worker");
        // Retain guest-created queued callback storage independently of the
        // COW snapshots, then retire while every allocation is rejected.
        std::thread worker([&] { int value = 1; zero.notify(); typed.notify(value); });
        worker.join();
        auto queueGuest = reinterpret_cast<void (*)()>(dlsym(handle, "storageQueue"));
        std::thread ownedWorker([&] { internal::AppOwnerScope scope(ticket.owner); queueGuest(); });
        ownedWorker.join();
        const int before = unloads;
        std::printf("guest-storage: mode=%s oom=%d\n", runtimeStop ? "Runtime shutdown" : "cohort retirement", int(oom));
        failAllocation = oom;
        if (runtimeStop) internal::shutdownRuntime();
        else internal::retireAppGeneration(generation);
        internal::releaseGuestCode(generation);
        failAllocation = false;
        check("OOM ending clears foreign guest node refs while preserving unrelated live nodes",
              peer.context().hoveredNode.expired() && peer.context().grabbedNode.expired() &&
              peer.context().grabbedButton == -1 && peer.context().selectedNode.lock() == peerNode);
        check("reentrant cohort retirement ends all Apps once in reverse attachment order",
              storageExits == 3 && storageCleanups == 3 && storageEndingOrder[0] == 303 &&
              storageEndingOrder[1] == 202 && storageEndingOrder[2] == 101);
        check("ending detaches every published App", !window->getApp() && !siblingWindow->getApp() && !youngestWindow->getApp());
        check("only Runtime shutdown retires the Window backings", window->isOpen() == !runtimeStop &&
              siblingWindow->isOpen() == !runtimeStop && youngestWindow->isOpen() == !runtimeStop);
        check("retirement releases captures before any queue drain", storageDrops == 3 && storageCalls == 0);
        internal::drainGuestCode();
        check("retained App pins its guest destroy function", unloads == before);
        app.reset(); sibling.reset(); youngest.reset();
        ticket.owner.reset(); ticket.runToken.reset();
        siblingTicket.owner.reset(); siblingTicket.runToken.reset();
        internal::drainGuestCode();
        if (oom) check("OOM tombstones retain their defining code", unloads == before);
        int value = 0; zero.notify(); typed.notify(value);
        check("retired callbacks never run", storageCalls == 0);
        zero.clear(); typed.clear();
        internal::drainGuestCode();
        check("retained weak control block still pins guest destruction code", unloads == before);
        weak.reset(); internal::drainGuestCode();
        check("completed MCP worker storage pins guest code", unloads == before);
        auto retiredReply = json::parse(reply());
        check("retired worker refuses execution inside JSON-RPC while its storage pins code",
            retiredReply.value("jsonrpc", "") == "2.0" && retiredReply["id"] == 239 && retiredReply["result"].value("isError", false));
        reply = {}; internal::drainGuestCode();
        check("single-argument queued guest capture remains pinned after every other storage release", unloads == before && storageDrops == 3);
        internal::drainMainThreadQueue();
        check("retired queued guest never runs and capture destruction finishes while pinned", storageCalls == 0 && storageDrops == 4 && unloads == before);
        internal::drainGuestCode();
        check("final queue storage release physically unloads once", unloads == before + 1);
        // Rebuild and notify after dlclose, catching lingering guest managers.
        auto a = zero.listen([] {}); auto b = typed.listen([](int&) {});
        zero.notify(); typed.notify(value);
        internal::shutdownRuntime();
    }
    std::printf("guest-storage: %d failures\n", failures);
    return failures ? 1 : 0;
}
