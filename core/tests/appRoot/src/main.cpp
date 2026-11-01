#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
using namespace tc;
namespace {
int failures = 0, exits = 0, cleanups = 0, childCleanups = 0;
void check(const char* label, bool okay) {
    std::printf("appRoot: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
struct Root : App {
    void setup() override { check("root published before setup", getRootNode() == this); }
    void exit() override { ++exits; check("root and Window detached before exit", !getRootNode() && !getWindow()); }
    void cleanup() override { ++cleanups; check("root stays detached through cleanup", !getRootNode() && !getWindow()); }
};
struct HeadlessRoot : Root { void update() override { requestExit(); } };
struct Child : Node { void cleanup() override { ++childCleanups; } };
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto window = std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*window, 101);
    internal::retainRuntimeWindow(window);
    window->setApp<Root>();
    check("pending candidate is not a root", !trussc::internal::WindowRequestAccess::context(*window).rootNode.lock());
    internal::applyPendingAppForTests(*window);
    std::weak_ptr<App> weak = window->getApp();
    {
        internal::AppOwnerScope owner(internal::appOwner(*window->getApp()));
        check("App token resolves root outside window callback", getRootNode() == window->getApp().get());
        window->getApp()->addChild(std::make_shared<Child>());
        internal::WindowEntryScope entry(*window);
        window->events().update.notify();
    }
    window->destroyApp(); internal::applyPendingAppForTests(*window);
    check("tree cleanup and owner release finish together", weak.expired() && exits == 1 && cleanups == 1 && childCleanups == 1 && !trussc::internal::WindowRequestAccess::context(*window).rootNode.lock());
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    check("headless publishes and detaches the same root", runHeadlessApp<HeadlessRoot>() == 0 && exits == 2 && cleanups == 2 && !internal::nonWindowContext().rootNode.lock());
    return failures ? 1 : 0;
}
