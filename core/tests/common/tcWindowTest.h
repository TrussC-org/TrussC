#pragma once
#include <TrussC.h>
namespace tcCoreTest {
// A deterministic addressed Window for GPU-free loop/query/capture fixtures.
// Tests explicitly destroy this receiver before starting a headless/new run.
class WindowFixture {
public:
    std::shared_ptr<trussc::Window> window;
    trussc::internal::WindowContext* previous = trussc::internal::currentWindowCtx();
    explicit WindowFixture(uint32_t id = 101) {
        using namespace trussc::internal;
        if (runtimeEnding()) { beginRuntimeRun(); beginFrameworkLifetime(); }
        window = std::make_shared<trussc::Window>();
        WindowRequestAccess::bindForTests(*window, id);
        currentWindowCtx() = &trussc::internal::WindowRequestAccess::context(*window);
    }
    ~WindowFixture() {
        trussc::internal::WindowRequestAccess::teardown(*window);
        trussc::internal::currentWindowCtx() = previous;
    }
};

inline void dispatchMainEvent(const sapp_event* event) {
    auto* context = trussc::internal::currentWindowCtx();
    auto* window = context && context->hostWindow ? context->hostWindow : trussc::internal::resolveWindow();
    trussc::internal::dispatchWindowEventForTests(event, *window);
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
