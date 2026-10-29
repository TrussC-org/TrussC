// Native delivery shared by all desktop ports. No process storage.
#pragma once
#include <cmath>

// Every event type is classified explicitly, including the raw-only notices.
static inline bool _sapp_tc_event_wakes(sapp_event_type type) {
    switch (type) {
        case SAPP_EVENTTYPE_KEY_DOWN: case SAPP_EVENTTYPE_KEY_UP: case SAPP_EVENTTYPE_CHAR:
        case SAPP_EVENTTYPE_MOUSE_DOWN: case SAPP_EVENTTYPE_MOUSE_UP: case SAPP_EVENTTYPE_MOUSE_SCROLL:
        case SAPP_EVENTTYPE_MOUSE_MOVE: case SAPP_EVENTTYPE_MOUSE_ENTER: case SAPP_EVENTTYPE_MOUSE_LEAVE:
        case SAPP_EVENTTYPE_TOUCHES_BEGAN: case SAPP_EVENTTYPE_TOUCHES_MOVED:
        case SAPP_EVENTTYPE_TOUCHES_ENDED: case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
        case SAPP_EVENTTYPE_FOCUSED: case SAPP_EVENTTYPE_UNFOCUSED:
        case SAPP_EVENTTYPE_ICONIFIED: case SAPP_EVENTTYPE_RESTORED:
        case SAPP_EVENTTYPE_FILES_DROPPED: case SAPP_EVENTTYPE_CLIPBOARD_PASTED: return true;
        case SAPP_EVENTTYPE_INVALID: case SAPP_EVENTTYPE_RESIZED:
        case SAPP_EVENTTYPE_SUSPENDED: case SAPP_EVENTTYPE_RESUMED:
        case SAPP_EVENTTYPE_QUIT_REQUESTED: case SAPP_EVENTTYPE_TC_DEVICE_LOST:
        case _SAPP_EVENTTYPE_NUM: case _SAPP_EVENTTYPE_FORCE_U32: return false;
    }
    return false;
}
static inline bool _sapp_tc_key_defined(int key) {
    return key == SAPP_KEYCODE_SPACE || key == SAPP_KEYCODE_APOSTROPHE ||
        (key >= SAPP_KEYCODE_COMMA && key <= SAPP_KEYCODE_9) ||
        key == SAPP_KEYCODE_SEMICOLON || key == SAPP_KEYCODE_EQUAL ||
        (key >= SAPP_KEYCODE_A && key <= SAPP_KEYCODE_RIGHT_BRACKET) ||
        key == SAPP_KEYCODE_GRAVE_ACCENT ||
        (key >= SAPP_KEYCODE_WORLD_1 && key <= SAPP_KEYCODE_WORLD_2) ||
        (key >= SAPP_KEYCODE_ESCAPE && key <= SAPP_KEYCODE_END) ||
        (key >= SAPP_KEYCODE_CAPS_LOCK && key <= SAPP_KEYCODE_PAUSE) ||
        (key >= SAPP_KEYCODE_F1 && key <= SAPP_KEYCODE_F25) ||
        (key >= SAPP_KEYCODE_KP_0 && key <= SAPP_KEYCODE_KP_EQUAL) ||
        (key >= SAPP_KEYCODE_LEFT_SHIFT && key <= SAPP_KEYCODE_MENU);
}
static inline bool _sapp_tc_valid_injected_event(const sapp_event& e) {
    // Injection carries input only. OS state and borrowed drop/clipboard payloads remain native.
    switch (e.type) {
        case SAPP_EVENTTYPE_KEY_DOWN: case SAPP_EVENTTYPE_KEY_UP:
            // Bound (platform): injection rejects keys outside the SDK key domain before delivery.
            return _sapp_tc_key_defined(e.key_code);
        case SAPP_EVENTTYPE_CHAR:
            // Bound (encoding): text injection rejects values outside Unicode scalars before delivery.
            return e.char_code <= 0x10ffff && !(e.char_code >= 0xd800 && e.char_code <= 0xdfff);
        case SAPP_EVENTTYPE_MOUSE_DOWN: case SAPP_EVENTTYPE_MOUSE_UP:
            // Bound (platform): injection rejects buttons outside the SDK button domain before delivery.
            if (e.mouse_button < SAPP_MOUSEBUTTON_LEFT || e.mouse_button > SAPP_MOUSEBUTTON_MIDDLE) return false;
            [[fallthrough]];
        case SAPP_EVENTTYPE_MOUSE_MOVE: case SAPP_EVENTTYPE_MOUSE_ENTER: case SAPP_EVENTTYPE_MOUSE_LEAVE:
        case SAPP_EVENTTYPE_MOUSE_SCROLL:
            // Bound (numeric): injection rejects nonfinite mouse components before delivery or state mutation.
            return std::isfinite(e.mouse_x) && std::isfinite(e.mouse_y) &&
                std::isfinite(e.mouse_dx) && std::isfinite(e.mouse_dy) &&
                std::isfinite(e.scroll_x) && std::isfinite(e.scroll_y);
        case SAPP_EVENTTYPE_TOUCHES_BEGAN: case SAPP_EVENTTYPE_TOUCHES_MOVED:
        case SAPP_EVENTTYPE_TOUCHES_ENDED: case SAPP_EVENTTYPE_TOUCHES_CANCELLED:
            // Bound (numeric): injection rejects nonfinite touch-relative components before delivery or state mutation.
            if (!std::isfinite(e.mouse_dx) || !std::isfinite(e.mouse_dy)) return false;
            // Bound (platform): invalid touch counts reject injection before array reads or state mutation.
            if (e.num_touches < 0 || e.num_touches > SAPP_MAX_TOUCHPOINTS) return false;
            // Bound (numeric): injection rejects nonfinite touch positions before delivery or state mutation.
            for (int i = 0; i < e.num_touches; ++i)
                if (!std::isfinite(e.touches[i].pos_x) || !std::isfinite(e.touches[i].pos_y)) return false;
            return true;
        case SAPP_EVENTTYPE_INVALID: case SAPP_EVENTTYPE_RESIZED: case SAPP_EVENTTYPE_ICONIFIED:
        case SAPP_EVENTTYPE_RESTORED: case SAPP_EVENTTYPE_FOCUSED: case SAPP_EVENTTYPE_UNFOCUSED:
        case SAPP_EVENTTYPE_SUSPENDED: case SAPP_EVENTTYPE_RESUMED: case SAPP_EVENTTYPE_QUIT_REQUESTED:
        case SAPP_EVENTTYPE_CLIPBOARD_PASTED: case SAPP_EVENTTYPE_FILES_DROPPED: case SAPP_EVENTTYPE_TC_DEVICE_LOST:
        case _SAPP_EVENTTYPE_NUM: case _SAPP_EVENTTYPE_FORCE_U32: return false;
    }
    return false;
}

