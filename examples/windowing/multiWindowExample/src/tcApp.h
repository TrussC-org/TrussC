#pragma once

#include <TrussC.h>
using namespace std;
using namespace tc;

// The SECOND window's content is a normal App subclass: setup/update/draw/
// keyPressed/windowResized all run against that window (its own mouse, its
// own size, its own event stream). GPU resources from the main window are
// usable directly — one shared sokol_gfx context.
class SubApp : public App {
public:
    Fbo* sharedFbo = nullptr;   // rendered by the MAIN window's app

    void setup() override {
        logNotice("SubApp") << "setup in second window: "
            << getWindowWidth() << "x" << getWindowHeight()
            << " dpi=" << getDpiScale();
    }

    void update() override {
        wobble += getDeltaTime() * TAU * 0.5f;
    }

    void draw() override {
        if (sharedFbo && sharedFbo->isAllocated()) {
            setColor(1.0f);
            sharedFbo->draw(20, 60, 200, 150);
            setColor(0.7f);
            drawBitmapString("live FBO from the MAIN window", 20, 230);
        }
        setColor(0.6f, 0.9f, 1.0f);
        drawBitmapString("second window (App lifecycle)", 20, 30);
        drawBitmapString("keys typed here: " + toString(keyCount), 20, 46);

        // App is a RectNode kept in sync with THIS window's size
        setColor(0.3f, 0.5f, 0.3f);
        noFill();
        drawRect(1, 1, getWidth() - 2, getHeight() - 2);
        fill();
        drawBitmapString("app size: " + toString((int)getWidth()) + "x" + toString((int)getHeight()),
                         20, getHeight() - 16);

        // This window's own mouse (wobbles from update())
        setColor(1.0f, 0.6f, 0.2f);
        drawCircle(getMouseX(), getMouseY() + sinf(wobble) * 6.0f, 16);
    }

    void keyPressed(int key) override {
        keyCount++;
        logNotice("SubApp") << "key in second window: " << key;
    }

    void windowResized(int w, int h) override {
        // RectNode size is already synced by the framework before this hook
        logNotice("SubApp") << "second window resized: " << w << "x" << h;
    }

private:
    float wobble = 0.0f;
    int keyCount = 0;
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
    shared_ptr<SubApp> subApp;
};
