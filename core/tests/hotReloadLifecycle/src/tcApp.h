#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// =============================================================================
// hotReloadLifecycle guest App
//
// Deliberately minimal: the regressions this test guards live in what the App
// BASE class touches on construction/destruction (AudioEngine listeners, the
// window-context root slot, Event COW listener lists). The extra update
// listener widens the churn on the global events() singleton.
//
// It also registers an MCP tool, a status entry and a status image, each
// capturing `this` — the usual app pattern. On reload the host must remove
// them before this App is deleted, or the old build's handlers stay listed
// and callable with a dangling `this` (#227).
//
// setup/draw/exit (tcApp.cpp) only run in `--app` mode (see main.cpp); the
// lifecycle cycles never call them. They use tcxImGui so the guest target is
// checked for addon include directories and for linking the addon archive.
// =============================================================================
class tcApp : public App {
public:
    tcApp() {
        updateListener_ = events().update.listen([this]() { ticks_++; });
        mcp::tool("guest_probe", "hotReloadLifecycle guest tool")
            .bind(std::function<json()>([this]() -> json { return json{{"ticks", ticks_}}; }));
        mcp::status("guest_status", std::function<double()>([this]() { return (double)ticks_; }));
        mcp::statusImage("guest_image", [this]() { (void)ticks_; return Pixels(); });
    }

    void setup() override;
    void draw() override;
    void exit() override;

private:
    EventListener updateListener_;
    int ticks_ = 0;
};
