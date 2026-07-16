#include "tcApp.h"

void tcApp::setup() {
    mcp::registerDebuggerTools();   // AI-drivable (mouse/key injection)
    imgui::imguiSetup();   // main window's imgui

    // Open the second window immediately — it runs its own App + imgui.
    WindowSettings ws;
    ws.setSize(520, 380);
    ws.setTitle("imguiWindowsExample - second");
    second_ = createWindow(ws);
    if (second_) {
        subApp_ = make_shared<SubApp>();
        second_->setApp(subApp_);
    }
}

void tcApp::update() {}

void tcApp::draw() {
    clear(0.06f, 0.10f, 0.12f);

    imgui::imguiBegin();
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::Begin("Main window panel");
    ImGui::Text("This panel lives in the MAIN window.");
    ImGui::SliderFloat("hue", &mainHue_, 0.0f, 1.0f);
    if (ImGui::Button(second_ && second_->isOpen() ? "close second window"
                                                   : "open second window")) {
        if (second_ && second_->isOpen()) {
            second_->close();
        } else {
            WindowSettings ws;
            ws.setSize(520, 380);
            ws.setTitle("imguiWindowsExample - second");
            second_ = createWindow(ws);
            if (second_) {
                subApp_ = make_shared<SubApp>();
                second_->setApp(subApp_);
            }
        }
    }
    ImGui::End();
    imgui::imguiEnd();

    setColor(Color::fromHSB(mainHue_, 0.7f, 1.0f));
    drawCircle(getWidth() * 0.5f, getHeight() * 0.65f, 70);

    setColor(1.0f, 1.0f, 1.0f);
    drawBitmapString("imgui in two windows (W: toggle second)", 20, getHeight() - 24);
}

void tcApp::keyPressed(int key) {
    if (key == 'W') {
        if (second_ && second_->isOpen()) {
            second_->close();
        } else {
            WindowSettings ws;
            ws.setSize(520, 380);
            ws.setTitle("imguiWindowsExample - second");
            second_ = createWindow(ws);
            if (second_) {
                subApp_ = make_shared<SubApp>();
                second_->setApp(subApp_);
            }
        }
    }
}

void tcApp::exit() {
    if (second_ && second_->isOpen()) second_->close();
}
