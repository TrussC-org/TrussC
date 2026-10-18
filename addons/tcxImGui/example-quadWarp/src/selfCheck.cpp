#include "tcApp.h"
#include <imgui/imgui_internal.h> // Test-only observation of ImGui's pending input count.
#include <cfloat>
#include <limits>

namespace {
bool completed = false;
int failures = 0;
void check(bool passed, const char* label) {
    if (passed) logNotice("self-check") << "PASS: " << label;
    else { logError("self-check") << "FAIL: " << label; ++failures; }
}
// Independent, known projective transform, including scale, shear and translation.
Vec2 project(Vec2 point) {
    float divisor = 1 + 0.001f * point.x + 0.002f * point.y;
    return {(100 + 2 * point.x + 0.2f * point.y) / divisor,
            (100 + 0.1f * point.x + 2 * point.y) / divisor};
}
}

class SelfCheckApp : public tcApp {
public:
    void update() override {
        // Establish the readback fixture after native window startup events.
        if (frame_ == 1) pointerInput(project(buttonCenter_), PointerAction::Hover);
        auto* previous = ImGui::GetCurrentContext();
        tcApp::update();
        if (uiContext_) peakQueued_ = (std::max)(peakQueued_, uiContext_->InputEventsQueue.Size);
        contextRestored_ &= ImGui::GetCurrentContext() == previous;
    }

