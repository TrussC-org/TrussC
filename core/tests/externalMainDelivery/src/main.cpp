#include "../../common/tcTcpClientTest.h"
#include "../../common/tcCoreTest.h"
#include "tc/network/tcTcpServer.h"
#include <future>
#include "../../../../addons/tcxOsc/src/tcxOscReceiver.h"
#include "../../../../addons/tcxMidi/src/tcxMidiIn.h"
#include "../../../../addons/tcxWebSocket/src/tcWebSocketClient.h"

namespace trussc::internal {
struct ExternalUdpDeliveryTestAccess {
    static void receiveError(UdpSocket& socket) {
        socket.notifyError(UdpSocket::ErrorKind::Receive, "Receive error", 42);
    }
    static void appError(UdpSocket& socket) {
        socket.notifyError(UdpSocket::ErrorKind::Send, "Application operation failed", 43);
    }
};
}

namespace tcx::osc {
struct OscDeliveryTestAccess {
    static void prepare(OscReceiver& receiver) { receiver.prepareSocket(0); }
    static void error(OscReceiver& receiver, bool external) {
        if (external) tc::internal::ExternalUdpDeliveryTestAccess::receiveError(receiver.socket_);
        else tc::internal::ExternalUdpDeliveryTestAccess::appError(receiver.socket_);
    }
    static void receive(OscReceiver& receiver, tc::UdpReceiveEventArgs& packet) {
        receiver.state_->handleReceive(packet, receiver.state_->generation_);
    }
};
}

