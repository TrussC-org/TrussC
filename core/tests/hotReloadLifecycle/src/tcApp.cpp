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
