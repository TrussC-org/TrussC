#pragma once

#include <TrussC.h>
#include <cstdlib>   // getenv (AUTOTEST mode)
using namespace std;
using namespace tc;

// =============================================================================
// windowControlExample
// =============================================================================
// Exercises the per-window window-control API (fullscreen / screenshot /
// recording / fps) on a SECONDARY window, proving that the context-aware global
// functions (toggleFullscreen / saveScreenshot / startRecording) route to the
// window whose tick is currently running.
//
// The SECOND window's content is a normal App subclass (SubApp): its setup /
// update / draw / keyPressed all run against that window, with its own event
// stream and size. It has a BLUE background and a shape that moves every frame,
// so a recording clearly shows motion and screenshots are BLUE-dominant.
// =============================================================================

class SubApp : public App {
public:
    // Back-pointer to our own Window, so key handlers can drive Window::setFps
    // directly (fullscreen / screenshot / recording go through the context-aware
    // GLOBAL functions instead, which is the routing this example proves).
    Window* self = nullptr;

    void setup() override {
        autotest_ = (getenv("TC_WINCTRL_AUTOTEST") != nullptr);
        logNotice("SubApp") << "second window setup: "
            << getWindowWidth() << "x" << getWindowHeight()
            << (autotest_ ? "  [AUTOTEST]" : "");
    }

    void update() override {
        shapeT_ += getDeltaTime();
        if (autotest_) runAutotest();
    }

    void draw() override {
        // (Window clearColor already painted the BLUE background before draw.)
        // Moving shape so recordings show motion.
        float w = (float)getWindowWidth();
        float h = (float)getWindowHeight();
        float x = w * 0.5f + cosf(shapeT_ * TAU * 0.3f) * (w * 0.30f);
        float y = h * 0.5f + sinf(shapeT_ * TAU * 0.5f) * (h * 0.25f);
        setColor(1.0f, 0.85f, 0.2f);
        drawCircle(x, y, 34);

        // getFps() returns 0 for "free-run at vsync" — show that as "vsync"
        // rather than a bare 0, which reads like a stall.
        float fpsTarget = self ? self->getFps() : 0.0f;
        string fpsLabel = (fpsTarget > 0.0f) ? toString(fpsTarget) : string("vsync");

        setColor(1.0f);
        drawBitmapString("SECONDARY window (blue)", 20, 26);
        drawBitmapString("f: fullscreen (global toggleFullscreen -> this window)", 20, 46);
        drawBitmapString("s: saveScreenshot   r: record 3s   1/2/3: fps 15/30/vsync", 20, 62);
        drawBitmapString("size: " + toString(getWindowWidth()) + "x" + toString(getWindowHeight())
                         + "   fps target: " + fpsLabel, 20, 82);
        if (isRecording()) {
            setColor(1.0f, 0.3f, 0.3f);
            drawBitmapString("REC " + toString(recordingFrameCount()), 20, 102);
        }
    }

    void keyPressed(int key) override {
        // These run while THIS (secondary) window's context is current, so the
        // global functions below route to this window.
        if (key == 'F') {
            toggleFullscreen();   // global -> routes to this secondary window
            logNotice("SubApp") << "toggleFullscreen -> isFullscreen=" << isFullscreen();
        } else if (key == 'S') {
            saveScreenshot("winctrl_secondary.png");   // queued on THIS window
            logNotice("SubApp") << "saveScreenshot queued (secondary)";
        } else if (key == 'R') {
            if (isRecording()) {
                stopRecording();
                logNotice("SubApp") << "recording stopped";
            } else {
                startRecording("winctrl_secondary.mp4", 3.0f);   // 3s from this context
                logNotice("SubApp") << "recording started (3s, secondary)";
            }
        } else if (key == '1' && self) {
            self->setFps(15.0f);
        } else if (key == '2' && self) {
            self->setFps(30.0f);
        } else if (key == '3' && self) {
            self->setFps(0.0f);   // free-run at vsync
        }
    }

private:
    // -- AUTOTEST timeline (TC_WINCTRL_AUTOTEST=1): timed script, no keys. Each
    // step logs what it did + the size this secondary window sees in its draw. --
    void runAutotest() {
        autoTime_ += getDeltaTime();
        auto sz = [this]() {
            return toString(getWindowWidth()) + "x" + toString(getWindowHeight());
        };
        if (autoStep_ == 0 && autoTime_ >= 1.0f) {
            logNotice("winctrl-auto") << "t=1  marker  size=" << sz();
            saveScreenshot("winctrl_auto_pre.png");
            autoStep_ = 1;
        } else if (autoStep_ == 1 && autoTime_ >= 2.0f) {
            logNotice("winctrl-auto") << "t=2  setFullscreen(true)  size(before)=" << sz();
            setFullscreen(true);
            autoStep_ = 2;
        } else if (autoStep_ == 2 && autoTime_ >= 4.0f) {
            logNotice("winctrl-auto") << "t=4  setFullscreen(false)  size(fullscreen)=" << sz();
            setFullscreen(false);
            autoStep_ = 3;
        } else if (autoStep_ == 3 && autoTime_ >= 5.0f) {
            logNotice("winctrl-auto") << "t=5  marker  size(restored)=" << sz();
            saveScreenshot("winctrl_auto_post.png");
            autoStep_ = 4;
        } else if (autoStep_ == 4 && autoTime_ >= 6.0f) {
            logNotice("winctrl-auto") << "AUTOTEST DONE";
            autoStep_ = 5;
        }
    }

    bool  autotest_ = false;
    float shapeT_   = 0.0f;
    float autoTime_ = 0.0f;
    int   autoStep_ = 0;
};

class tcApp : public App {
public:
    void setup() override;
    void draw() override;
    void keyPressed(int key) override;

private:
    shared_ptr<Window> second_;
    shared_ptr<SubApp> subApp_;
};
