#pragma once
#include <TrussC.h>
#include "sokol/util/sokol_window_event.h"

namespace tcCoreTest {
// Virtual native producer for deterministic input tests. Calls the same gate
// and host client as native windows, without an OS event pump or GPU device.
struct InjectedWindow {
    uint32_t win_id = 701;
    sapp_window_desc desc{};
    bool ready = true, in_tick = false, input_delivered = false, wake_on_input = true;
    bool (*events_enabled)() = nullptr;
    int win_width = 640, win_height = 480, fb_width = 640, fb_height = 480;
    float dpi_scale = 1;
    trussc::Window& window;
    trussc::internal::WindowNativeOperations operations{};
    sapp_tc_window_metadata metadata{};
    bool closed = false, pointerLocked = false;
    explicit InjectedWindow(trussc::Window& target, uint32_t id = 701) : win_id(id), window(target) {
        desc.event_cb = trussc::internal::mainWindowEvent; desc.user_data = &window;
        metadata.fb_width = fb_width; metadata.fb_height = fb_height;
        metadata.width = win_width; metadata.height = win_height; metadata.dpi_scale = dpi_scale;
        metadata.sample_count = 1; metadata.trust_hidden_signals = true;
        metadata.reliable_button_bits = true; metadata.source_interval = 1.0 / 60;
        operations.readMetadata = [](sapp_window, void* user, sapp_tc_window_metadata& out) {
            out = static_cast<InjectedWindow*>(user)->metadata; return true;
        };
        operations.closeRecorded = [](sapp_window, void* user) { return static_cast<InjectedWindow*>(user)->closed; };
        operations.injectEvent = [](sapp_window handle, void* user, const sapp_event* input) {
            auto& n = *static_cast<InjectedWindow*>(user);
            if (handle.id != n.win_id || !input || !_sapp_tc_valid_injected_event(*input)) return false;
            sapp_event event = *input;
            event.source = input->source == SAPP_EVENTSOURCE_REPLAYED ? input->source : SAPP_EVENTSOURCE_INJECTED;
            return n.send(event);
        };
        trussc::internal::bindWindow(window, {win_id}, operations, this);
    }
    bool send(sapp_event event) {
        dpi_scale = metadata.dpi_scale;
        fb_width = metadata.fb_width; fb_height = metadata.fb_height;
        win_width = metadata.width; win_height = metadata.height;
        event.pointer_locked = event.pointer_locked || pointerLocked;
        event.reliable_button_bits = true;
        return _sapp_tc_deliver_event(this, &event, 123,
            [&](sapp_window h) { return h.id == win_id ? this : nullptr; }).delivered;
    }
    ~InjectedWindow() { trussc::internal::WindowRequestAccess::unbind(window); }
};
}
