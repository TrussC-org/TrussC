#pragma once

#include "tcBaseApp.h"
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // ダイアログの結果
    FileDialogResult lastResult;
    std::string statusMessage = "Press keys to open dialogs";

    // 読み込んだ画像（あれば）
    Image loadedImage;
    bool hasImage = false;
};
