#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("imguiExample");

    // Initialize ImGui
    imguiSetup();
}

void tcApp::draw() {
    // Clear with background color
    clear(clearColor[0], clearColor[1], clearColor[2]);

    // Begin ImGui frame
    imguiBegin();

    // Main window
    ImGui::Begin("TrussC + ImGui Demo");

    ImGui::Text("Welcome to TrussC with Dear ImGui!");
    ImGui::Spacing();

    // Slider
    ImGui::SliderFloat("Slider", &sliderValue, 0.0f, 1.0f);

    // Button
    if (ImGui::Button("Click me!")) {
        counter++;
    }
    ImGui::SameLine();
    ImGui::Text("Counter: %d", counter);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Background color picker
    ImGui::ColorEdit3("Background", clearColor);

    // Text input
    ImGui::InputText("Text", textBuffer, sizeof(textBuffer));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // Toggle demo window visibility
    ImGui::Checkbox("Show ImGui Demo Window", &showDemoWindow);

    ImGui::Spacing();

    // Display frame rate
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

    ImGui::End();

    // ImGui demo window (optional)
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    // TrussC drawing (can coexist with ImGui)
    // Change circle size based on slider value
    float circleSize = 50 + sliderValue * 100;
    setColor(1.0f, 0.78f, 0.4f);
    drawCircle(getWindowWidth() / 2, getWindowHeight() / 2, circleSize);

    // End ImGui frame (render)
    imguiEnd();
}

void tcApp::cleanup() {
    // Shutdown ImGui
    imguiShutdown();
}
