#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "tc/sound/tcMicInputInternal.h"
#include <cstdio>
#include <thread>
using namespace tc;
namespace {
int failures = 0;
void check(const char* label, bool okay) {
    std::printf("appCancellationLifetime: %s: %s\n", label, okay ? "PASS" : "FAIL");
    failures += !okay;
}
struct Empty : App {};
size_t records(const std::shared_ptr<internal::AppOwner>& owner) {
    std::lock_guard lock(owner->cancellationsMutex);
    return owner->cancellations.size();
}
void cycles() {
    auto app = internal::makeConstructedApp<Empty>();
    auto owner = internal::appOwner(*app);
    Event<int> typed;
    Event<void> plain;
    const auto initial = records(owner);
    int calls = 0;
    {
        internal::AppOwnerScope scope(owner);
        for (int i = 0; i < 10000; ++i) {
            auto a = typed.listen([&](int&) { ++calls; });
            auto b = plain.listen([&] { ++calls; });
            if (i % 2) { a.disconnect(); b.disconnect(); }
        }
        check("10000 typed/void destroy and disconnect cycles keep record count flat", records(owner) == initial);
        EventListener a, b;
        for (int i = 0; i < 10000; ++i) {
            a = typed.listen([&](int&) { ++calls; });
            b = plain.listen([&] { ++calls; });
        }
        a.disconnect(); b.disconnect();
        check("10000 typed/void replacement cycles keep record count flat", records(owner) == initial);
        std::vector<EventListener> cleared;
        for (int i = 0; i < 1000; ++i) {
            cleared.push_back(typed.listen([](int&) {}));
            cleared.push_back(plain.listen([] {}));
            typed.clear(); plain.clear();
        }
        check("clear releases records while listener handles survive", records(owner) == initial);
        EventListener orphan;
        {
            Event<void> temporary;
            orphan = temporary.listen([] {});
        }
        check("Event destruction releases records while listener handle survives", records(owner) == initial);
        for (int i = 0; i < 10000; ++i) {
            auto callback = internal::appOwnedCallback(std::function<void()>([&] { ++calls; }));
            auto completion = std::exchange(callback, {});
            completion();
        }
        check("10000 async-style completions keep record count flat", records(owner) == initial && calls == 10000);
        auto reusable = internal::appOwnedCallback(std::function<int()>([&] { ++calls; return 42; }));
        auto copy = reusable;
        reusable = {};
        check("callback copy keeps retirement registration and remains reusable", records(owner) == initial + 1 && copy() == 42 && copy() == 42);
    }
    check("last callback copy unregisters", records(owner) == initial);
    internal::retireAppOwner(owner);
}
void microphoneCycles() {
    auto app = internal::makeConstructedApp<Empty>();
    auto owner = internal::appOwner(*app);
    internal::setMicAuthorizationForTests([] { return internal::MicAuthorization::Denied; }, {});
    {
        internal::AppOwnerScope scope(owner);
        MicInput mic;
        const auto initial = records(owner);
        bool stable = true;
        for (int i = 0; i < 256; ++i) {
            mic.setup();
            mic.close();
            stable = stable && records(owner) == initial;
        }
        check("repeated mocked microphone setup/close keeps record count flat", stable);
    }
    internal::setMicAuthorizationForTests({}, {});
    internal::retireAppOwner(owner);
}
void retirement() {
    auto first = internal::makeConstructedApp<Empty>();
    auto second = internal::makeConstructedApp<Empty>();
    auto owner = internal::appOwner(*first);
    auto peer = internal::appOwner(*second);
    Event<void> plain;
    Event<int> typed;
    EventListener a, b, survivor;
    std::function<void()> pending;
    std::weak_ptr<int> capture;
    int calls = 0, peerCalls = 0;
    {
        internal::AppOwnerScope scope(owner);
        auto held = std::make_shared<int>(1); capture = held;
        a = plain.listen([held, &calls] { ++calls; }, Deliver::Main);
        b = typed.listen([held, &calls](int&) { ++calls; }, Deliver::Main);
        pending = internal::appOwnedCallback(std::function<void()>([held, &calls] { ++calls; }));
    }
    {
        internal::AppOwnerScope scope(peer);
        survivor = plain.listen([&] {
            ++peerCalls;
            check("callback restores subscribing App", internal::currentAppOwner() == peer);
        }, Deliver::Main);
    }
    std::thread worker([&] { int value = 0; typed.notify(value); plain.notify(); });
    worker.join();
    internal::retireAppOwner(owner);
    check("retirement releases captures even with retained callback and queued deliveries", capture.expired());
    check("retirement disconnects only its own listeners", typed.listenerCount() == 0 && plain.listenerCount() == 1 && records(owner) == 0);
    pending();
    internal::drainMainThreadQueue();
    plain.notify();
    check("retired queued and retained callbacks never run; peer survives", calls == 0 && peerCalls == 2);
    check("callback restores caller ownership", !internal::currentAppOwner());
    internal::retireAppOwner(peer);
    check("peer retirement also disconnects", plain.listenerCount() == 0 && records(peer) == 0);
}
}
TC_CORE_TEST_MAIN(int, char**) {
    getMainThreadId();
    internal::beginRuntimeRun();
    internal::beginFrameworkLifetime();
    cycles(); microphoneCycles(); retirement();
    internal::shutdownRuntime();
    return failures ? 1 : 0;
}
