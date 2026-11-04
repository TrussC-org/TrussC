#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// Each window shows one third of the wall picture.
constexpr int PANEL_W = 400;
constexpr int PANEL_H = 300;
constexpr int PANEL_COUNT = 3;

// Draw part `index` (0 = left, 1 = center, 2 = right) of the wall into the
// current window: the whole Fbo is drawn PANEL_COUNT windows wide and shifted
// left, so only this window's third lands inside it.
inline void drawWallPart(const Fbo& wall, int index) {
    const float w = (float)getWindowWidth();
    const float h = (float)getWindowHeight();
    setColor(1.0f);
    wall.draw(-index * w, 0, w * PANEL_COUNT, h);

    static const char* names[PANEL_COUNT] = {"left", "center", "right"};
    setColor(1.0f, 0.8f);
    drawBitmapString(names[index], 12, 20);
}

// The center and right windows. They only draw the shared Fbo; the main
// window renders it once per frame. GPU resources are shared by all windows
// (one sokol_gfx context), so no copy is needed.
class PanelApp : public App {
public:
    PanelApp(shared_ptr<Fbo> wall, int index) : wall_(std::move(wall)), index_(index) {}

    void draw() override {
        if (wall_->isAllocated()) drawWallPart(*wall_, index_);
    }

private:
    shared_ptr<Fbo> wall_;
    int index_;
};

// The main window: renders the wall and shows its left third.
class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    void arrange();

    shared_ptr<Fbo> wall_ = make_shared<Fbo>();
    vector<shared_ptr<Window>> panels_;   // center, right
    float t_ = 0.0f;
};
