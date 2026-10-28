#pragma once
#include <TrussC.h>
namespace tcCoreTest {
inline void dispatchMainEvent(const sapp_event* event) {
    auto* window = trussc::internal::mainWindow();
    trussc::internal::mainWindowEvent(event,
        trussc::internal::WindowRequestAccess::binding(*window).handle, window);
}
inline bool beginScheduledFrame(std::chrono::steady_clock::time_point now) {
    using namespace trussc::internal;
    sampleFrameTime(currentWindowContext());
    beforeHostTurn();
    if (!mainLoop().updateSyncedToDraw) runIndependentUpdates(now);
    const bool draw = mainLoopShouldDraw(now);
    afterHostTurn();
    return draw;
}
template<class F> void nativeBoundary(F&& work) noexcept {
    using namespace trussc::internal;
    // The test's C callback catches failures; production entry/request code must restore the caller.
    TeardownSteps steps;
    steps.run("test native callback", std::forward<F>(work));
}
}
