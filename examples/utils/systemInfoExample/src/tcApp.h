#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;

    void touchPressed(const TouchEventArgs& touch) override;

private:
    // Helper to draw a labeled bar
    void drawBar(float x, float y, float w, float h, float value, const string& label);

    // Helper to draw a section header
    float drawSection(float x, float y, const string& title);
};
