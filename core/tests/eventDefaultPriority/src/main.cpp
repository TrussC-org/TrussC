#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <algorithm>
#include <cstdio>
#include <limits>
#include <string>
#include <vector>

using namespace tc;
namespace {
int failures = 0;
void check(const char* name, bool ok) {
    std::printf("%s: %s\n", name, ok ? "PASS" : "FAIL");
    if (!ok) ++failures;
}
template<class T> void fire(Event<T>& event) {
    if constexpr (std::is_void_v<T>) event.notify();
    else { T value{}; event.notify(value); }
}
template<class T> struct Receiver {
    std::vector<int>& order;
    int tag;
    void call(T&) { order.push_back(tag); }
};
template<> struct Receiver<void> {
    std::vector<int>& order;
    int tag;
    void call() { order.push_back(tag); }
};

// Each of the six forms is exercised with omission and every explicit integer,
// for Inline and Main (already on the main thread). No integer is a sentinel.
// Deprecated forms are deliberately called, with diagnostics scoped to this test.
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#elif defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4996)
#endif
template<class T> void overloads(int defaultPriority, bool generic = false) {
    for (int form = 0; form != 6; ++form) {
        for (Deliver delivery : {Deliver::Inline, Deliver::Main}) {
            for (bool omitted : {false, true}) {
                for (int priority : {std::numeric_limits<int>::lowest(), -20, 0, 100, 200,
                                     std::numeric_limits<int>::max()}) {
                    auto event = generic ? std::make_unique<Event<T>>() : std::make_unique<Event<T>>(defaultPriority);
                    std::vector<int> order;
                    Receiver<T> receiver{order, 1};
                    typename Event<T>::Callback callback = [&](auto&...) { order.push_back(1); };
                    const int effective = omitted ? defaultPriority : priority;
                    // Register probes first so equal-priority behavior is tested too.
                    std::vector<EventListener> probes;
                    std::vector<std::pair<int, int>> expected;
                    for (int probe : {std::numeric_limits<int>::lowest(), -20, 0, 37, 100, 200,
                                      std::numeric_limits<int>::max()}) {
                        const int tag = static_cast<int>(probes.size()) + 10;
                        probes.push_back(event->listen([&, tag](auto&...) { order.push_back(tag); }, probe));
                        expected.emplace_back(probe, tag);
                    }
                    EventListener target;
                    switch (form) {
                    case 0: target = omitted ? event->listen(callback) : event->listen(callback, priority); break;
                    case 1: target = omitted ? event->listen(callback, delivery) : event->listen(callback, delivery, priority); break;
                    case 2: target = omitted ? event->listen(&receiver, &Receiver<T>::call) : event->listen(&receiver, &Receiver<T>::call, priority); break;
                    case 3: target = omitted ? event->listen(&receiver, &Receiver<T>::call, delivery) : event->listen(&receiver, &Receiver<T>::call, delivery, priority); break;
                    case 4:
                        if (omitted) event->listen(target, callback);
                        else event->listen(target, callback, priority);
                        break;
                    case 5:
                        if (omitted) event->listen(target, &receiver, &Receiver<T>::call);
                        else event->listen(target, &receiver, &Receiver<T>::call, priority);
                        break;
                    }
                    expected.emplace_back(effective, 1);
                    std::stable_sort(expected.begin(), expected.end(), [](auto a, auto b) { return a.first < b.first; });
                    std::vector<int> wanted;
                    for (auto entry : expected) wanted.push_back(entry.second);
                    fire(*event);
                    if (order != wanted) {
                        check("overload priority / stable registration order", false);
                        return;
                    }
                }
            }
        }
    }
    check(std::is_void_v<T> ? "Event<void>: all six forms" : "Event<T>: all six forms", true);
}
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#elif defined(_MSC_VER)
#pragma warning(pop)
#endif

template<class T> void family(Event<T>& event, int expectedPriority, const char* label) {
    std::string order;
    auto explicitZero = event.listen([&](auto&...) { order += '0'; }, 0);
    auto omitted = event.listen([&](auto&...) { order += 'D'; });
    auto explicitMiddle = event.listen([&](auto&...) { order += '5'; }, 50);
    auto explicitHundred = event.listen([&](auto&...) { order += '1'; }, 100);
    fire(event);
    check(label, order == (expectedPriority == 0 ? "0D51" : "05D1"));
}
void coreFamilies() {
    CoreEvents hub;
#define APP_EVENT(name) family(hub.name, 0, "CoreEvents::" #name " defaults to 0")
    APP_EVENT(update); APP_EVENT(draw); APP_EVENT(exit);
    APP_EVENT(keyPressed); APP_EVENT(keyReleased);
    APP_EVENT(mousePressed); APP_EVENT(mouseReleased); APP_EVENT(mouseMoved);
    APP_EVENT(mouseDragged); APP_EVENT(mouseScrolled);
    APP_EVENT(touchPressed); APP_EVENT(touchMoved); APP_EVENT(touchReleased);
    APP_EVENT(windowResized); APP_EVENT(filesDropped);
#undef APP_EVENT
#define OTHER_EVENT(name) family(hub.name, 100, "CoreEvents::" #name " defaults to 100")
    OTHER_EVENT(setup); OTHER_EVENT(onRender); OTHER_EVENT(afterFrame);
    OTHER_EVENT(hotReloadUnload); OTHER_EVENT(exitRequested); OTHER_EVENT(deviceLost);
    OTHER_EVENT(clipboardPasted); OTHER_EVENT(console); OTHER_EVENT(rawEvent);
#undef OTHER_EVENT
    family(AudioEngine::getInstance().audioOut, audio::priority::Generator, "audioOut defaults to Generator (100)");
    family(AudioEngine::getInstance().audioIn, 100, "audioIn defaults to 100");
}

// Framework consumers keep their precedence; framework entries that used the
// old default now specify 100. Probe an explicit 0 registered between two such
// entries: the base order is b, a, c. User omission changes are tested separately.
void productionOrder() {
    CoreEvents hub;
    std::string order;
    bool gizmoCaptures = false, imguiCaptures = false;
    auto first = hub.mousePressed.listen([&](auto&) { order += 'a'; }, EventPriority::App);
    auto second = hub.mousePressed.listen([&](auto&) { order += 'b'; }, EventPriority::BeforeApp);
    auto third = hub.mousePressed.listen([&](auto&) { order += 'c'; }, EventPriority::App);
    auto appPriority = hub.mousePressed.listen([&](auto&) { order += 'P'; }, EventPriority::App);
    auto after = hub.mousePressed.listen([&](auto&) { order += 'L'; }, EventPriority::AfterApp);
    auto imgui = hub.mousePressed.listen([&](auto& e) { order += 'I'; e.consumed = imguiCaptures; }, -20);
    auto gizmo = hub.mousePressed.listen([&](auto& e) { order += 'G'; e.consumed = gizmoCaptures; }, -30);
    fire(hub.mousePressed);
    check("framework pairs retain base priority order", order == "GIbacPL");
    order.clear(); imguiCaptures = true;
    fire(hub.mousePressed);
    check("ImGui blocks framework probes registered before it", order == "GI");
    order.clear(); gizmoCaptures = true;
    fire(hub.mousePressed);
    check("gizmo precedes ImGui and framework probes", order == "G");
}

void userDefaultOrder() {
    // Model the old per-event default alongside the new CoreEvents default.
    for (int defaultPriority : {100, 0}) {
        Event<MouseEventArgs> event(defaultPriority);
        std::string order;
        auto first = event.listen([&](auto&) { order += 'a'; });
        auto zero = event.listen([&](auto&) { order += 'b'; }, 0);
        fire(event);
        check(defaultPriority == 100 ? "base: default then explicit 0 is ba"
                                     : "new: default then explicit 0 ties in registration order ab",
              order == (defaultPriority == 100 ? "ba" : "ab"));
    }
    for (int defaultPriority : {100, 0}) {
        Event<MouseEventArgs> event(defaultPriority);
        std::string order;
        auto hundred = event.listen([&](auto&) { order += 'P'; }, 100);
        auto consumingDefault = event.listen([&](auto& e) { order += 'D'; e.consumed = true; });
        fire(event);
        check(defaultPriority == 100 ? "base: earlier explicit 100 runs before consuming default PD"
                                     : "new: consuming default pre-empts earlier explicit 100 D",
              order == (defaultPriority == 100 ? "PD" : "D"));
    }
}

std::string touchOrder;
class TouchApp : public App {
public:
    void setup() override {
        pressed = events().touchPressed.listen([](auto&) { touchOrder += 'B'; }, 0);
        moved = events().touchMoved.listen([](auto&) { touchOrder += 'B'; }, 0);
        released = events().touchReleased.listen([](auto&) { touchOrder += 'B'; }, 0);
    }
    void touchPressed(const TouchEventArgs&) override { touchOrder += 'A'; }
    void touchMoved(const TouchEventArgs&) override { touchOrder += 'A'; }
    void touchReleased(const TouchEventArgs&) override { touchOrder += 'A'; }
private:
    EventListener pressed, moved, released;
};
void appTouchOrder() {
    // Exercise the real App listeners and setup registration without a display.
    (void)buildAppDescriptor<TouchApp>();
    // Model the runner opening its main Window without starting sokol.
    internal::WindowRequestAccess::bindForTests(*internal::mainWindow(), 101);
    internal::appSetupFunc();
    internal::appUpdateFunc();
    for (auto* event : {&events().touchPressed, &events().touchMoved, &events().touchReleased}) {
        touchOrder.clear();
        fire(*event);
        check("App touch listener vs explicit-0 setup listener keeps base order BA", touchOrder == "BA");
    }
    internal::appCleanupFunc();
}

} // namespace
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    overloads<int>(37); overloads<void>(37);
    overloads<int>(0); overloads<void>(0);
    overloads<int>(100, true); overloads<void>(100, true);
    coreFamilies(); productionOrder(); userDefaultOrder(); appTouchOrder();
    return failures ? 1 : 0;
}
