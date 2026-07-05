#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// Scene shown in the SECOND window. It has its own mouse state: getMouseX()
// etc. resolve to the window this node is drawn in, not the main window.
class SubScene : public RectNode {
public:
    Fbo* sharedFbo = nullptr;   // rendered by the MAIN window, drawn here too

    bool loggedFirstDraw = false;
    void draw() override {
        if (!loggedFirstDraw) {
            loggedFirstDraw = true;
            logNotice("SubScene") << "first draw in second window: "
                << getWindowWidth() << "x" << getWindowHeight()
                << " dpi=" << getDpiScale();
        }
        // The main window's FBO texture, used directly — one shared GPU
        // context means zero-copy sharing across windows.
        if (sharedFbo && sharedFbo->isAllocated()) {
            setColor(1.0f);
            sharedFbo->draw(20, 60, 200, 150);
            setColor(0.7f);
            drawBitmapString("live FBO from the MAIN window", 20, 230);
        }
        setColor(0.6f, 0.9f, 1.0f);
        drawBitmapString("second window: move the mouse here", 20, 30);

        // Bounds synced to the window size (resize the window to see it follow)
        setColor(0.3f, 0.5f, 0.3f);
        noFill();
        drawRect(1, 1, getWidth() - 2, getHeight() - 2);
        fill();
        drawBitmapString("root RectNode: " + toString((int)getWidth()) + "x" + toString((int)getHeight()),
                         20, getHeight() - 16);

        // This window's own mouse
        setColor(1.0f, 0.6f, 0.2f);
        drawCircle(getMouseX(), getMouseY(), 16);
    }
};

class tcApp : public App {
public:
    void setup() override;
    void update() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    Fbo fbo;
    float t = 0.0f;
    shared_ptr<Window> second;
    shared_ptr<SubScene> subScene;
};
