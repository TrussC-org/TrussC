#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <condition_variable>
#include <cstdio>
#include <thread>
using namespace tc;
namespace {
int failures = 0, checks = 0;
void check(bool okay, const char* label) {
    ++checks; failures += !okay;
    std::printf("%s %s\n", okay ? "PASS" : "FAIL", label);
}
void endOwner(const std::shared_ptr<internal::AppOwner>& owner) { internal::invalidateAppListeners(owner, false); }
void closeOwner(const std::shared_ptr<internal::AppOwner>& owner) { internal::invalidateAppListeners(owner, true); }
template<template<class> class EventClass> void contracts() {
    using Handle = decltype(std::declval<EventClass<int>&>().listen(std::function<void(int&)>()));
    {
        EventClass<int> event;
        Handle later, added;
        std::string order;
        auto first = event.listen([&](int&) {
            order += 'a'; later.disconnect();
            if (!added) added = event.listen([&](int&) { order += 'c'; });
        });
        later = event.listen([&](int&) { order += 'b'; });
        int value = 0;
        event.notify(value);
        check(order == "a", "remove skips pending listener; add waits for next notify");
        event.notify(value);
        check(order == "aac", "added listener runs on next notify in registration order");
    }
    {
        EventClass<void> event;
        std::string order;
        auto first = event.listen([&] { order += 'a'; event.clear(); }, -10);
        auto later = event.listen([&] { order += 'b'; }, 10);
        event.notify();
        check(order == "a" && event.listenerCount() == 0, "clear during void dispatch cancels later callbacks");
    }
    {
        EventClass<int> event;
        std::vector<Handle> handles;
        std::string order;
        handles.push_back(event.listen([&](int&) { order += 'c'; }, 200));
        handles.push_back(event.listen([&](int&) { order += 'a'; }, 0));
        handles.push_back(event.listen([&](int&) { order += 'b'; }, 0));
        int value = 0; event.notify(value);
        check(order == "abc", "priority ordering and equal-priority FIFO");
    }
    {
        EventClass<int> event;
        int calls = 0, payload = 0;
        auto listener = event.listen([&](int& value) { ++calls; payload = value; }, Deliver::Main);
        std::thread worker([&] { int value = 42; event.notify(value); value = 100; });
        worker.join();
        check(calls == 0, "worker Main delivery queues");
        internal::drainMainThreadQueue();
        check(calls == 1 && payload == 42, "Main delivery owns payload copy");
        std::thread another([&] { int value = 7; event.notify(value); });
        another.join(); listener.disconnect(); internal::drainMainThreadQueue();
        check(calls == 1, "disconnect cancels queued Main delivery");
    }

    {
        EventClass<int> event;
        auto owner = std::make_shared<internal::AppOwner>();
        auto peer = std::make_shared<internal::AppOwner>();
        Handle owned, unowned;
        int calls = 0;
        bool correctOwner = true;
        unowned = event.listen([&](int&) { correctOwner &= !internal::currentAppOwner(); });
        {
            internal::AppOwnerScope scope(owner);
            owned = event.listen([&](int&) { ++calls; correctOwner &= internal::currentAppOwner() == owner; });
        }
        {
            internal::AppOwnerScope scope(peer);
            int value = 0; event.notify(value);
            correctOwner &= internal::currentAppOwner() == peer;
        }
        check(correctOwner && calls == 1, "each listener restores its own registration owner and then the caller");
        endOwner(owner);
        int value = 0; event.notify(value);
        check(calls == 1, "ended App token suppresses owned callback");
    }

    {
        EventClass<int> event;
        struct Receiver { Handle handle; };
        auto receiver = std::make_unique<Receiver>();
        int calls = 0;
        auto killer = event.listen([&](int&) { receiver.reset(); });
        receiver->handle = event.listen([&](int&) { ++calls; });
        int value = 0; event.notify(value);
        check(calls == 0, "destroying a later listener during dispatch cancels it");
    }
    {
        EventClass<int> event;
        std::vector<Handle> handles;
        std::vector<std::pair<int, int>> expected;
        std::vector<int> actual;
        for (int index = 0; index < 3000; ++index) {
            const int priority = (index * 73) % 127 - 63;
            expected.emplace_back(priority, index);
            handles.push_back(event.listen([&, index](int&) { actual.push_back(index); }, priority));
        }
        std::stable_sort(expected.begin(), expected.end(), [](auto a, auto b) { return a.first < b.first; });
        int value = 0; event.notify(value);
        bool okay = actual.size() == expected.size();
        for (size_t index = 0; index < actual.size(); ++index) okay &= actual[index] == expected[index].second;
        check(okay, "3000 mixed priorities preserve sorted order and ties");
        event.clear();
    }
    {
        EventClass<int> event;
        Handle added;
        std::string order;
        int depth = 0;
        auto first = event.listen([&](int& value) {
            order += 'a';
            if (!depth) {
                ++depth;
                added = event.listen([&](int&) { order += 'c'; });
                event.notify(value);
            }
        });
        auto second = event.listen([&](int&) { order += 'b'; });
        int value = 0; event.notify(value);
        check(order == "aabcb", "nested notify sees newly added listener; enclosing pass keeps its snapshot");
    }
    {
        struct Input { bool consumed = false; };
        EventClass<Input> event;
        int calls = 0;
        auto first = event.listen([&](Input& value) { ++calls; value.consumed = true; });
        auto later = event.listen([&](Input&) { ++calls; });
        Input value; event.notify(value);
        check(calls == 1, "consumed payload stops synchronous propagation");
    }
    {
        EventClass<int> event;
        int calls = 0;
        std::mutex mutex;
        std::condition_variable condition;
        bool entered = false, release = false;
        auto listener = event.listen([&](int&) {
            std::unique_lock lock(mutex);
            ++calls; entered = true; condition.notify_all();
            condition.wait(lock, [&] { return release; });
        });
        std::thread worker([&] { int value = 0; event.notify(value); });
        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [&] { return entered; });
        }
        listener.disconnect();
        {
            std::lock_guard lock(mutex); release = true;
        }
        condition.notify_all(); worker.join();
        int value = 0; event.notify(value);
        check(calls == 1, "cross-thread disconnect lets an admitted callback finish and suppresses future calls");
    }

    {
        EventClass<int> event;
        auto owner = std::make_shared<internal::AppOwner>();
        std::weak_ptr<int> retained;
        Handle listener;
        int calls = 0;
        {
            internal::AppOwnerScope scope(owner);
            auto capture = std::make_shared<int>(42); retained = capture;
            listener = event.listen([capture, &calls](int&) { ++calls; }, Deliver::Main);
        }
        std::thread worker([&] { int value = 0; event.notify(value); });
        worker.join();
        auto retire = event.listen([&](int&) {
            internal::retireAppOwner(owner);
            check(!retained.expired(), "App retirement honors the variant capture lifetime with outstanding snapshot/queue");
        }, -10);
        int value = 0; event.notify(value);
        internal::drainMainThreadQueue();
        check(calls == 0, "App retirement cancels pending synchronous and Main callbacks");
    }

}

