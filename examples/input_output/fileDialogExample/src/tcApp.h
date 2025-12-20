#pragma once

#include "tcBaseApp.h"
using namespace tc;
using namespace std;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    // Dialog result
    FileDialogResult lastResult;
    std::string statusMessage = "Press keys to open dialogs";

    // Loaded image (if any)
    Image loadedImage;
    bool hasImage = false;
};