namespace {
using Store = internal::ExternalDeliveryStore;
template<class T> using External = internal::ExternalEventDelivery<T>;

struct CopyProbe {
    int value = 7;
    static std::atomic<int> copies;
    static bool failCopy;
    CopyProbe() = default;
    CopyProbe(const CopyProbe& other) : value(other.value) { if (failCopy) throw std::bad_alloc(); ++copies; }
};
std::atomic<int> CopyProbe::copies{0};
bool CopyProbe::failCopy = false;

struct ClosureProbe {
    static std::atomic<int> copies;
    ClosureProbe() = default;
    ClosureProbe(const ClosureProbe&) { ++copies; }
};
std::atomic<int> ClosureProbe::copies{0};

template<class T> void cancelledSlot(bool clear) {
    Event<T> source;
    source.setMainThreadDeliveryLimit(1);
    int calls = 0;
    auto first = source.listen([&](auto&...) { ++calls; }, Deliver::Main);
    auto emit = [&] {
        if constexpr (std::is_void_v<T>) return External<T>::notify(source);
        else { T value{}; return External<T>::notify(source, value); }
    };
    std::thread queued([&] { check("copy accepted before cancellation", emit()); });
    queued.join();
    auto store = External<T>::diagnostics(source);
    if (clear) source.clear();
    else first.disconnect();
    check("cancelled copy retains slot before next drain", store->pending == 1);
    auto replacement = source.listen([&](auto&...) { ++calls; }, Deliver::Main);
    std::thread beforeDrain([&] { check("replacement drops while cancelled copy awaits drain", !emit()); });
    beforeDrain.join();
    check("replacement rejection counted before drain", store->pending == 1 && store->rejectedCopies == 1);
    internal::drainMainThreadQueue();
    check("drain discards cancelled copy and returns slot", calls == 0 && store->pending == 0);
    std::thread afterDrain([&] { check("replacement accepted after drain", emit()); });
    afterDrain.join();
    internal::drainMainThreadQueue();
    check("replacement runs after slot returned", calls == 1 && store->pending == 0);
}

void allocationFailure() {
    Event<CopyProbe> source;
    source.setMainThreadDeliveryLimit(0);
    int calls = 0;
    auto listener = source.listen([&](auto&) { ++calls; }, Deliver::Main);
    std::thread producer([&] {
        CopyProbe value;
        CopyProbe::failCopy = true;
        check("failed payload allocation returns cleanly", !External<CopyProbe>::notify(source, value));
        CopyProbe::failCopy = false;
        check("delivery works after allocation failure", External<CopyProbe>::notify(source, value));
    });
    producer.join();
    auto store = External<CopyProbe>::diagnostics(source);
    check("failed copy counted and pending slot released", store->pending == 1 && store->rejectedCopies == 1);
    source.setMainThreadDeliveryLimit(1);
    internal::drainMainThreadQueue();
    check("lowering optional limit retains accepted copy", calls == 1 && store->pending == 0);
}

template<class T> void optionalTransportLimit() {
    Event<T> source;
    source.setMainThreadDeliveryLimit(1);
    int mainCalls = 0, inlineCalls = 0;
    auto mainListener = source.listen([&](T&) { ++mainCalls; }, Deliver::Main);
    auto inlineListener = source.listen([&](T&) { ++inlineCalls; });
    auto gate = std::make_shared<internal::NetworkDelivery>();
    auto target = internal::EventSnapshotDelivery<T>::bind(source, gate);
    internal::EventSnapshotDelivery<T> ingress{target};
    std::thread worker([&] {
        T value;
        check("transport first Main copy accepted", ingress.notifyExternal(value));
        check("transport explicit limit drops second Main copy", !ingress.notifyExternal(value));
    });
    worker.join();
    auto store = External<T>::diagnostics(source);
    check("transport drops count while Inline delivery continues", store->pending == 1 && store->rejectedCopies == 1 && inlineCalls == 2);
    internal::drainMainThreadQueue();
    check("transport accepted copy runs", mainCalls == 1 && store->pending == 0);
}

void noPayloadByteLimit() {
    Event<std::string> source;
    int calls = 0;
    auto listener = source.listen([&](std::string& value) { check("reserved payload delivered whole", value == "x"); ++calls; }, Deliver::Main);
    std::thread producer([&] {
        std::string value = "x";
        value.reserve(65 * 1024 * 1024);
        check("source capacity above old byte cap accepted", External<std::string>::notify(source, value));
    });
    producer.join();
    internal::drainMainThreadQueue();
    check("large source capacity has no delivery byte charge", calls == 1);
}

void optionalVoidLimit() {
    Event<void> source;
    source.setMainThreadDeliveryLimit(1);
    int calls = 0;
    auto first = source.listen([&] { ++calls; }, Deliver::Main);
    auto second = source.listen([&] { ++calls; }, Deliver::Main);
    std::thread producer([&] { check("void event shares limit across listeners", !External<void>::notify(source)); });
    producer.join();
    auto store = External<void>::diagnostics(source);
    check("void event whole-copy drop counted", store->pending == 1 && store->rejectedCopies == 1);
    source.setMainThreadDeliveryLimit(0);
    std::thread unlimited([&] { check("zero restores unlimited void delivery", External<void>::notify(source)); });
    unlimited.join();
    internal::drainMainThreadQueue();
    check("accepted void calls survive limit changes", calls == 3 && store->pending == 0);
}

void countAndFanout() {
    Event<CopyProbe> source;
    source.setMainThreadDeliveryLimit(1024);
    int called = 0;
    bool checkedExecuting = false;
    auto first = source.listen([&, tracked = ClosureProbe{}](CopyProbe& p) {
        (void)tracked;
        if (!checkedExecuting) {
            checkedExecuting = true;
            std::thread producer([&] { CopyProbe value; check("executing copies retain slots", !External<CopyProbe>::notify(source, value)); });
            producer.join();
            const int captureCopies = ClosureProbe::copies;
            std::thread registrar([&] {
                auto temporary = source.listen([](CopyProbe&) {}, Deliver::Main);
            });
            registrar.join();
            check("listener updates do not copy an executing callback", ClosureProbe::copies == captureCopies);
        }
        check("independent payload", p.value == 7); p.value = 1; ++called;
    }, Deliver::Main);
    auto second = source.listen([&](CopyProbe& p) { check("second independent payload", p.value == 7); ++called; }, Deliver::Main);
    CopyProbe payload;
    CopyProbe::copies = 0;
    ClosureProbe::copies = 0;
    std::thread worker([&] {
        for (int i = 0; i < 512; ++i) check("fanout admission", External<CopyProbe>::notify(source, payload));
        for (int i = 0; i < 101; ++i)
            check("both Main copies rejected", !External<CopyProbe>::notify(source, payload));
    });
    worker.join();
    auto store = External<CopyProbe>::diagnostics(source);
    check("1024 copies pending before any drain", store && store->pending == 1024 && called == 0);
    check("rejection occurs before payload copying", CopyProbe::copies == 1024);
    check("only admitted notifications copy callable captures", ClosureProbe::copies == 512);
    check("every rejected Main copy counted", store->rejectedCopies == 202);
    internal::drainMainThreadQueue();
    check("all admitted listeners run", called == 1024);
    check("only accepted copies queued", internal::getMainThreadQueuePending() == 1024);
    check("pending slots returned", store->pending == 0);
    check("rejections remain cumulative after drain", store->rejectedCopies == 204);
}

void oscAndMidi() {
    tcx::osc::OscReceiver osc;
    osc.onMessageReceived.setMainThreadDeliveryLimit(1024);
    tcx::osc::OscMessage message("/bounded");
    message.addString("complete");
    const auto bytes = message.toBytes();
    UdpReceiveEventArgs packet;
    packet.data.assign(bytes.begin(), bytes.end());
    int oscCalls = 0;
    auto oscListener = osc.onMessageReceived.listen([&](tcx::osc::OscMessage& p) {
        check("whole OSC message", p.getArgAsString(0) == "complete"); ++oscCalls;
    }, Deliver::Main);
    tcx::midi::MidiIn midi;
    midi.onMessage.setMainThreadDeliveryLimit(1024);
    int midiCalls = 0;
    auto midiListener = midi.onMessage.listen([&](tcx::midi::MidiMessage& p) {
        check("whole MIDI message", p.bytes == std::vector<unsigned char>({0x90, 60, 100})); ++midiCalls;
    }, Deliver::Main);
    check("open MIDI mock", midi.openPort(0));
    auto callback = libremidi::test::inputCallback;
    std::thread worker([&] {
        for (int i = 0; i < 1025; ++i) {
            tcx::osc::OscDeliveryTestAccess::receive(osc, packet);
            callback(libremidi::message{{0x90, 60, 100}, 0});
        }
    });
    worker.join();
    auto oscStore = External<tcx::osc::OscMessage>::diagnostics(osc.onMessageReceived);
    auto midiStore = External<tcx::midi::MidiMessage>::diagnostics(midi.onMessage);
    check("OSC stalled Main with polling never enabled", oscCalls == 0 && oscStore->pending == 1024 && oscStore->rejectedCopies == 1);
    check("MIDI stalled Main with polling never enabled", midiCalls == 0 && midiStore->pending == 1024 && midiStore->rejectedCopies == 1);
    internal::drainMainThreadQueue();
    check("OSC/MIDI whole-unit drop", oscCalls == 1024 && midiCalls == 1024);
    midi.closePort();
    libremidi::test::inputCallback = {};


}

// Two targets share the production acquisition gate. Removing the final
// capture reenters the other target while external acquisition is in flight.
// The suite watchdog turns a lock-order regression into a failing process.
template<class T> void sharedGateRemoval(bool clear) {
    using Delivery = internal::EventSnapshotDelivery<T>;
    Event<T> source, other;
    auto gate = std::make_shared<internal::NetworkDelivery>();
    auto target = Delivery::bind(source, gate);
    auto otherTarget = Delivery::bind(other, gate);
    std::promise<void> start;
    auto go = start.get_future();
    std::atomic<bool> done{false};
    struct Capture {
        std::shared_ptr<internal::NetworkDelivery> gate;
        std::shared_ptr<typename Delivery::Target> other;
        std::promise<void>& start;
        std::atomic<bool>& done;
        ~Capture() {
            start.set_value();
            while (!done.load()) {
                if (!gate->mutex.try_lock()) break;
                gate->mutex.unlock();
                std::this_thread::yield();
            }
            if constexpr (std::is_void_v<T>) (*other)();
            else { T arg{}; (*other)(arg); }
        }
    };
    auto capture = std::shared_ptr<Capture>(new Capture{gate, otherTarget, start, done});
    auto removed = source.listen([capture](auto&...) {});
    auto remaining = source.listen([](auto&...) {}, Deliver::Main);
    capture.reset();
    std::thread worker([&] {
        go.wait();
        if constexpr (std::is_void_v<T>) target->dispatch(true);
        else { T arg{}; target->dispatch(arg, {}, true); }
        done = true;
    });
    if (clear) source.clear();
    else removed.disconnect();
    worker.join();
    check("shared-gate external acquisition and capture destruction finish", done);
    internal::drainMainThreadQueue();
}

void lockOrder() {
    sharedGateRemoval<int>(false);
    sharedGateRemoval<void>(false);
    sharedGateRemoval<int>(true);
    sharedGateRemoval<void>(true);
}

void oscForwardedErrors() {
    tcx::osc::OscReceiver receiver;
    receiver.onParseError.setMainThreadDeliveryLimit(1024);
    // Install the real socket forwarding listeners. Error injection below
    // exercises UDP classification without depending on a failing OS socket.
    tcx::osc::OscDeliveryTestAccess::prepare(receiver);
    int calls = 0, appCalls = 0;
    auto listener = receiver.onParseError.listen([&](std::string& msg) {
        ++calls;
        if (msg.find("Application operation") != std::string::npos) ++appCalls;
    }, Deliver::Main);
    std::thread worker([&] {
        for (int i = 0; i < 10000; ++i) tcx::osc::OscDeliveryTestAccess::error(receiver, true);
    });
    worker.join();
    auto store = External<std::string>::diagnostics(receiver.onParseError);
    check("forwarded receive errors bound pending copies and count every rejection",
          store && store->pending == 1024 && store->rejectedCopies == 8976);
#if !defined(__EMSCRIPTEN__)
    check("forwarded receive errors queue only accepted copies",
          internal::mainThreadQueue().size() == 1024);
#endif
    std::thread app([&] {
        for (int i = 0; i < 10000; ++i) tcx::osc::OscDeliveryTestAccess::error(receiver, false);
    });
    app.join();
    check("forwarded app errors bypass saturated external store",
          store && store->pending == 1024 && store->rejectedCopies == 8976);
#if !defined(__EMSCRIPTEN__)
    check("forwarded app error copies stay growable", internal::mainThreadQueue().size() == 11024);
#endif
    internal::drainMainThreadQueue();
    check("forwarded receive/app error callbacks remain whole", calls == 11024 && appCalls == 10000);
    check("forwarded error pending slots released after drain", store && store->pending == 0);
}

void retainedSnapshotDestruction() {
    std::promise<void> entered, release;
    auto enteredFuture = entered.get_future();
    auto releaseFuture = release.get_future().share();
    auto source = std::make_unique<Event<int>>();
    int calls = 0;
    auto mainListener = source->listen([&](int&) { ++calls; }, Deliver::Main);
    auto inlineListener = source->listen([&](int&) { entered.set_value(); releaseFuture.wait(); });
    auto* event = source.get();
    std::thread worker([&] { int value = 0; External<int>::notify(*event, value); });
    check("external dispatch retains snapshot on worker", enteredFuture.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    auto store = External<int>::diagnostics(*source);
    source.reset();
    internal::drainMainThreadQueue();
    check("Event destruction cancels queued Main with snapshot still retained", calls == 0);
    release.set_value();
    worker.join();
    check("retained-snapshot destruction releases all pending slots", store->pending == 0);

    std::promise<void> voidEntered, voidRelease;
    auto voidReady = voidEntered.get_future();
    auto voidProceed = voidRelease.get_future().share();
    auto voidSource = std::make_unique<Event<void>>();
    auto voidMain = voidSource->listen([&] { ++calls; }, Deliver::Main);
    auto voidInline = voidSource->listen([&] { voidEntered.set_value(); voidProceed.wait(); });
    auto* voidEvent = voidSource.get();
    std::thread voidWorker([&] { External<void>::notify(*voidEvent); });
    check("void dispatch retains snapshot on worker", voidReady.wait_for(std::chrono::seconds(10)) == std::future_status::ready);
    auto voidStore = External<void>::diagnostics(*voidSource);
    voidSource.reset();
    internal::drainMainThreadQueue();
    check("void Event destruction cancels queued Main with snapshot retained", calls == 0);
    voidRelease.set_value();
    voidWorker.join();
    check("void retained-snapshot destruction releases pending slots", voidStore->pending == 0);

    int synchronousCalls = 0;
    auto synchronous = std::make_unique<Event<int>>();
    auto destroy = synchronous->listen([&](int&) { synchronous.reset(); });
    auto finish = synchronous->listen([&](int&) { ++synchronousCalls; }, Deliver::Main);
    int value = 0;
    synchronous->notify(value);
    check("already-main delivery finishes synchronous snapshot after source death", synchronousCalls == 1);
    auto synchronousVoid = std::make_unique<Event<void>>();
    auto destroyVoid = synchronousVoid->listen([&] { synchronousVoid.reset(); });
    auto finishVoid = synchronousVoid->listen([&] { ++synchronousCalls; }, Deliver::Main);
    synchronousVoid->notify();
    check("already-main void delivery preserves synchronous snapshot", synchronousCalls == 2);
}

void callableCopies() {
    for (bool external : {false, true}) {
        Event<int> typed;
        Event<void> empty;
        std::vector<int> typedSeen, voidSeen;
        auto typedListener = typed.listen([n = 0, &typedSeen](int&) mutable { typedSeen.push_back(++n); }, Deliver::Main);
        auto voidListener = empty.listen([n = 0, &voidSeen]() mutable { voidSeen.push_back(++n); }, Deliver::Main);
        std::thread worker([&] {
            int value = 0;
            for (int i = 0; i < 2; ++i) {
                if (external) { External<int>::notify(typed, value); External<void>::notify(empty); }
                else { typed.notify(value); empty.notify(); }
            }
        });
        worker.join();
        internal::drainMainThreadQueue();
        int value = 0;
        typed.notify(value);
        empty.notify();
        check("typed queued callables retain independent mutable captures", typedSeen == std::vector<int>({1, 1, 1}));
        check("void queued callables retain independent mutable captures", voidSeen == std::vector<int>({1, 1, 1}));
    }
}

void orderedCopies() {
    Event<std::string> edited;
    std::vector<std::string> seen;
    auto inlineEdit = edited.listen([](std::string& value) { value = "edited"; });
    auto first = edited.listen([&](std::string& value) { seen.push_back(value); }, Deliver::Main);
    auto inlineEditAgain = edited.listen([](std::string& value) { value += " again"; });
    auto second = edited.listen([&](std::string& value) { seen.push_back(value); }, Deliver::Main);
    std::thread producer([&] { std::string value = "original"; External<std::string>::notify(edited, value); });
    producer.join();
    internal::drainMainThreadQueue();
    std::string value = "original";
    External<std::string>::notify(edited, value);
    check("Main copies see earlier Inline edits on worker and main", seen == std::vector<std::string>({"edited", "edited again", "edited", "edited again"}));


}

// One-byte reads make the number of queued TCP chunks deterministic.
void tcpBacklog(bool serverSide, bool limited) {
    TcpServer server;
    server.setBindAddress("127.0.0.1");
    server.setReceiveBufferSize(1);
    TcpClient client;
    client.setReceiveBufferSize(1);
    if (limited) {
        server.onReceive.setMainThreadDeliveryLimit(7);
        client.onReceive.setMainThreadDeliveryLimit(7);
    }
    int calls = 0;
    std::atomic<int> received{0}, disconnected{0};
    EventListener main, observe, close;
    if (serverSide) {
        main = server.onReceive.listen([&](auto& args) { check("whole server chunk", args.data == vector<char>{'t'}); ++calls; }, Deliver::Main);
        observe = server.onReceive.listen([&](auto&) { ++received; });
        close = server.onClientDisconnect.listen([&](auto&) { ++disconnected; });
    } else {
        main = client.onReceive.listen([&](auto& args) { check("whole client chunk", args.data == vector<char>{'t'}); ++calls; }, Deliver::Main);
        observe = client.onReceive.listen([&](auto&) { ++received; });
        close = client.onDisconnect.listen([&](auto&) { ++disconnected; });
    }
    check("backlog server starts", server.start(0));
    check("backlog client connects", client.connect("127.0.0.1", server.getPort()));
    check("backlog client accepted", until([&] { return server.getClientCount() == 1; }));
    const std::string bytes(1500, 't');
    if (serverSide) check("send backlog to server", client.send(bytes));
    else server.broadcast(bytes);
    check("all TCP chunks read before next frame", until([&] { return received == 1500; }));
    check("backlog keeps TCP connection open", calls == 0 && disconnected == 0 && client.isConnected() && server.getClientCount() == 1);
    auto store = serverSide ? External<TcpServerReceiveEventArgs>::diagnostics(server.onReceive)
                            : External<TcpReceiveEventArgs>::diagnostics(client.onReceive);
    check("only explicit limit drops copies", store->pending == (limited ? 7u : 1500u) && store->rejectedCopies == (limited ? 1493u : 0u));
    internal::drainMainThreadQueue();
    check("all accepted chunks arrive", calls == (limited ? 7 : 1500) && store->pending == 0);
    client.disconnect();
    server.stop();
    internal::drainMainThreadQueue();
}

struct MoveOnlyProbe {
    MoveOnlyProbe() = default;
    MoveOnlyProbe(const MoveOnlyProbe&) = delete;
};

void lifetimeAndAppWork() {
    int calls = 0;
    auto source = std::make_unique<Event<int>>();
    auto listener = source->listen([&](int&) { ++calls; }, Deliver::Main);
    std::thread worker([&] { int payload = 0; External<int>::notify(*source, payload); });
    worker.join();
    auto store = External<int>::diagnostics(*source);
    source.reset();
    internal::drainMainThreadQueue();
    check("source destruction cancels external delivery and releases pending slots", calls == 0 && store->pending == 0);

    Event<int> cleared;
    auto removed = cleared.listen([&](int&) { ++calls; }, Deliver::Main);
    std::thread queued([&] { int value = 0; External<int>::notify(cleared, value); });
    queued.join();
    auto clearedStore = External<int>::diagnostics(cleared);
    cleared.clear();
    internal::drainMainThreadQueue();
    check("clear cancels external deliveries and releases pending slots", calls == 0 && clearedStore->pending == 0);

    Event<int> inlineSource;
    auto inlineListener = inlineSource.listen([](int&) {});
    std::thread inlineProducer([&] { int value = 0; External<int>::notify(inlineSource, value); });
    inlineProducer.join();
    check("external store stays unallocated without Main listeners", !External<int>::diagnostics(inlineSource));

    Event<MoveOnlyProbe> moveOnly;
    int inlineCalls = 0;
    bool workerDelivery = false;
    auto moveListener = moveOnly.listen([&](MoveOnlyProbe&) { ++inlineCalls; workerDelivery = !isMainThread(); }, Deliver::Main);
    std::thread moveProducer([&] { MoveOnlyProbe value; moveOnly.notify(value); });
    moveProducer.join();
    check("noncopyable Main payload preserves inline fallback", inlineCalls == 1 && workerDelivery);

    Event<int> app;
    auto appListener = app.listen([&](int&) { ++calls; }, Deliver::Main);
    int jobs = 0;
    std::thread producer([&] {
        int value = 0;
        for (int i = 0; i < 10000; ++i) {
            app.notify(value);
            runOnMainThread([&] { ++jobs; });
        }
        runOnMainThread([&] { jobs += 7; }); // teardown/completion work
    });
    producer.join();
    check("app event never allocates external store", !External<int>::diagnostics(app));
    internal::drainMainThreadQueue();
    check("app events and jobs remain growable", calls == 10000 && jobs == 10007);
}
}

TC_CORE_TEST_MAIN() {
    Watchdog watchdog;
    getMainThreadId();
    lockOrder();
    optionalTransportLimit<UdpReceiveEventArgs>();
    optionalTransportLimit<tcx::websocket::WebSocketEventArgs>();
    noPayloadByteLimit();
    optionalVoidLimit();
    for (bool clear : {false, true}) { cancelledSlot<int>(clear); cancelledSlot<void>(clear); }
    allocationFailure();
    countAndFanout();
    oscAndMidi();
    callableCopies();
    orderedCopies();
    oscForwardedErrors();
    retainedSnapshotDestruction();
    lifetimeAndAppWork();
    for (bool serverSide : {false, true}) {
        for (bool limited : {false, true}) tcpBacklog(serverSide, limited);
    }
    return failures ? 1 : 0;
}
