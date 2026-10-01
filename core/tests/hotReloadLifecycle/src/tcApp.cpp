// The TC_HOT_RELOAD macro below does two jobs:
//  - at cmake configure time, its presence in a .cpp switches this project to
//    the host/guest split build (main.cpp -> host EXE, this file -> libguest)
//  - at compile time it emits the extern "C" create/destroy factories the
//    host's GuestLibrary resolves via dlsym/GetProcAddress
#include "tcApp.h"
#include <tcxImGui.h>   // guest-side addon includes (addons.make)
#include <tcxNodeInspector.h>
using namespace tcx;
using tcx::nodeinspector::NodeInspector;

TC_HOT_RELOAD(tcApp)

void tcApp::setup() {
    ++setupCalls;
    audioOutHooksInSetup = (long)AudioEngine::getInstance().audioOut.listenerCount();
    audioInHooksInSetup = (long)AudioEngine::getInstance().audioIn.listenerCount();
    if (cycleOnly) return;
    setWindowTitle("hotReloadLifecycle");
    // Draws its Hierarchy panel at (10, 10) every frame from an onRender
    // listener, through tcxImGui (set up by attach()).
    NodeInspector::attach();
}

void tcApp::draw() {
    clear(0.12f);
    setColor(0.9f);
    drawBitmapString("Edit src/tcApp.cpp and save to reload", 280, 30);
    drawBitmapString("ticks: " + to_string(ticks_), 280, 50);
}

void tcApp::exit() {
    imguiShutdown();
}

// Settings app code writes (usually in setup()) that the core loop, the event
// callback, the atlas baker, ... read on the host side.
void tcApp::writeSharedState() {
    setFps(37.0f);
    redraw(3);
    setTouchAsMouse(false);
    setNearClip(0.25f);
    setFarClip(750.0f);
    setDefaultScreenFov(30.0f);
    setDataPathRoot("guest-data-root");
    setBeepVolume(0.37f);
    mcp::alert("hotReloadLifecycle guest alert");
    static const uint8_t box[13] = {0xFF, 0x81, 0x81, 0x81, 0x81, 0x81, 0x81,
                                    0x81, 0x81, 0x81, 0x81, 0x81, 0xFF};
    tc::bitmapfont::registerGlyph({0xE000, box, tc::bitmapfont::Width::Halfwidth});
    // What tcxImGui installs (guest code) for the host's node-tree hover
    tc::internal::overlayHoveredQuery() = []() { return true; };
    tc::internal::overlayFocusedQuery() = []() { return true; };
}

// State the host sets (launcher settings, the sokol_gl budget it grows, the
// bitmap-font sampler it creates, the secondary window it is ticking), as the
// guest's inline readers see it.
GuestView tcApp::readSharedState() {
    GuestView v;
    v.pixelPerfect = tc::internal::pixelPerfectMode();
    v.sglMaxVertices = tc::internal::sglBudget().maxVertices;
    v.fontSamplerReady = tc::internal::bitmapFontAtlas().initialized;
    v.windowContext = &tc::internal::currentWindowContext();
    return v;
}

// The singletons and GPU caches guest code reaches through their accessors.
// None of these calls touches the GPU: the caches are only looked up.
GuestInstances tcApp::sharedInstances() {
    GuestInstances g;
    g.audioEngine = &AudioEngine::getInstance();
    g.screenRecorder = &tc::internal::globalScreenRecorder();
    g.asyncScheduler = &tc::internal::AsyncScheduler::get();
    g.beepManager = &tc::internal::getManager();
    g.consoleRunning = &tc::console::detail::isRunning();
    g.pbrPipeline = &tc::internal::getPbrPipeline();
    g.pointPipeline = &tc::internal::getPointPipeline();
    g.fboShared = &tc::internal::fboSharedMap();
    g.fboSharedMip = &tc::internal::fboSharedMipMap();
    g.iblBake = &tc::internal::iblBakeResources();
    g.fontCache = &tc::internal::SharedFontCache::getInstance();
    g.fontSamplers = &tc::internal::fontSamplers();
    g.nodeCount = &tc::internal::nodeCount();
    g.textureCount = &tc::internal::textureCount();
    g.fboCount = &tc::internal::fboCount();
    g.asyncOwner = tc::internal::AsyncScheduler::newOwner();
    // callAfter() is inline, so this is the guest's copy of it: its id must
    // come from the host's sequence, or a node could hold a host timer and a
    // guest timer with the same id (cancelTimer(id) removes both).
    g.timerId = callAfter(3600.0, [] {});
    cancelTimer(g.timerId);
    return g;
}

// Work a guest worker thread (a network or timer callback, say) hands to the
// main thread: it must land in the queue the host's frame loop drains.
void tcApp::queueFromWorker(std::atomic<int>* ran) {
    std::thread worker([ran] { runOnMainThread([ran] { ++*ran; }); });
    worker.join();
}

// Window::setApp() is inline, so this runs the guest's copy of it: the
// double-attach guard it consults and adds to must be the one the host's
// close() removes from.
bool tcApp::attachApp(Window& window, std::shared_ptr<App> app) {
    window.setApp(app);
    return window.getApp() == app;
}

// The guard as guest code reads it: the host's release must show here.
bool tcApp::seesAttached(const App* app) {
    return trussc::internal::attachedApps().count(app) != 0;
}

void tcApp::useInspectorToggleKey() {
    NodeInspector::setToggleKey(KEY_F1);
}

// make_shared runs here, in the guest: the node's control block (and the code
// that frees it once the last weak reference goes) belongs to the guest.
std::shared_ptr<Node> tcApp::addGuestChild() {
    auto node = std::make_shared<RectNode>();
    addChild(node);
    return node;
}
