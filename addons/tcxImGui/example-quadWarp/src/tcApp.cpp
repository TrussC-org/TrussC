#include "tcApp.h"
#include <cfloat>
#include <cmath>

void tcApp::setup() {
    fbo_.allocate(320, 240);
    ui_.setupOffscreen();
    warp_.setup();
    warp_.setInputEnabled(false);
    warp_.setSourceRect(Rect(0, 0, 320, 240));
    warp_.dstPoints[0] = {100, 110};
    warp_.dstPoints[1] = {840, 85};
    warp_.dstPoints[2] = {900, 610};
    warp_.dstPoints[3] = {160, 540};
    // App/CoreEvents have no dedicated window-leave or focus-loss callback.
    cancelListener_ = events().rawEvent.listen([this](const sapp_event& event) {
        if (event.type == SAPP_EVENTTYPE_MOUSE_LEAVE || event.type == SAPP_EVENTTYPE_UNFOCUSED)
            pointerInput({}, PointerAction::Cancel);
    });
}

bool tcApp::screenToFbo(Vec2 screen, Vec2& fboPx) {
    // Keep the quad convex when editing its handles.
    Mat4 inverse;
    if (!warp_.getMatrix().tryInvert(inverse)) return false;
    Vec4 source = inverse * Vec4(screen.x, screen.y, 0, 1);
    if (!(source.w > 0)) return false;
    // Homogeneous coordinates need a projective divide, even for a 2D UI.
    Vec2 pixel(source.x / source.w, source.y / source.w);
    if (!std::isfinite(pixel.x) || !std::isfinite(pixel.y) ||
        pixel.x < 0 || pixel.y < 0 || pixel.x >= fbo_.getWidth() || pixel.y >= fbo_.getHeight()) return false;
    fboPx = pixel;
    return true;
}

bool tcApp::pointerInput(Vec2 screenPos, PointerAction action) {
    // A TUIO cursor, OSC message or touch point can call this instead of the mouse.
    // Send Cancel when the cursor disappears. Drag only keeps a press started by
    // Down, so a lost release (#896) cannot resurrect a canceled gesture.
    if (warp_.isInputEnabled() && action != PointerAction::Cancel) return false; // Warp handles own the pointer.
    if (action == PointerAction::Cancel || !screenToFbo(screenPos, mappedPoint_)) {
        mappedPoint_ = {-FLT_MAX, -FLT_MAX};
        pointerDown_ = pressedLatch_ = false;
        cancelPending_ = true;
        return false;
    }
    if (action == PointerAction::Down) {
        pointerDown_ = pressedLatch_ = true;
    } else if (action != PointerAction::Drag) {
        pointerDown_ = false;
    }
    return true;
}

void tcApp::update() {
    if (!uiContext_) return; // The first draw captures the offscreen context.
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(uiContext_);
    auto& io = ImGui::GetIO();
    // Per-frame forwarding keeps ImGui's input queue small.
    Vec2 position = cancelPending_ ? Vec2(-FLT_MAX, -FLT_MAX) : mappedPoint_;
    io.AddMousePosEvent(position.x, position.y);
    // A short click holds for one frame, then releases.
    io.AddMouseButtonEvent(0, !cancelPending_ && (pointerDown_ || (pressedLatch_ && !io.MouseDown[0])));
    if (!cancelPending_) pressedLatch_ = false;
    cancelPending_ = false;
    ImGui::SetCurrentContext(previous);
}

void tcApp::draw() {
    clear(0.08f, 0.09f, 0.12f);
    fbo_.begin(0.12f, 0.15f, 0.2f, 1);
    {
        auto scope = ui_.scoped();
        if (scope) {
            uiContext_ = ImGui::GetCurrentContext();
            ImGui::SetNextWindowPos({16, 16});
            ImGui::SetNextWindowSize({288, 208});
            ImGui::Begin("Warped UI", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                         ImGuiWindowFlags_NoCollapse);
            ImGui::TextUnformatted("Input mapped into Fbo pixels");
            if (ImGui::Button("Click me", {120, 30})) ++clicks_;
            auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            buttonCenter_ = {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f};
            ImGui::Text("Clicks: %d", clicks_);
            ImGui::SliderFloat("Value", &value_, 0, 1);
            ImGui::TextUnformatted("W: toggle warp handles");
            ImGui::End();
        }
    }
    if (mappedPoint_.x >= 0) {
        setColor(1, 0.4f, 0.15f);
        drawCircle(mappedPoint_.x, mappedPoint_.y, 3);
    }
    fbo_.end();

    pushMatrix();
    multMatrix(warp_.getMatrix());
    setColor(1);
    fbo_.draw(0, 0);
    popMatrix();
    if (warp_.isInputEnabled()) warp_.drawUI();
    setColor(1);
    drawBitmapString("Click the warped UI. W: toggle handles, then drag corners (keep the quad convex).", 20, 30);
}

void tcApp::keyPressed(int key) {
    if (key == 'w' || key == 'W') warp_.toggleInput();
    if ((key == 'w' || key == 'W') && warp_.isInputEnabled()) pointerInput({}, PointerAction::Cancel);
}