    void draw() override {
        tcApp::draw();
        if (uiContext_) framesDrained_ &= uiContext_->InputEventsQueue.Size == 0;
        if (frame_ == 0) {
            check(uiContext_ != nullptr, "offscreen context initialized");
            for (int i = 0; i < 4; ++i) warp_.dstPoints[i] = project(warp_.srcPoints[i]);
            auto* previous = ImGui::GetCurrentContext();
            for (Vec2 point : {Vec2(2, 2), Vec2(160, 120), Vec2(318, 238), Vec2(48, 67)}) {
                check(pointerInput(project(point), PointerAction::Hover) && mappedPoint_.distance(point) < 0.01f,
                      "inverse warp recovers Fbo pixels");
            }
            for (Vec2 point : {Vec2(-30, 120), Vec2(350, 100), Vec2(120, -30), Vec2(120, 270)})
                check(!pointerInput(project(point), PointerAction::Hover), "outside quad rejected on each side");
            check(!pointerInput({numeric_limits<float>::quiet_NaN(), 100}, PointerAction::Hover), "non-finite input rejected");
            check(!pointerInput({2000, 100}, PointerAction::Hover), "projective horizon rejected");
            check(ImGui::GetCurrentContext() == previous, "input restores the previous context");
            keyPressed('w'); check(warp_.isInputEnabled(), "warp handles enabled");
            keyPressed('w'); check(!warp_.isInputEnabled(), "warp handles disabled");
        } else if (frame_ == 1) {
            Pixels pixels;
            pixels.allocate(fbo_.getWidth(), fbo_.getHeight(), 4);
            bool read = fbo_.readPixels(pixels.getData());
            check(read, "Fbo readback succeeds");
            if (read) {
                auto dot = pixels.getColor(int(mappedPoint_.x), int(mappedPoint_.y));
                check(std::abs(dot.r - 1) < 0.01f && std::abs(dot.g - 0.4f) < 0.01f &&
                      std::abs(dot.b - 0.15f) < 0.01f, "mapped cursor dot is rendered inside the Fbo");
            }
            pointerInput(project(buttonCenter_), PointerAction::Down);
        } else if (frame_ == 2) {
            pointerInput(project(buttonCenter_), PointerAction::Up);
        } else if (frame_ == 3) {
            check(clicks_ == 1, "warped pointer clicks the ImGui button exactly once");
            pointerInput(project(buttonCenter_), PointerAction::Down);
        } else if (frame_ == 4) {
            check(!pointerInput(project({-30, 120}), PointerAction::Up), "release outside rejected");
        } else if (frame_ == 5) {
            check(clicks_ == 1 && mappedPoint_.x == -FLT_MAX, "release outside cancels the click and hides the dot");
            auto* previous = ImGui::GetCurrentContext();
            ImGui::SetCurrentContext(uiContext_);
            check(!ImGui::GetIO().MouseDown[0], "release outside does not leave ImGui pressed");
            ImGui::SetCurrentContext(previous);
            checkCanceled("outside release invalidates mouse position");
            mousePressed(project(buttonCenter_), MOUSE_BUTTON_LEFT);
        } else if (frame_ == 6 || frame_ == 8) {
            check(uiContext_->IO.MouseDown[0] && mappedPoint_.x >= 0, "inside press reaches ImGui before cancellation");
            sapp_event event{};
            event.type = frame_ == 6 ? SAPP_EVENTTYPE_MOUSE_LEAVE : SAPP_EVENTTYPE_UNFOCUSED;
            events().rawEvent.notify(event); // Exercise the installed public listener.
            check(mappedPoint_.x == -FLT_MAX, "window cancellation hides the dot immediately");
        } else if (frame_ == 7) {
            checkCanceled("window leave releases button, invalidates position and hides dot");
            check(clicks_ == 1, "window leave cancels the widget click");
            mousePressed(project(buttonCenter_), MOUSE_BUTTON_LEFT);
        } else if (frame_ == 9) {
            checkCanceled("focus loss releases button, invalidates position and hides dot");
            check(clicks_ == 1, "focus loss cancels the widget click");
            burst();
        } else if (frame_ == 10) {
            check(uiContext_->IO.MouseDown[0] && !pointerDown_ && !pressedLatch_, "burst forwards a latched press with no held source");
            burst();
        } else if (frame_ == 11) {
            check(!uiContext_->IO.MouseDown[0] && !pointerDown_ && !pressedLatch_, "consecutive bursts leave the button released");
            burst();
        } else if (frame_ == 12) {
            check(uiContext_->IO.MouseDown[0], "next burst forwards one new press");
        } else if (frame_ == 13) {
            check(!uiContext_->IO.MouseDown[0] && !pointerDown_ && !pressedLatch_, "burst settles without a pending press");
            clicksAfterBurst_ = clicks_;
            pointerInput(project(buttonCenter_), PointerAction::Down);
            pointerInput(project(buttonCenter_), PointerAction::Up);
        } else if (frame_ == 14) {
            check(uiContext_->IO.MouseDown[0], "press and release between frames preserves the press");
        } else if (frame_ == 15) {
            check(!uiContext_->IO.MouseDown[0] && clicks_ == clicksAfterBurst_ + 1, "short press registers exactly one click");
            pointerInput(project(buttonCenter_), PointerAction::Down);
            pointerInput({}, PointerAction::Cancel); // A disappearing TUIO/OSC cursor uses this path.
        } else if (frame_ == 16) {
            checkCanceled("source cancellation clears an unforwarded press");
            rawMouse(SAPP_EVENTTYPE_MOUSE_DOWN);
            rawMouse(SAPP_EVENTTYPE_MOUSE_LEAVE);
            rawMouse(SAPP_EVENTTYPE_UNFOCUSED);
            rawMouse(SAPP_EVENTTYPE_FOCUSED);
            rawMouse(SAPP_EVENTTYPE_MOUSE_MOVE); // No button modifiers, but TrussC still dispatches a drag.
        } else if (frame_ == 17) {
            check(tc::internal::currentMouseButton == MOUSE_BUTTON_LEFT && !pointerDown_ && mappedPoint_.x >= 0,
                  "raw buttonless move after cancellation reaches the disarmed App drag handler");
            checkReleasedOutside("coalesced leave/refocus/move releases outside without a click");
            rawMouse(SAPP_EVENTTYPE_MOUSE_MOVE);
        } else if (frame_ == 18) {
            checkHover("later buttonless move stays hover without resurrecting the press");
            rawMouse(SAPP_EVENTTYPE_MOUSE_DOWN);
        } else if (frame_ == 19) {
            check(uiContext_->IO.MouseDown[0] && pointerDown_, "fresh raw down rearms and holds for a frame");
            pointerInput({}, PointerAction::Cancel);
            pointerInput(project(buttonCenter_), PointerAction::Hover);
        } else if (frame_ == 20) {
            checkReleasedOutside("held cursor disappearance survives same-frame hover reappearance");
            pointerInput(project(buttonCenter_), PointerAction::Drag);
        } else if (frame_ == 21) {
            checkHover("disappeared source stays disarmed on a later drag");
            pointerInput(project(buttonCenter_), PointerAction::Down);
        } else if (frame_ == 22) {
            check(uiContext_->IO.MouseDown[0] && pointerDown_, "fresh source down rearms the gesture");
            pointerInput(project(buttonCenter_), PointerAction::Up);
        } else if (frame_ == 23) {
            check(!uiContext_->IO.MouseDown[0] && clicks_ == clicksAfterBurst_ + 2, "fresh source gesture clicks exactly once");
            rawMouse(SAPP_EVENTTYPE_MOUSE_DOWN);
        } else if (frame_ == 24) {
            check(uiContext_->IO.MouseDown[0], "raw press held before outside release");
            rawMouse(SAPP_EVENTTYPE_MOUSE_UP, project({-30, 120}));
            rawMouse(SAPP_EVENTTYPE_MOUSE_MOVE);
        } else if (frame_ == 25) {
            checkReleasedOutside("outside raw release survives same-frame reentry", clicksAfterBurst_ + 2);
        } else if (frame_ == 26) {
            checkHover("hover position resumes one frame after canceled outside release", clicksAfterBurst_ + 2);
        } else if (frame_ == 31) {
            check(clicks_ == clicksAfterBurst_ + 2, "idle frames do not replay coalesced or canceled clicks");
            check(peakQueued_ <= 2 && peakQueued_ > 0 && framesDrained_,
                  "pending ImGui input stays at most two events and drains each frame");
            check(contextRestored_, "per-frame input forwarding restores the previous context");
            completed = true;
            exitApp(failures ? 1 : 0);
        }
        ++frame_;
    }
private:
    void rawMouse(sapp_event_type type) { rawMouse(type, project(buttonCenter_)); }
    void rawMouse(sapp_event_type type, Vec2 point) {
        sapp_event event{};
        event.type = type;
        float scale = tc::internal::pixelPerfectMode() ? 1 : sapp_dpi_scale();
        event.mouse_x = point.x * scale;
        event.mouse_y = point.y * scale;
        event.mouse_button = type == SAPP_EVENTTYPE_MOUSE_DOWN || type == SAPP_EVENTTYPE_MOUSE_UP
            ? SAPP_MOUSEBUTTON_LEFT : SAPP_MOUSEBUTTON_INVALID;
        event.modifiers = type == SAPP_EVENTTYPE_MOUSE_DOWN ? SAPP_MODIFIER_LMB : 0;
        tc::internal::_event_cb(&event); // Real runApp bindings dispatch through App::handleMouse*.
    }
    void checkReleasedOutside(const char* label, int clicks = -1) {
        const auto& io = uiContext_->IO;
        if (clicks < 0) clicks = clicksAfterBurst_ + 1;
        check(!io.MouseDown[0] && io.MousePos.x == -FLT_MAX && io.MousePos.y == -FLT_MAX &&
              !pointerDown_ && !pressedLatch_ && clicks_ == clicks, label);
    }
    void checkHover(const char* label, int clicks = -1) {
        const auto& io = uiContext_->IO;
        if (clicks < 0) clicks = clicksAfterBurst_ + 1;
        check(!io.MouseDown[0] && !pointerDown_ && mappedPoint_.x >= 0 &&
              Vec2(io.MousePos.x, io.MousePos.y).distance(buttonCenter_) < 0.01f && clicks_ == clicks, label);
    }
    void checkCanceled(const char* label) {
        const auto& io = uiContext_->IO;
        check(!io.MouseDown[0] && io.MousePos.x == -FLT_MAX && io.MousePos.y == -FLT_MAX &&
              mappedPoint_.x == -FLT_MAX && !pointerDown_ && !pressedLatch_, label);
    }
    void burst() {
        int queued = uiContext_->InputEventsQueue.Size;
        for (int i = 0; i < 10000; ++i) {
            Vec2 point = project(buttonCenter_ + Vec2(i % 3, 0));
            pointerInput(point, PointerAction::Down);
            pointerInput(point, PointerAction::Up);
        }
        check(uiContext_->InputEventsQueue.Size == queued && queued <= 2,
              "10000 down/up pairs keep the pending ImGui input small");
        check(!pointerDown_ && pressedLatch_ && mappedPoint_.x >= 0 && !cancelPending_,
              "burst retains a position and one press latch, with the source released");
    }
    int frame_ = 0;
    int peakQueued_ = 0;
    int clicksAfterBurst_ = 0;
    bool contextRestored_ = true;
    bool framesDrained_ = true;
};

int runSelfCheck(const WindowSettings& settings) {
    int result = TC_RUN_APP(SelfCheckApp, settings);
    if (!completed) { logError("self-check") << "Did not complete"; return 1; }
    return result;
}
