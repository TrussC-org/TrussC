#include "TrussC.h"
#include "tcApp.h"

void tcApp::setup() {
    setWindowTitle("imguiExample");

    // ImGui を初期化
    imguiSetup();
}

void tcApp::draw() {
    // 背景色でクリア
    clear(clearColor[0], clearColor[1], clearColor[2]);

    // ImGui フレーム開始
    imguiBegin();

    // メインウィンドウ
    ImGui::Begin("TrussC + ImGui Demo");

    ImGui::Text("Welcome to TrussC with Dear ImGui!");
    ImGui::Spacing();

    // スライダー
    ImGui::SliderFloat("Slider", &sliderValue, 0.0f, 1.0f);

    // ボタン
    if (ImGui::Button("Click me!")) {
        counter++;
    }
    ImGui::SameLine();
    ImGui::Text("Counter: %d", counter);

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // 背景色ピッカー
    ImGui::ColorEdit3("Background", clearColor);

    // テキスト入力
    ImGui::InputText("Text", textBuffer, sizeof(textBuffer));

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    // デモウィンドウの表示切り替え
    ImGui::Checkbox("Show ImGui Demo Window", &showDemoWindow);

    ImGui::Spacing();

    // フレームレート表示
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);

    ImGui::End();

    // ImGui デモウィンドウ（オプション）
    if (showDemoWindow) {
        ImGui::ShowDemoWindow(&showDemoWindow);
    }

    // TrussC の描画（ImGui と共存可能）
    // スライダーの値で円のサイズを変更
    float circleSize = 50 + sliderValue * 100;
    setColor(1.0f, 0.78f, 0.4f);
    drawCircle(getWindowWidth() / 2, getWindowHeight() / 2, circleSize);

    // ImGui フレーム終了（描画）
    imguiEnd();
}

void tcApp::cleanup() {
    // ImGui を終了
    imguiShutdown();
}
