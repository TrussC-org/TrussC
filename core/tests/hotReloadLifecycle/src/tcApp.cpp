// The TC_HOT_RELOAD macro below does two jobs:
//  - at cmake configure time, its presence in a .cpp switches this project to
//    the host/guest split build (main.cpp -> host EXE, this file -> libguest)
//  - at compile time it emits the extern "C" create/destroy factories the
//    host's GuestLibrary resolves via dlsym/GetProcAddress
#include "tcApp.h"
#include <tcxImGui.h>   // guest-side addon include (addons.make)
using namespace tcx;

TC_HOT_RELOAD(tcApp)

void tcApp::setup() {
    setWindowTitle("hotReloadLifecycle");
    imguiSetup();
}

void tcApp::draw() {
    clear(0.12f);

    imguiBegin();
    ImGui::Begin("Hot reload");
    ImGui::Text("Edit src/tcApp.cpp and save to reload");
    ImGui::Text("ticks: %d", ticks_);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
    imguiEnd();
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
