// Regression from the C2 review: A unload must retain B’s addon hooks.
#include <TrussC.h>
#include <tcxNodeInspector.h>
#include <cstdio>
using namespace tc;
struct Empty : App {};
int main() {
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    auto a=std::make_shared<Window>(), b=std::make_shared<Window>();
    internal::WindowRequestAccess::bindForTests(*a, 812);
    internal::WindowRequestAccess::bindForTests(*b, 813);
    internal::retainRuntimeWindow(a); internal::retainRuntimeWindow(b);
    a->setApp<Empty>(); internal::applyPendingAppForTests(*a);
    b->setApp<Empty>(); internal::applyPendingAppForTests(*b);
    const auto generation = internal::nextAppGeneration();
    internal::appOwner(*a->getApp())->generation=generation;
    { internal::AppCallbackScope owner(internal::appOwner(*b->getApp()));
      tcx::nodeinspector::NodeInspector::setToggleKey(KEY_F1); }
    const auto before=b->events().keyPressed.listenerCount();
    internal::dispatchHotReloadUnload(generation);
    const auto after=b->events().keyPressed.listenerCount();
    std::printf("ADDON_PROBE non-reloading B key listeners: before=%zu after=%zu App alive=%d\n",before,after,!!b->getApp());
    const auto bGeneration = internal::nextAppGeneration();
    internal::appOwner(*b->getApp())->generation = bGeneration;
    internal::dispatchHotReloadUnload(bGeneration);
    const auto retired = b->events().keyPressed.listenerCount();
    std::printf("ADDON_PROBE own generation retires B key listeners: %zu -> %zu\n", after, retired);
    const auto exitBaseline = b->events().exit.listenerCount();
    { internal::AppCallbackScope owner(internal::appOwner(*b->getApp()));
      tcx::nodeinspector::NodeInspector::setToggleKey(KEY_F1); }
    b->destroyApp();
    internal::applyPendingAppForTests(*b);
    const bool endedCleanly = !b->getApp() && b->events().keyPressed.listenerCount() == retired &&
        b->events().exit.listenerCount() == exitBaseline;
    std::printf("ADDON_PROBE App ending retires protected exit hook=%d\n", endedCleanly);
    internal::shutdownRuntime();
    internal::endFrameworkLifetime();
    return before == 2 && after == before && retired == 1 && endedCleanly ? 0 : 1;
}
