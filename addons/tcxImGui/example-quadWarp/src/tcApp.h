#pragma once
#include <TrussC.h>
#include <tcxImGui.h>
#include <tcxQuadWarp.h>
#include <cfloat>
using namespace std;
using namespace tc;
using namespace tcx;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mouseMoved(Vec2 pos) override { pointerInput(pos, PointerAction::Hover); }
    void mouseDragged(Vec2 pos, int button) override {
        if (button == MOUSE_BUTTON_LEFT) pointerInput(pos, PointerAction::Drag);
    }
    void mousePressed(Vec2 pos, int button) override {
        if (button == MOUSE_BUTTON_LEFT) pointerInput(pos, PointerAction::Down);
    }
    void mouseReleased(Vec2 pos, int button) override {
        if (button == MOUSE_BUTTON_LEFT) pointerInput(pos, PointerAction::Up);
    }

private:
    enum class PointerAction { Hover, Down, Drag, Up, Cancel };
    bool screenToFbo(Vec2 screen, Vec2& fboPx);
    bool pointerInput(Vec2 screenPos, PointerAction action);

    QuadWarp warp_;
    Fbo fbo_;
    imgui::ImGuiManager ui_;
    ImGuiContext* uiContext_ = nullptr;
    Vec2 mappedPoint_{-FLT_MAX, -FLT_MAX};
    bool pointerDown_ = false, pressedLatch_ = false;
    bool cancelPending_ = false; // Release at an invalid position before same-frame reentry.
    Vec2 buttonCenter_;
    EventListener cancelListener_;
    float value_ = 0.5f;
    int clicks_ = 0;

    friend class SelfCheckApp;
};

// Kept separately so the reference example stays focused on rendering and input.
int runSelfCheck(const WindowSettings& settings);
