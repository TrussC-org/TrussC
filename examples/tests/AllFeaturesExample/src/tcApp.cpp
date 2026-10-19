#include "tcApp.h"

void tcApp::setup() {
    af::coverAll();   // compile/link coverage only; never runs
    imgui::imguiSetup();
}

void tcApp::draw() {
    clear(0.12f);

    pushMatrix();
    noFill();
    setColor(colors::white);
    translate(getWidth() / 2.0f, getHeight() / 2.0f);
    rotate(getElapsedTimef() * speed_);
    drawBox(200.0f);
    popMatrix();

    fill();
    setColor(colors::hotPink);
    drawCircle(120, getHeight() - 120, 40);

    setColor(colors::white);
    drawBitmapString("All Features (compile / link canary)", 10, 20);

    imgui::imguiBegin();
    ImGui::Begin("AllFeaturesExample");
    ImGui::SliderFloat("speed", &speed_, 0.0f, 3.0f);
    ImGui::Text("%s", getBackendName().c_str());
    ImGui::End();
    imgui::imguiEnd();
}
