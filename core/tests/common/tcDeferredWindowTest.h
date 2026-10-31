#pragma once
#include <TrussC.h>

namespace tcCoreTest {
// CPU-only addressed storage. Tests simulate presentation explicitly; process
// maintenance always uses false, exactly as the production host loop does.
inline std::shared_ptr<tc::Window> deferredWindow() {
    auto window = tc::internal::prepareLaunchWindow(tc::WindowSettings{});
    tc::internal::WindowRequestAccess::bindForTests(*window, 683);
    return window;
}
inline void drainDeferredFrame() {
    auto* window = tc::internal::resolveWindow();
    if (window) tc::mcp::internal::drainDeferredResponses(&window->context(), true,
        std::chrono::steady_clock::now(), false);
    tc::mcp::internal::drainDeferredResponses(nullptr, false);
}
}
