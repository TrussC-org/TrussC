#pragma once

#include "tcBaseApp.h"

class tcApp : public tc::App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // ダイアログの結果
    tc::FileDialogResult lastResult;
    std::string statusMessage = "Press keys to open dialogs";

    // 読み込んだ画像（あれば）
    tc::Image loadedImage;
    bool hasImage = false;
};
