#include <TrussC.h>
#include "../../common/tcCoreTest.h"
#include <cstdio>
#include <type_traits>

namespace {
template<class T> concept HasWindowReserve = requires(T t) { t.uniformBufferReserve; t.reserveUniformBuffer(1); };
template<class T> concept HasClipboardSize = requires(T t) { t.clipboardSize; };
template<class T> concept HasClipboardSetter = requires(T t) { t.setClipboardSize(1); };
static_assert(!HasWindowReserve<tc::WindowSettings>);
static_assert(!HasClipboardSize<tc::WindowSettings> && !HasClipboardSetter<tc::WindowSettings>);
static_assert(std::is_aggregate_v<tc::RuntimeSettings>);
struct SmokeApp : tc::App {};
using WindowLauncher = int (*)(const tc::WindowSettings&);
using RuntimeLauncher = int (*)(const tc::WindowSettings&, const tc::RuntimeSettings&);
static_assert(std::is_same_v<decltype(static_cast<WindowLauncher>(&tc::runApp<SmokeApp>)), WindowLauncher>);
static_assert(std::is_same_v<decltype(static_cast<RuntimeLauncher>(&tc::runApp<SmokeApp>)), RuntimeLauncher>);
}

namespace {
void cursorHelpers() {
    unsigned char pixel[4] = {255, 255, 255, 255};
    tc::setCursor(tc::Cursor::Hand);
    tc::hideCursor(); tc::showCursor();
    tc::bindCursorImage(tc::Cursor::Custom0, 1, 1, pixel);
    tc::unbindCursorImage(tc::Cursor::Custom0);
    (void)tc::getCursor();
}
struct CursorLifetimeApp : tc::App {
    void setup() override { cursorHelpers(); }
    void update() override { tc::exitApp(); }
};
struct QueryWindow : tc::Window {
    QueryWindow(const char* title, unsigned id, bool hidden) {
        title_ = title;
        tc::internal::WindowRequestAccess::bindForTests(*this, id);
        auto& metadata = tc::internal::WindowRequestAccess::binding(*this).metadata;
        metadata.trust_hidden_signals = true; metadata.occluded = hidden;
    }
};
bool windowGlobals() {
    using namespace tc;
    getMainThreadId();
    internal::beginRuntimeRun(); internal::beginFrameworkLifetime();
    int errors = 0;
    auto logs = getLogger().onLog.listen([&](LogEventArgs& e) {
        if (e.level == LogLevel::Error) ++errors;
    });
    bool ok = getWindowTitle().empty() && !isWindowOccluded() && errors == 2;
    auto a = std::make_shared<QueryWindow>("First", 901, false);
    internal::retainRuntimeWindow(a);
    ok = ok && getWindowTitle() == "First" && !isWindowOccluded() && errors == 2;
    auto b = std::make_shared<QueryWindow>("Second", 902, true);
    internal::retainRuntimeWindow(b);
    ok = ok && getWindowTitle().empty() && !isWindowOccluded() && errors == 4;
    a->setApp<SmokeApp>(); b->setApp<SmokeApp>();
    internal::applyPendingAppForTests(*a); internal::applyPendingAppForTests(*b);
    for (auto* window : {a.get(), b.get()}) {
        internal::AppOwnerScope scope(internal::appOwner(*window->getApp()));
        ok = ok && getWindowTitle() == window->getTitle() && isWindowOccluded() == window->isOccluded();
    }
    ok = ok && errors == 4;
    internal::shutdownRuntime(); internal::endFrameworkLifetime();
    std::printf("runtimeSettings globals: %s (App ownership, sole/zero/two Windows, Errors)\n", ok ? "PASS" : "FAIL");
    return ok;
}
}
TC_CORE_TEST_MAIN(int argc, char** argv) {
#if defined(__linux__)
    if (argc > 1 && std::string(argv[1]) == "--cursor-lifetime") {
        cursorHelpers(); // Real core + native backend, before its display exists.
        // runapp-verdict: linux-returning-lifetime -- real X11 post-run cursor regression requires the display to be cleaned up
        const int result = tc::runApp<CursorLifetimeApp>(tc::WindowSettings{}.setSize(64, 64));
        cursorHelpers(); // X11 cleanup has cleared the display here.
        std::puts("runtimeSettings cursor lifetime: PASS (before/during/after runApp)");
        return result;
    }
#endif
    const bool globalsOk = windowGlobals();
    tc::WindowSettings window;
    tc::RuntimeSettings runtime;
    bool ok = globalsOk && runtime.uniformBufferReserve == 0 && &runtime.reserveUniformBuffer(4096) == &runtime;
    const auto desc = tc::buildAppDescriptor<SmokeApp>(window, runtime);
    ok = ok && tc::internal::gpuUniformBufferReserve == 4096 && desc.clipboard_size == 65536;
    tc::internal::shutdownRuntime();
    tc::internal::endFrameworkLifetime();
    tc::buildAppDescriptor<SmokeApp>(window);
    ok = ok && tc::internal::gpuUniformBufferReserve == 0;
    tc::internal::shutdownRuntime();
    tc::internal::endFrameworkLifetime();
    std::printf("runtimeSettings: %s (reservation forwarding/reset, fixed clipboard, runApp overloads)\n", ok ? "PASS" : "FAIL");
    return ok ? 0 : 1;
}