struct _sapp_tc_event_result {
    bool delivered = false;
    bool client_threw = false;
};

template<class Record, class Lookup>
static _sapp_tc_event_result _sapp_tc_deliver_event(Record* w, sapp_event* event, uint64_t frame, Lookup lookup) noexcept {
    if (!w || !event || !w->ready || !w->desc.event_cb || (w->events_enabled && !w->events_enabled())) return {};
    const auto id = w->win_id;
    const bool busy = w->in_tick;
    w->in_tick = true;
    event->frame_count = frame;
    event->window_id = id;
    event->window_width = w->win_width; event->window_height = w->win_height;
    event->framebuffer_width = w->fb_width; event->framebuffer_height = w->fb_height;
    event->dpi_scale = w->dpi_scale;
    if (_sapp_tc_event_wakes(event->type)) w->input_delivered = w->wake_on_input;
    _sapp_tc_event_result result{true, false};
    try { w->desc.event_cb(event, {id}, w->desc.user_data); }
    catch (...) { result.client_threw = true; } // Report failure without unwinding through an OS callback.
    if (auto* live = lookup(sapp_window{id})) live->in_tick = busy;
    return result;
}

// Adapt the single-window ports' existing state to the same delivery gate.
// The local record is only a view of that state; it owns no native resources.
template<class State>
static bool _sapp_tc_deliver_single_event(State& state, const sapp_event* input,
                                          bool locked, bool reliable) noexcept {
    struct Client {
        uint32_t win_id = 0;
        sapp_window_desc desc{};
        bool ready, in_tick = false, input_delivered = false, wake_on_input = false;
        bool (*events_enabled)() = nullptr;
        int win_width, win_height, fb_width, fb_height;
        float dpi_scale;
    } client{0, {}, !state.cleanup_called, false, false, false, nullptr,
        state.window_width, state.window_height, state.framebuffer_width, state.framebuffer_height, state.dpi_scale};
    client.desc.user_data = &state;
    if (state.desc.event_cb || state.desc.event_userdata_cb) {
        client.desc.event_cb = [](const sapp_event* event, sapp_window, void* user) {
            auto& receiver = *static_cast<State*>(user);
            if (receiver.desc.event_cb) receiver.desc.event_cb(event);
            else receiver.desc.event_userdata_cb(event, receiver.desc.user_data);
        };
    }
    sapp_event event = *input;
    event.pointer_locked = locked; event.reliable_button_bits = reliable;
    _sapp_tc_deliver_event(&client, &event, state.frame_count,
        [&](sapp_window handle) { return handle.id == 0 ? &client : nullptr; });
    const bool consumed = state.event_consumed;
    state.event_consumed = false;
    return consumed;
}
