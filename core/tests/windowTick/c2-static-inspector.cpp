#include <TrussC.h>
#include <tcxNodeInspector.h>
#include <cstdio>
#include <cstdlib>
using namespace tc;
struct Empty : App {};
int main() {
    std::fputs("ENTERED MAIN\n", stderr);
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto w = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*w, 814);
    internal::retainRuntimeWindow(w);
    w->setApp<Empty>(); internal::applyPendingAppForTests(*w);
    {
        internal::AppCallbackScope owner(internal::appOwner(*w->getApp()));
        auto node = std::make_shared<Node>();
        w->getApp()->addChild(node);
        auto& inspector = tcx::nodeinspector::NodeInspector::instance();
        inspector.select(node.get());
        inspector.enableGizmoInput();
        MouseEventArgs e; e.pos = Vec2(20,0); e.button = 0;
        events().mousePressed.notify(e);
        std::fprintf(stderr, "STATIC_INSPECTOR dragging=%d consumed=%d\n", inspector.isGizmoDragging(), e.consumed);
        if (!inspector.isGizmoDragging()) return 22;
    }
    // Runtime's atexit handler precedes this lazily created singleton, so
    // singleton destruction runs first when normal stack cleanup is bypassed.
    std::exit(0);
}
