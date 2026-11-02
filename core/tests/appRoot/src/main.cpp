#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include "nodeSetupChecks.h"
#include <cstdio>
#include <string>
using namespace tc;
namespace {
int failures = 0, exits = 0, cleanups = 0, childCleanups = 0;
std::string setupOrder;   // App and child setup() order (#949)
void check(const char* label, bool okay) {
    std::printf("appRoot: %s: %s\n", label, okay ? "PASS" : "FAIL");
    if (!okay) ++failures;
}
struct Child : Node {
    void setup() override { setupOrder += "child "; }
    void cleanup() override { ++childCleanups; }
};
struct Root : App {
    void setup() override {
        check("root published before setup", getRootNode() == this);
        setupOrder += "app ";
        addChild(std::make_shared<Child>());
        setupOrder += "app-end ";
    }
    void exit() override { ++exits; check("root and Window detached before exit", !getRootNode() && !getWindow()); }
    void cleanup() override { ++cleanups; check("root stays detached through cleanup", !getRootNode() && !getWindow()); }
};
struct HeadlessRoot : Root { void update() override { exitApp(); } };
}
TC_CORE_TEST_MAIN() {
    getMainThreadId();
    nodesetup::run([](const std::string& label, bool okay) { check(label.c_str(), okay); });
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
        check("child added before the App starts is not set up", setupOrder.empty());
        internal::WindowEntryScope entry(*window);
        window->events().update.notify();
    }
    // The App becomes a live root on its first tick: its setup() first, a
    // child it adds there at once, then the child added before it started.
    check("App start: App, then its setup()'s child at once, then the earlier child",
          setupOrder == "app child app-end child ");
    window->destroyApp(); internal::applyPendingAppForTests(*window);
    check("tree cleanup and owner release finish together", weak.expired() && exits == 1 && cleanups == 1 && childCleanups == 2 && !trussc::internal::WindowRequestAccess::context(*window).rootNode.lock());
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    check("headless publishes and detaches the same root", runHeadlessApp<HeadlessRoot>() == 0 && exits == 2 && cleanups == 2 && !internal::nonWindowContext().rootNode.lock());
    return failures ? 1 : 0;
}