using Owner = internal::AppOwner;
using Handle = EventListener;
struct Empty : App {};
struct CopyRegistration {
    Event<void>* auxiliary;
    int* calls;
    std::shared_ptr<EventListener> registration;
    CopyRegistration(Event<void>& auxiliary, int& calls) : auxiliary(&auxiliary), calls(&calls) {}
    CopyRegistration(const CopyRegistration& other) : auxiliary(other.auxiliary), calls(other.calls),
        registration(std::make_shared<EventListener>(auxiliary->listen([calls = calls] { ++*calls; }))) {}
};
void reviewContracts() {
    {
        auto owner = std::make_shared<Owner>();
        Event<void> auxiliary;
        Event<CopyRegistration> source;
        Handle listener;
        int calls = 0;
        {
            internal::AppOwnerScope registration(owner);
            listener = source.listen([](CopyRegistration&) {}, Deliver::Main);
        }
        std::thread worker([&] { CopyRegistration value(auxiliary, calls); source.notify(value); });
        worker.join();
        check(auxiliary.listenerCount() == 1, "Main payload copy can register a listener");
        internal::retireAppOwner(owner);
        auxiliary.notify();
        check(auxiliary.listenerCount() == 0 && calls == 0,
            "payload-copy registrations belong to the subscriber App and cancel on retirement");
        internal::drainMainThreadQueue();
    }
    {
        auto window = std::make_shared<Window>();
        auto peer = std::make_shared<Window>();
        internal::WindowRequestAccess::bindForTests(*window, 15161);
        internal::WindowRequestAccess::bindForTests(*peer, 15162);
        internal::retainRuntimeWindow(window);
        internal::retainRuntimeWindow(peer);
        window->setApp<Empty>();
        internal::applyPendingAppForTests(*window);
        auto owner = internal::appOwner(*window->getApp());
        Handle listener;
        int calls = 0;
        {
            internal::AppOwnerScope registration(owner);
            listener = window->events().exit.listen([&] { ++calls; }, Deliver::Main);
        }
        std::thread worker([&] { window->events().exit.notify(); });
        worker.join();
        window->close();
        internal::drainMainThreadQueue();
        check(!owner->token->load() && !owner->ended && calls == 0,
            "queued exit subscription skips closing App before the exit boundary");
        internal::WindowRequestAccess::teardown(*window);
        check(calls == 1, "exit subscription still runs at teardown");
        internal::WindowRequestAccess::teardown(*peer);
    }
}
struct UpdateProbe : App {
    int& updates;
    explicit UpdateProbe(int& updates) : updates(updates) {}
    void update() override { ++updates; }
};
void ownedContracts() {
    // Real WindowContext/Runtime scopes, without opening native windows.
    auto a = std::make_shared<Owner>(), b = std::make_shared<Owner>(), c = std::make_shared<Owner>();
    Window wa, wb, wc;
    auto* ca = &internal::WindowRequestAccess::context(wa);
    auto* cb = &internal::WindowRequestAccess::context(wb);
    auto* cc = &internal::WindowRequestAccess::context(wc);
    a->context = ca; b->context = cb; c->context = cc;
    internal::WindowContextScope selected(ca);
    {
        internal::AppOwnerScope attribution(a);
        check(internal::captureEventDispatchContext().eligibleOwner != a.get(), "attribution alone requires Runtime admission");
    }
    internal::AppCallbackScope outer(a);
    check(internal::captureEventDispatchContext().eligibleOwner == a.get(), "Runtime entry with matching context is eligible");
    {
        bool fullAdmission = false;
        std::thread worker([&] {
            internal::AppOwnerScope attribution(a);
            auto context = internal::captureEventDispatchContext();
            fullAdmission = !context.mainThread && context.eligibleOwner != a.get() && !context.selected;
        });
        worker.join();
        check(fullAdmission, "worker with same owner always uses full admission without reading window state");
    }
    for (int mutation = 0; mutation < 4; ++mutation) {
        Event<int> event, nested;
        auto owner = std::make_shared<Owner>(); owner->context = cb;
        Handle first, second, inner;
        bool innerCorrect = false, nextCorrect = false;
        int calls = 0;
        {
            internal::AppOwnerScope registration(c);
            inner = nested.listen([&](int&) {
                innerCorrect = internal::currentAppOwner() == c && internal::currentWindowCtx() == cc;
                internal::restoreWindowContext(ca);
            });
        }
        {
            internal::AppOwnerScope registration(owner);
            first = event.listen([&](int& value) {
                ++calls;
                check(internal::currentAppOwner() == owner && internal::currentWindowCtx() == cb, "cross-App callback selects subscriber and context");
                if (mutation == 0) internal::restoreWindowContext(cc);
                if (mutation == 1) first.disconnect();
                if (mutation == 2) closeOwner(owner);
                if (mutation == 3) nested.notify(value);
            });
            second = event.listen([&](int&) {
                ++calls;
                nextCorrect = internal::currentAppOwner() == owner && internal::currentWindowCtx() == cb &&
                    mcp::internal::registrationOwner() == owner->token.get();
            });
        }
        int value = 0; event.notify(value);
        check(internal::currentAppOwner() == a && internal::currentWindowCtx() == ca &&
            mcp::internal::registrationOwner() == a->token.get(), "cross-App callback restores caller context and registration owner");
        if (mutation == 0) check(calls == 2 && nextCorrect, "changed window context is repaired before next listener");
        if (mutation == 1) check(calls == 2 && nextCorrect, "self-disconnect preserves next listener context");
        if (mutation == 2) check(calls == 1, "self-close cancels later listeners in the retained snapshot");
        if (mutation == 3) check(calls == 2 && nextCorrect && innerCorrect, "nested cross-App notify repairs both enclosing contexts");
    }
    {
        Event<void> event;
        auto first = event.listen([&] { internal::restoreWindowContext(cb); });
        bool repaired = false;
        auto second = event.listen([&] { repaired = internal::currentWindowCtx() == ca; });
        event.notify();
        check(repaired && internal::currentWindowCtx() == ca, "cached same-owner dispatch repairs context without a new owner scope");
        internal::restoreWindowContext(cb);
        check(internal::captureEventDispatchContext().eligibleOwner != a.get(), "same owner with wrong selected context is ineligible");
        event.notify();
        check(repaired && internal::currentWindowCtx() == cb, "wrong-context dispatch admits owner and restores caller's selected context");
        internal::restoreWindowContext(ca);
    }
    {
        Event<void> event;
        auto first = event.listen([&] { internal::restoreWindowContext(cb); throw std::runtime_error("expected"); });
        bool caught = false;
        try { event.notify(); } catch (const std::runtime_error&) { caught = true; }
        check(caught && internal::currentWindowCtx() == ca, "context repair also runs when a callback throws");
    }
    {
        Event<void> event;
        std::vector<Handle> handles;
        std::string order;
        for (int index = 0; index < 8; ++index) {
            internal::AppOwnerScope registration(index % 2 ? b : a);
            handles.push_back(event.listen([&, index] { order += char('0' + index); }, index < 4 ? 10 : 0));
        }
        event.notify();
        check(order == "45670123", "mixed owners preserve priority and FIFO without owner grouping");
    }
    {
        Event<int> event;
        auto owner = std::make_shared<Owner>();
        internal::AppOwnerScope registration(owner);
        Handle added;
        int calls = 0;
        bool registered = false;
        auto first = event.listen([&](int&) {
            if (registered) return;
            registered = true;
            std::thread worker([&] {
                internal::AppOwnerScope workerRegistration(owner);
                added = event.listen([&](int&) { ++calls; });
            });
            worker.join();
        });
        int value = 0; event.notify(value);
        check(calls == 0, "other-thread registration joins only the next snapshot");
        closeOwner(owner); event.notify(value);
        check(calls == 0 && event.listenerCount() == 0, "App registry includes other-thread registration");
        auto rejected = event.listen([](int&) {});
        check(!rejected && event.listenerCount() == 0, "registration after closing cannot revive a listener");
    }
    {
        auto owner = std::make_shared<Owner>();
        auto alive = std::make_shared<std::atomic<bool>>(true);
        int removals = 0;
        auto reserved = internal::registerAppListener(owner, alive, false, [&] { ++removals; });
        closeOwner(owner);
        check(!alive->load() && removals == 1, "closing a registered but unpublished record clears its flag before publication");
        auto rejected = internal::registerAppListener(owner, std::make_shared<std::atomic<bool>>(true), false, [] {});
        check(!rejected->alive->load(), "registry rejects closing Apps before tree publication");
        internal::unregisterAppListener(reserved);
    }
    {
        Event<void> event;
        Handle self;
        std::weak_ptr<int> weak;
        bool retained = false;
        auto capture = std::make_shared<int>(4); weak = capture;
        self = event.listen([&, capture] { self.disconnect(); retained = !weak.expired(); });
        capture.reset(); event.notify();
        check(retained && weak.expired(), "snapshot retains self-disconnected callable through return and then releases it");
    }
    {
        Event<int> event;
        auto owner = std::make_shared<Owner>();
        Handle listener;
        int calls = 0;
        std::weak_ptr<int> weak;
        {
            internal::AppOwnerScope registration(owner);
            auto capture = std::make_shared<int>(3); weak = capture;
            listener = event.listen([&, capture](int&) { ++calls; }, Deliver::Main);
        }
        std::thread worker([&] { int value = 1; event.notify(value); }); worker.join();
        closeOwner(owner);
        check(!weak.expired(), "queued Main work owns invalidated callable until queue release");
        internal::drainMainThreadQueue();
        check(calls == 0 && weak.expired(), "closing suppresses queued Main work then releases captures");
    }
}
void generationAndExitContracts() {
    auto app = internal::makeConstructedApp<Empty>();
    auto owner = internal::appOwner(*app);
    owner->generation = internal::nextAppGeneration();
    Event<int> event;
    Handle listener;
    int calls = 0;
    std::weak_ptr<int> weak;
    {
        internal::AppOwnerScope registration(owner);
        auto capture = std::make_shared<int>(5); weak = capture;
        listener = event.listen([&, capture](int&) { ++calls; });
    }
    auto snapshot = internal::FrameworkEventAccess::snapshot(event);
    internal::retireAppGeneration(owner->generation);
    int value = 0; event.notify(value);
    check(calls == 0 && event.listenerCount() == 0, "guest generation unload invalidates listeners with an outstanding snapshot");
    check(!weak.expired(), "guest generation snapshot retains callable safely after invalidation");
    snapshot.reset();
    check(weak.expired(), "last guest snapshot releases callable after retirement");
    Window window;
    auto* context = &internal::WindowRequestAccess::context(window);
    auto exitOwner = std::make_shared<Owner>(); exitOwner->context = context;
    std::string order;
    Handle first, second, framework;
    {
        internal::AppOwnerScope registration(exitOwner);
        first = window.events().exit.listen([&] { order += 'a'; throw std::runtime_error("expected exit failure"); });
        second = window.events().exit.listen([&] { order += 'b'; });
        framework = internal::FrameworkEventAccess::listen(window.events().exit, [&] { order += 'f'; }, 200);
    }
    exitOwner->ended = true; endOwner(exitOwner);
    internal::TeardownSteps steps;
    internal::FrameworkEventAccess::notifyShutdown(window.events().exit, steps);
    check(order == "abf", "ended-App exit exception preserves later exit and protected framework hooks");
    closeOwner(exitOwner);
    internal::FrameworkEventAccess::notifyShutdown(window.events().exit, steps);
    check(order == "abff", "callbacksClosing cancels exit callbacks and preserves framework hooks");
}
template<class Tree> int validateTree(const Tree& tree, bool& valid) {
    if (!tree) return 0;
    const int lhs = validateTree(tree->left, valid), rhs = validateTree(tree->right, valid);
    valid = valid && std::abs(lhs - rhs) <= 1 && tree->height == 1 + (std::max)(lhs, rhs);
    return tree->height;
}
void balancedStorage() {
    Event<int> event;
    std::vector<Handle> handles;
    for (int index = 0; index < 3000; ++index) handles.push_back(event.listen([](int&) {}, (index * 73) % 127));
    auto retained = internal::FrameworkEventAccess::snapshot(event);
    bool valid = true;
    validateTree(retained, valid);
    for (int index = 0; index < 3000; ++index) {
        // 197 and 3000 are coprime: visit every identity, in mixed key order.
        handles[(index * 197) % 3000].disconnect();
        validateTree(internal::FrameworkEventAccess::snapshot(event), valid);
    }
    validateTree(retained, valid);
    check(valid && event.listenerCount() == 0 && std::distance(retained->begin(), retained->end()) == 3000,
        "AVL insert/erase stay balanced and retain immutable old paths for 3000 mixed keys");
}
void remainingContracts() {
    {
        auto firstWindow = std::make_shared<Window>();
        auto peerWindow = std::make_shared<Window>();
        internal::WindowRequestAccess::bindForTests(*firstWindow, 15141);
        internal::WindowRequestAccess::bindForTests(*peerWindow, 15142);
        internal::retainRuntimeWindow(firstWindow); internal::retainRuntimeWindow(peerWindow);
        int updates = 0;
        firstWindow->setApp<UpdateProbe>(updates); internal::applyPendingAppForTests(*firstWindow);
        auto owner = internal::appOwner(*firstWindow->getApp());
        auto& event = firstWindow->events().update;
        Handle first, later, queued;
        int calls = 0, queuedCalls = 0;
        {
            internal::AppOwnerScope registration(owner);
            first = event.listen([&] { ++calls; firstWindow->close(); }, EventPriority::BeforeApp);
            later = event.listen([&] { ++calls; }, EventPriority::BeforeApp);
            queued = peerWindow->events().update.listen([&] { ++queuedCalls; }, Deliver::Main);
        }
        std::thread worker([&] { peerWindow->events().update.notify(); }); worker.join();
        {
            internal::AppCallbackScope outer(owner);
            event.notify();
            check(calls == 1, "Window::close inside callback eagerly cancels its remaining listeners");
        }
        check(!owner->token->load(), "accepted Window close ends App run admission immediately");
        check(updates == 0, "close before the framework App slot suppresses App update too");
        internal::drainMainThreadQueue();
        check(queuedCalls == 0, "Window close invalidates already queued work on a foreign Event");
        check(peerWindow->isOpen(), "closing one Window preserves its peer");
        internal::WindowRequestAccess::teardown(*firstWindow);
        check(owner->ended && owner->retiring, "Window teardown still ends and retires App after eager invalidation");
        internal::WindowRequestAccess::teardown(*peerWindow);
    }
    {
        auto window = std::make_shared<Window>();
        internal::WindowRequestAccess::bindForTests(*window, 15143);
        internal::retainRuntimeWindow(window);
        window->setApp<Empty>(); internal::applyPendingAppForTests(*window);
        auto owner = internal::appOwner(*window->getApp());
        Event<void> event;
        Handle listener;
        int calls = 0;
        {
            internal::AppOwnerScope registration(owner);
            listener = event.listen([&] { ++calls; });
        }
        const bool nativeAllowed = internal::allowWindowClose({15143}, window.get());
        event.notify();
        check(!nativeAllowed && internal::WindowRequestAccess::closeRequested(*window) &&
            !owner->token->load() && calls == 0,
            "accepted last-window native close invalidates before outer Runtime cleanup");
        internal::WindowRequestAccess::teardown(*window);
    }
    {
        Event<void> event;
        auto owner = std::make_shared<Owner>();
        internal::AppCallbackScope outer(owner);
        std::weak_ptr<int> weak;
        auto capture = std::make_shared<int>(1); weak = capture;
        int calls = 0; bool retained = false;
        auto first = event.listen([&, capture] {
            internal::retireAppOwner(owner); retained = !weak.expired(); ++calls;
        });
        auto later = event.listen([&] { ++calls; });
        capture.reset(); event.notify();
        check(retained && calls == 1, "self-retirement retains executing callable and suppresses tail");
        check(weak.expired(), "self-retirement releases callable after snapshot unwinds");
    }
    {
        Event<void> event;
        auto owner = std::make_shared<Owner>();
        internal::AppOwnerScope registration(owner);
        int calls = 0;
        auto first = internal::FrameworkEventAccess::listen(event, [&] { ++calls; event.clear(); }, 0);
        auto user = event.listen([&] { calls += 100; });
        auto last = internal::FrameworkEventAccess::listen(event, [&] { ++calls; }, 200);
        event.notify(); event.notify();
        check(calls == 4 && event.listenerCount() == 2, "protected framework roots survive repeated clear during dispatch");
        internal::retireAppOwner(owner); event.notify();
        check(calls == 6, "App retirement preserves protected framework listeners");
    }
    {
        auto event = std::make_unique<Event<void>>();
        int calls = 0;
        auto queued = event->listen([&] { ++calls; }, Deliver::Main);
        std::thread worker([&] { event->notify(); }); worker.join();
        event.reset(); internal::drainMainThreadQueue();
        check(calls == 0, "source destruction cancels queued Main work");
    }
    {
        struct Input { bool consumed = true; };
        Event<Input> event;
        int calls = 0;
        auto dead = event.listen([&](Input&) { calls += 100; });
        auto first = event.listen([&](Input&) { ++calls; });
        auto later = event.listen([&](Input&) { calls += 100; });
        dead.disconnect(); Input input; event.notify(input);
        check(calls == 1, "already-consumed payload still calls first live synchronous listener");
    }
    {
        Event<int> event;
        auto owner = std::make_shared<Owner>();
        Handle listener;
        int calls = 0, payload = 0; bool correct = false;
        {
            internal::AppOwnerScope registration(owner);
            listener = event.listen([&](int& value) {
                ++calls; payload = value; correct = internal::currentAppOwner() == owner;
            }, Deliver::Main);
        }
        std::thread worker([&] { int value = 23; event.notify(value); value = 99; }); worker.join();
        check(calls == 0, "owned Main work queues off-thread");
        internal::drainMainThreadQueue();
        check(calls == 1 && payload == 23 && correct, "owned Main work selects subscriber and copied payload");
        check(!internal::currentAppOwner(), "owned Main work restores unowned caller");
    }
    {
        auto owner = std::make_shared<Owner>();
        auto flagA = std::make_shared<std::atomic<bool>>(true), flagB = std::make_shared<std::atomic<bool>>(true);
        bool allDead = false;
        auto one = internal::registerAppListener(owner, flagA, false, [&] { allDead = !flagA->load() && !flagB->load(); });
        auto two = internal::registerAppListener(owner, flagB, false, [] {});
        closeOwner(owner);
        check(allDead, "all flags clear before any removal can reenter user code");
        check(owner->listeners.empty(), "closing drains App listener registry");
    }
    {
        Event<void> event;
        auto owner = std::make_shared<Owner>();
        internal::AppOwnerScope registration(owner);
        int calls = 0;
        auto first = event.listen([&] { ++calls; });
        auto snapshot = internal::FrameworkEventAccess::snapshot(event);
        internal::retireAppOwner(owner); event.notify();
        check(calls == 0 && event.listenerCount() == 0 && snapshot, "void retirement suppresses retained snapshots too");
    }
}
} // namespace
TC_CORE_TEST_MAIN() {
    std::thread preload([] { logNotice("Preload") << "loading before the app runner starts"; });
    preload.join();
    check(isMainThread(), "worker Inline logging before runner startup preserves the real main identity");
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    contracts<Event>(); ownedContracts(); generationAndExitContracts(); remainingContracts(); balancedStorage(); reviewContracts();
    internal::shutdownRuntime();
    std::printf("eventDispatchD: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
