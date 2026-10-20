#include "tcApp.h"
#include <cmath>

namespace {
bool completed = false;
int failures = 0;

void check(bool passed, const char* label) {
    if (passed) logNotice("self-check") << "PASS: " << label;
    else { logError("self-check") << "FAIL: " << label; ++failures; }
}

bool nearColor(Color actual, Color expected) {
    return abs(actual.r - expected.r) < 0.02f &&
           abs(actual.g - expected.g) < 0.02f &&
           abs(actual.b - expected.b) < 0.02f;
}
}

class SelfCheckApp : public tcApp {
public:
    void setup() override {
        tcApp::setup();
        checkListener_ = events().afterFrame.listen([this]() {
            if (frame_ != 6) return; // Also let draw() display the uploaded thumbnail.
            bool sized = pixels_.getWidth() == getFramebufferWidth() &&
                         pixels_.getHeight() == getFramebufferHeight();
            check(sized, "grab size equals framebuffer size");
            if (sized) {
                // draw() uses window coordinates; readback uses framebuffer pixels.
                int x = 100 * getFramebufferWidth() / getWidth();
                int y = 150 * getFramebufferHeight() / getHeight();
                check(nearColor(pixels_.getColor(x, y), Color(1, 0, 0)),
                      "rectangle interior is red");
                check(nearColor(pixels_.getColor(10, getFramebufferHeight() - 10),
                           Color(0.08f, 0.1f, 0.14f)),
                      "background color and readback orientation match");
            }
            check(!grabRequested_ && thumbnail_.isAllocated() &&
                  thumbnail_.getWidth() == pixels_.getWidth() &&
                  thumbnail_.getHeight() == pixels_.getHeight(),
                  "request consumed and pixels uploaded to thumbnail");
            completed = true;
            exitApp(failures ? 1 : 0);
        });
    }

    void draw() override {
        if (++frame_ == 5) keyPressed(' '); // Exercise the same request path as Space.
        tcApp::draw();
    }

private:
    int frame_ = 0;
    EventListener checkListener_;
};

int runSelfCheck(const WindowSettings& settings) {
    int result = TC_RUN_APP(SelfCheckApp, settings);
    if (!completed) { logError("self-check") << "Did not complete"; return 1; }
    return result;
}
