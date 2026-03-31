#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;
    void mousePressed(Vec2 pos, int button) override;

private:
    // Status
    string statusMessage = "Tap buttons or press keys";

    // Dialog result
    FileDialogResult lastResult;
    bool hasImage = false;
    Image loadedImage;

    // Button rects for touch input
    struct Button {
        float x, y, w, h;
        string label;
    };
    vector<Button> buttons;
    void setupButtons();
    int hitTest(float x, float y);
    void triggerAction(int index);
};
