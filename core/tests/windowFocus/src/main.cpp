// Focus changes reach the Window they belong to: the typed event first, then
// the App virtual, with isFocused() already updated. The other Window is not
// notified.
#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <string>
using namespace tc;
namespace {
int failures = 0;
Window* windows[2];
std::string order[2];   // per window: 'L' = listener, 'A' = App virtual, 'f'/'t' = value
void check(const char* label, bool okay) {
    std::printf("windowFocus: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
struct FocusApp : App {
    int index = 0;
    explicit FocusApp(int i) : index(i) {}
    void windowFocusChanged(bool focused) override {
        order[index] += 'A'; order[index] += focused ? 't' : 'f';
        check("App sees isFocused() already updated", getWindow() && getWindow()->isFocused() == focused);
    }
};
void feed(Window& window, sapp_event_type type) {
    sapp_event e{}; e.type = type;
    internal::dispatchWindowEventForTests(&e, window);
}
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto a = std::make_shared<Window>(), b = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*a, 101);
    internal::WindowRequestAccess::bindForTests(*b, 202);
    internal::retainRuntimeWindow(a); internal::retainRuntimeWindow(b);
    a->setApp<FocusApp>(0); b->setApp<FocusApp>(1);
    internal::applyPendingAppForTests(*a); internal::applyPendingAppForTests(*b);
    windows[0] = a.get(); windows[1] = b.get();
    EventListener listeners[2];
    for (int i = 0; i < 2; ++i)
        listeners[i] = windows[i]->events().focusChanged.listen([i](FocusEventArgs& e) {
            order[i] += 'L'; order[i] += e.focused ? 't' : 'f';
            check("listener sees isFocused() already updated", windows[i]->isFocused() == e.focused);
        });
    check("not focused before any event", !a->isFocused() && !b->isFocused());

    feed(*a, SAPP_EVENTTYPE_FOCUSED);
    check("FOCUSED reaches its Window: listener, then App", order[0] == "LtAt");
    check("other Window is not notified", order[1].empty());
    check("isFocused() follows each Window", a->isFocused() && !b->isFocused());

    order[0].clear();
    feed(*a, SAPP_EVENTTYPE_FOCUSED);
    check("repeated FOCUSED is not delivered twice", order[0].empty());

    feed(*a, SAPP_EVENTTYPE_UNFOCUSED); feed(*b, SAPP_EVENTTYPE_FOCUSED);
    check("UNFOCUSED then FOCUSED on the other Window", order[0] == "LfAf" && order[1] == "LtAt");
    check("focus moved to the other Window", !a->isFocused() && b->isFocused());

    // Listeners of the Window that did not change stay silent for its UNFOCUSED too.
    order[0].clear(); order[1].clear();
    feed(*a, SAPP_EVENTTYPE_UNFOCUSED);
    check("UNFOCUSED of an unfocused Window is not delivered", order[0].empty() && order[1].empty());

    internal::WindowRequestAccess::teardown(*b);
    check("closed Window is not focused", !b->isFocused());

    for (auto& l : listeners) l.disconnect();
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    return failures ? 1 : 0;
}
