#pragma once

#include <TrussC.h>
#include <tcxImGui.h>

using namespace std;
using namespace tc;
using namespace tcx;

// Dear ImGui in MULTIPLE windows: the main window and a secondary window each
// run their own ImGui context (imguiSetup() inside the App that drives the
// window binds imgui to THAT window). Panels, input capture and rendering are
// fully independent per window.

// The secondary window's App — its own imgui, its own state.
class SubApp : public App {
public:
    void setup() override {
        imgui::imguiSetup();   // binds to THIS window (called in its lifecycle)
    }
    void update() override {}
    void draw() override {
        clear(0.10f, 0.06f, 0.12f);
        imgui::imguiBegin();
        ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
        ImGui::Begin("Second window panel");
        ImGui::Text("This panel lives in the SECOND window.");
        ImGui::SliderFloat("hue", &hue_, 0.0f, 1.0f);
        ImGui::Checkbox("spin", &spin_);
        ImGui::End();
        imgui::imguiEnd();

        if (spin_) angle_ += 1.5f * (float)getDeltaTime();
        setColor(Color::fromHSB(hue_, 0.7f, 1.0f));
        pushMatrix();
        translate(getWidth() * 0.5f, getHeight() * 0.65f);
        rotate(angle_);
        drawRect(-60, -60, 120, 120);
        popMatrix();
    }
private:
    float hue_ = 0.6f;
    bool spin_ = true;
    float angle_ = 0.0f;
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void exit() override;

private:
    shared_ptr<Window> second_;
    shared_ptr<SubApp> subApp_;
    float mainHue_ = 0.1f;
};
