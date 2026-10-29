#pragma once
#include "tcWindowGeometryState.h"
#include "tcAppConstruction.h"

namespace trussc {
class Window;
namespace internal {
struct Runtime;
Runtime& runtime();
class RuntimeActivityScope {
public:
    RuntimeActivityScope() noexcept;
    ~RuntimeActivityScope() noexcept;
    RuntimeActivityScope(const RuntimeActivityScope&) = delete;
};
// Per-window production operations are replaceable without creating a native
// window. No query may acquire a drawable, allocate GPU resources or call App code.
struct WindowNativeOperations {
    bool (*readMetadata)(sapp_window, void*, sapp_tc_window_metadata&);
    sg_swapchain (*acquireSwapchain)(sapp_window, void*);
    void (*activate)(sapp_window, void*);
    void (*setTitle)(Window&, const char*) = nullptr;
    void (*setSize)(Window&, double, double, bool) = nullptr;
    void (*setPosition)(Window&, int, int) = nullptr;
    IVec2 (*position)(const Window&) = nullptr;
    void (*setFullscreen)(Window&, bool) = nullptr;
    uint32_t (*fullscreenPhase)(const Window&) = nullptr;
    void (*toggleFullscreen)(Window&) = nullptr;
    bool (*closeRecorded)(sapp_window, void*) = nullptr;
    bool (*injectEvent)(sapp_window, void*, const sapp_event*) = nullptr;
};
struct WindowBinding {
    sapp_window handle{};
    const WindowNativeOperations* operations = nullptr;
    void* user = nullptr;
    sapp_tc_window_metadata metadata{};
    bool bound = false;
    // Last attempted aggregate visibility, initially visible. Snapshot publication
    // never consumes its delivery.
    bool lastDeliveredHidden = false;
    bool visibilityReady = false;
    uint64_t visibilityRevision = 0;
    WindowGeometryState geometry;
    bool geometryInitialized = false;
};
// Existing ending endpoints also exclude native ticks and process queue delivery.
// This only marks callback activity; App ending remains at its current endpoint.
class WindowLifecycleScope {
public:
    explicit WindowLifecycleScope(Window&) noexcept;
    ~WindowLifecycleScope() noexcept;
    WindowLifecycleScope(const WindowLifecycleScope&) = delete;
private:
    Window& window_;
};
// A receiver is retained until pending requests and caller restoration finish.
class WindowEntryScope {
public:
    explicit WindowEntryScope(Window& window, bool tick = false, bool bootstrap = false, bool pendingVisibility = true);
    ~WindowEntryScope() noexcept;
    explicit operator bool() const { return admitted_; }
    WindowEntryScope(const WindowEntryScope&) = delete;
private:
    Window& window_;
    std::shared_ptr<Window> keepAlive_;
    WindowContext* previous_;
    sgl_context previousSgl_{};
    sapp_tc_gl_state native_{};
    bool entered_ = false, admitted_ = false, dispatch_ = false, requestsReady_ = false;
    void leave() noexcept;
};
void mainWindowEvent(const sapp_event*, sapp_window, void*) noexcept;
void singleWindowEvent(const sapp_event*) noexcept;
void enterWindowEvent(const sapp_event*, sapp_window, void*) noexcept;
void convertWindowEvent(const sapp_event*, Window&);
const char* eventEntryName(const sapp_event*, bool);
void dispatchWindowEventForTests(const sapp_event*, Window&);
void dispatchWindowDisplayEventForTests(const sapp_event*, Window&);
bool enterWindowTick(Window&, sapp_window, std::chrono::steady_clock::time_point, double,
                     const WindowTickPresentation* = nullptr);
void windowTick(sapp_window, void*) noexcept;
void windowClosed(sapp_window, void*) noexcept;
void singleWindowFrame() noexcept;
void mainWindowBootstrap(void (*bootstrap)());
// Reconcile launch visibility; single-window lifecycle observations also use this hook.
void deliverPendingWindowVisibilityAtReadyEntry(Window&);
bool currentWindowCloseRecorded();
// Hooks bracket an actual native loop turn, including turns with no due window.
class HostTurnScope {
public:
    HostTurnScope() noexcept;
    ~HostTurnScope() noexcept;
    HostTurnScope(const HostTurnScope&) = delete;
};
bool hostCallbacksActive() noexcept;
void beforeHostTurn() noexcept;
void afterHostTurn() noexcept;
void setHostReloadPoll(void (*poll)()) noexcept;
struct HostServiceOperations {
    void (*queues)();
    void (*maintenance)();
};
HostServiceOperations& hostServiceOperationsForTests();
Window* mainWindow();
// Capture readers need dimensions by value, without changing unbound contexts.
struct WindowDimensions {
    float dpiScale;
    int fbWidth, fbHeight;
};
inline bool usesNativeWindowDimensions(const WindowContext&) noexcept;
inline WindowDimensions windowDimensions(const WindowContext&) noexcept;
WindowContext& nonWindowContext();
WindowContext* nonWindowContextIfInitialized() noexcept;
void nativeWindowSetTitle(Window&, const char*);
void nativeWindowSetSize(Window&, double, double, bool);
void nativeWindowSetPosition(Window&, int, int);
IVec2 nativeWindowPosition(const Window&);
void nativeWindowSetFullscreen(Window&, bool);
uint32_t nativeWindowFullscreenPhase(const Window&);
void nativeWindowToggleFullscreen(Window&);
const WindowNativeOperations& productionWindowOperations();
bool bindWindow(Window& window, sapp_window handle, const WindowNativeOperations& operations, void* user);
bool bindNativeWindow(uint32_t id, void* window) noexcept;
bool refreshWindowMetadata(Window& window);
bool refreshWindowEntryMetadata(Window& window);
void publishWindowMetadata(Window& window, const sapp_tc_window_metadata& metadata);
void syncWindowAppSize(Window& window, bool initial = false);
void refreshMainWindowMetadata();
// Single-window ports retain their native bootstrap and lifecycle boundary.
void bindSingleWindowMain();
}
}
