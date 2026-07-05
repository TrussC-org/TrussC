#pragma once

// =============================================================================
// tcWindow.h - Secondary application windows (multi-window, Phase 1)
// =============================================================================
//
// One App, many windows: the main window keeps the classic implicit lifecycle
// (runApp / update / draw), a secondary Window owns its own Node tree, events
// and render state (WindowContext) and is driven by its display's own vsync
// (macOS: one CADisplayLink per window, delivered on the main run loop).
// Everything runs synchronously on the main thread; a fully occluded window
// simply skips drawable acquisition (fps 0), so it can never stall the others.
//
// GPU resources (Texture / Fbo / Mesh / Font) live in the single shared
// sokol_gfx context and can be used freely from any window.
//
// Platform support: macOS only for now. On other platforms createWindow()
// logs an error and returns nullptr.
//
// This file is included from TrussC.h (after Node / CoreEvents / WindowSettings).

namespace trussc {

class TC_PLATFORMS("macos") Window {
public:
    ~Window();

    // Attach the Node tree shown in this window (setup runs on first tick).
    void setRoot(std::shared_ptr<Node> root);
    std::shared_ptr<Node> getRoot() const { return root_; }

    // This window's event stream (mousePressed / keyPressed / draw / ...).
    CoreEvents& events() { return events_; }

    // Close the native window. The main window and other windows keep running.
    void close();
    bool isOpen() const { return native_ != nullptr; }

    void setTitle(const std::string& title);
    int getWidth() const;    // logical size (matches the window's coordinates)
    int getHeight() const;

    void setClearColor(const Color& c) { clearColor_ = c; }

    internal::WindowContext& context() { return ctx_; }

    // --- tree driving (called by the platform glue; friend access to Node) ---
    void dispatchMousePressToTree(const MouseEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchMousePress(e);
    }
    void dispatchMouseReleaseToTree(const MouseEventArgs& e) {
        if (ctx_.rootNode) ctx_.rootNode->dispatchMouseRelease(e);
    }
    void tickTree() {
        if (!ctx_.rootNode) return;
        ctx_.rootNode->updateTree();
        ctx_.rootNode->updateHoverState(ctx_.mouseX, ctx_.mouseY);
    }
    void drawTreeNow() {
        if (ctx_.rootNode) ctx_.rootNode->drawTree();
    }
    // Size-sync convention (mirrors the main App, which is a RectNode kept in
    // sync with the window): if the root IS a RectNode it is resized to the
    // window's logical size at attach time and on every resize/display change.
    // A plain Node root is left untouched.
    void syncRootSize(float wPts, float hPts) {
        if (auto* rect = dynamic_cast<RectNode*>(ctx_.rootNode)) {
            if (rect->getWidth() != wPts || rect->getHeight() != hPts) {
                rect->setSize(wPts, hPts);
            }
        }
    }

    // --- internal (used by the platform glue; not user API) ---
    Window();
    void* native_ = nullptr;                 // platform-side state
    internal::WindowContext ctx_;
    CoreEvents events_;
    internal::RenderContext render_;
    std::shared_ptr<Node> root_;
    bool rootSetupDone_ = false;
    Color clearColor_ = Color(0.05f, 0.05f, 0.08f, 1.0f);
};

// Create a secondary window. macOS only for now (nullptr elsewhere).
// The returned shared_ptr keeps the window alive; closing the window (user or
// close()) destroys the native side while the handle stays valid (isOpen()).
TC_PLATFORMS("macos") std::shared_ptr<Window> createWindow(const WindowSettings& settings = {});

inline Window::Window() {
    ctx_.isMain = false;
    ctx_.render = &render_;
    ctx_.coreEvents = &events_;
}

inline void Window::setRoot(std::shared_ptr<Node> root) {
    root_ = std::move(root);
    ctx_.rootNode = root_.get();
    rootSetupDone_ = false;
}

#if !defined(__APPLE__)
// Non-macOS stubs. The real implementations live in platform/mac/tcWindowMac.mm.
// (iOS also defines __APPLE__ but has no window glue — iOS is not in the CI
// build matrix; revisit if an iOS app ever links these symbols.)
inline Window::~Window() {}
inline void Window::close() {}
inline void Window::setTitle(const std::string&) {}
inline int Window::getWidth() const { return 0; }
inline int Window::getHeight() const { return 0; }
inline std::shared_ptr<Window> createWindow(const WindowSettings&) {
    logError("Window") << "createWindow(): secondary windows are only supported on macOS for now";
    return nullptr;
}
#endif

} // namespace trussc
