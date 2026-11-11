#pragma once

// Uses the probe's surfaceless Mesa context and addressed Window. Input goes
// through the real manager listener; only the window's frame admission is
// driven by hand, with the same redraw reservation as the Runtime.
namespace {
void eventDrivenRegression() {
    const auto savedLoop = internal::mainLoop();
    ImGuiManager ui;
    ui.setup();
    auto* ctx = ImGui::GetCurrentContext();
    ImGui::GetIO().IniFilename = nullptr;
    Fbo fbo;
    fbo.allocate(256, 256);
    ImVec2 buttonCenter;
    int clicks = 0, keyPresses = 0;
    auto frame = [&] {
        fbo.begin(0, 0, 0, 0);
        {
            auto scope = ui.scoped();
            check("event-driven UI frame accepted", bool(scope));
            ImGui::SetNextWindowPos({0, 0});
            ImGui::SetNextWindowSize({256, 256});
            ImGui::Begin("event-driven input", nullptr, ImGuiWindowFlags_NoSavedSettings);
            if (ImGui::Button("click")) ++clicks;
            buttonCenter = ImGui::GetItemRectMin();
            buttonCenter.x += 5;
            buttonCenter.y += 5;
            if (ImGui::IsKeyPressed(ImGuiKey_A, false)) ++keyPresses;
            ImGui::End();
        }
        fbo.end();
        internal::endGpuFrame();
    };
    auto send = [&](sapp_event_type type, sapp_keycode key = SAPP_KEYCODE_INVALID, float mouseOffset = 0) {
        sapp_event event{};
        event.type = type;
        event.mouse_button = SAPP_MOUSEBUTTON_LEFT;
        event.mouse_x = buttonCenter.x + mouseOffset;
        event.mouse_y = buttonCenter.y;
        event.key_code = key;
        events().rawEvent.notify(event);
    };
    auto drain = [&] {
        int frames = 0;
        const auto now = std::chrono::steady_clock::time_point{};
        while (internal::mainLoopShouldDraw(now) && frames < 16) {
            --internal::mainLoop().redrawCount;
            frame();
            ++frames;
        }
        return frames;
    };
    auto idle = [&] {
        check("input queue drained and redraw requests stopped",
            ctx->InputEventsQueue.empty() && internal::mainLoop().redrawCount == 0);
        check("idle scheduler admits zero frames", drain() == 0 && drain() == 0);
    };

    // Settle layout and hover before counting the batched button transitions.
    frame();
    frame();
    setFps(EVENT_DRIVEN);
    internal::mainLoop().redrawCount = 0; // Discard the mode switch's initial request.
    send(SAPP_EVENTTYPE_MOUSE_MOVE);
    check("forwarded hover requests one frame", drain() == 1);
    idle();
    for (int batch : {1, 3}) {
        const int before = clicks;
        for (int i = 0; i < batch; ++i) {
            send(SAPP_EVENTTYPE_MOUSE_DOWN);
            send(SAPP_EVENTTYPE_MOUSE_UP);
        }
        check("batched clicks coalesce to one initial redraw", internal::mainLoop().redrawCount == 1);
        check("each click batch drains in exactly two frames per click", drain() == batch * 2);
        check("every queued click reaches the button", clicks == before + batch);
        check("hover capture does not sustain redraws", ImGui::GetIO().WantCaptureMouse);
        idle();
    }
    send(SAPP_EVENTTYPE_MOUSE_DOWN);
    check("held press wakes one frame", drain() == 1 && ImGui::GetIO().MouseDown[0]);
    check("active widget does not sustain redraws", ImGui::IsAnyItemActive());
    idle();
    send(SAPP_EVENTTYPE_MOUSE_MOVE, SAPP_KEYCODE_INVALID, 10);
    check("drag motion wakes one frame", drain() == 1 &&
        ImGui::GetIO().MousePos.x == buttonCenter.x + 10 && ImGui::GetIO().MouseDown[0]);
    idle();
    send(SAPP_EVENTTYPE_MOUSE_UP);
    check("drag release wakes one frame", drain() == 1 && !ImGui::GetIO().MouseDown[0]);
    idle();
    for (int i = 0; i < 3; ++i) {
        send(SAPP_EVENTTYPE_KEY_DOWN, SAPP_KEYCODE_A);
        send(SAPP_EVENTTYPE_KEY_UP, SAPP_KEYCODE_A);
    }
    check("key sequence drains in six frames", drain() == 6 && keyPresses == 3);
    idle();

    for (float fps : {VSYNC, 60.0f}) {
        setFps(fps);
        internal::mainLoop().redrawCount = 0;
        send(SAPP_EVENTTYPE_MOUSE_DOWN);
        send(SAPP_EVENTTYPE_MOUSE_UP);
        check("other fps modes receive no input redraw request", internal::mainLoop().redrawCount == 0);
        frame();
        check("other fps modes receive no trickle redraw request", internal::mainLoop().redrawCount == 0);
        frame();
        check("other fps modes still drain input normally", ctx->InputEventsQueue.empty());
    }
    ui.shutdown();
    internal::mainLoop() = savedLoop;
}
} // namespace
