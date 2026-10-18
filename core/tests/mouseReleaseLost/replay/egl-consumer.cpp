// Compile with GLES3 so the actual callback uses the EGL lock-state gate.
#include <TrussC.h>
#include <cstdio>
using namespace tc;
static bool replayLocked;
// The linked headless GLCORE library has no pointer-lock implementation.
// Replay the producer's query, with a synchronous setter for listener unlock.
extern "C" bool sapp_mouse_locked(void) { return replayLocked; }
extern "C" void sapp_lock_mouse(bool locked) { replayLocked = locked; }
struct Probe : RectNode {
    int releases = 0, drags = 0;
    Probe() { setSize(100, 100); enableEvents(); }
    bool onMousePress(const MouseEventArgs&) override { return true; }
    bool onMouseRelease(const MouseEventArgs&) override { ++releases; return true; }
    bool onMouseDrag(const MouseDragEventArgs&) override { ++drags; return true; }
};
struct TestApp : App {
    int releases = 0, drags = 0;
    void mouseReleased(const MouseEventArgs&) override { ++releases; }
    void mouseDragged(const MouseDragEventArgs&) override { ++drags; }
};
int main(int argc, char** argv) {
    if (argc != 2) return 1;
    FILE* wire = std::fopen(argv[1], "rb");
    if (!wire) return 1;
    internal::pixelPerfectMode() = true;
    static auto app = std::make_shared<TestApp>();
    app->RectNode::setSize(100, 100);
    auto probe = std::make_shared<Probe>(); app->addChild(probe);
    auto& ctx = internal::currentWindowContext(); ctx.rootNode = app;
    internal::appMousePressedFunc = [](const MouseEventArgs& e) { app->handleMousePressed(e); };
    internal::appMouseReleasedFunc = [](const MouseEventArgs& e) { app->handleMouseReleased(e); };
    internal::appMouseMovedFunc = [](const internal::MouseEventRaw& e) { app->handleMouseMoved(e); };
    internal::appMouseDraggedFunc = [](const internal::MouseEventRaw& e) { app->handleMouseDragged(e); };
    int releases = 0, drags = 0, failures = 0;
    auto lr = events().mouseReleased.listen([&](const MouseEventArgs&) { ++releases; });
    auto ld = events().mouseDragged.listen([&](const MouseDragEventArgs&) { ++drags; });
    auto check = [&](const char* label, bool ok) {
        std::printf("%s: %s\n", label, ok ? "PASS" : "FAIL");
        if (!ok) ++failures;
    };
    const sapp_event_type types[] = {SAPP_EVENTTYPE_MOUSE_DOWN, SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_EVENTTYPE_MOUSE_UP};
    sapp_event e{};
    for (auto type : types) {
        unsigned char locked = 0;
        if (std::fread(&locked, 1, 1, wire) != 1 || std::fread(&e, sizeof(e), 1, wire) != 1) return 1;
        replayLocked = locked != 0;
        check("production event type and lock state", e.type == type && replayLocked);
        internal::_event_cb(&e);
        if (type == SAPP_EVENTTYPE_MOUSE_MOVE) {
            check("raw motion has no button bits", (e.modifiers & SAPP_MODIFIER_LMB) == 0);
            check("locked move gives one drag through events, App and node", drags == 1 && app->drags == 1 && probe->drags == 1);
            check("locked move keeps the press and grab without a release", releases == 0 && app->releases == 0 && probe->releases == 0 && isMousePressed() && !ctx.grabbedNode.expired());
        }
    }
    check("UP releases exactly once through events, App and node", releases == 1 && app->releases == 1 && probe->releases == 1 && !isMousePressed() && ctx.grabbedNode.expired());
    check("UP preserves the single drag", drags == 1 && app->drags == 1 && probe->drags == 1);
    // Focus recovery must remain active even if the lock query is still true.
    e.type = SAPP_EVENTTYPE_MOUSE_DOWN; e.mouse_button = SAPP_MOUSEBUTTON_LEFT;
    internal::_event_cb(&e);
    e.type = SAPP_EVENTTYPE_UNFOCUSED; internal::_event_cb(&e); internal::_event_cb(&e);
    e.type = SAPP_EVENTTYPE_MOUSE_UP; internal::_event_cb(&e);
    check("locked focus recovery releases once and ignores late UP", releases == 2 && app->releases == 2 && probe->releases == 2 && !isMousePressed() && ctx.grabbedNode.expired());
    // Replay the same production motion, but unlock from inside raw notify.
    // Like the real EGL API, the substituted setter changes the query immediately.
    std::rewind(wire);
    bool listenerSawLockedRawMove = false;
    auto raw = events().rawEvent.listen([&](const sapp_event& event) {
        if (event.type == SAPP_EVENTTYPE_MOUSE_MOVE) {
            listenerSawLockedRawMove = sapp_mouse_locked() &&
                (event.modifiers & (SAPP_MODIFIER_LMB | SAPP_MODIFIER_RMB | SAPP_MODIFIER_MMB)) == 0;
            sapp_lock_mouse(false);
        }
    });
    for (auto type : types) {
        unsigned char locked = 0;
        if (std::fread(&locked, 1, 1, wire) != 1 || std::fread(&e, sizeof(e), 1, wire) != 1) return 1;
        if (type == SAPP_EVENTTYPE_MOUSE_DOWN) replayLocked = locked != 0;
        internal::_event_cb(&e);
        if (type == SAPP_EVENTTYPE_MOUSE_MOVE) {
            check("raw listener unlocks during locked motion", listenerSawLockedRawMove && !sapp_mouse_locked());
            check("listener unlock keeps one new drag through events, App and node", drags == 2 && app->drags == 2 && probe->drags == 2);
            check("listener unlock keeps press and grab until physical UP", releases == 2 && app->releases == 2 && probe->releases == 2 && isMousePressed() && !ctx.grabbedNode.expired());
        }
    }
    check("physical UP after listener unlock releases exactly once", releases == 3 && app->releases == 3 && probe->releases == 3 && drags == 2 && app->drags == 2 && probe->drags == 2 && !isMousePressed() && ctx.grabbedNode.expired());
    std::fclose(wire);
    internal::appMousePressedFunc = nullptr; internal::appMouseReleasedFunc = nullptr;
    internal::appMouseMovedFunc = nullptr; internal::appMouseDraggedFunc = nullptr;
    ctx.rootNode.reset();
    app.reset();
    return failures ? 1 : 0;
}
