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
        case SAPP_EVENTTYPE_TC_REDRAW_REQUESTED:
        case SAPP_EVENTTYPE_TC_RESIZE_REDRAW_REQUESTED:
        case _SAPP_EVENTTYPE_NUM: case _SAPP_EVENTTYPE_FORCE_U32: return false;
    }
    return false;
}
// [TrussC] Shared native fork record.
struct _sapp_tc_event_result {
    bool delivered = false;
    bool client_threw = false;
};

// A process event has no native Window receiver, including after Window0.
template<class Descriptor>
static inline _sapp_tc_event_result _sapp_tc_deliver_runtime_event(const Descriptor& desc, bool initialized,
                                                                 const sapp_event& event) noexcept {
    if (!initialized || !desc._tc_event_cb) return {};
    _sapp_tc_event_result result{true, false};
    try { desc._tc_event_cb(&event, {0}, nullptr); }
    catch (...) { result.client_threw = true; }
    return result;
}

// Process quit uses the Runtime callback, never an initial native Window.
template<class Descriptor>
static inline _sapp_tc_event_result _sapp_tc_deliver_runtime_quit(const Descriptor& desc, bool initialized) noexcept {
    if (!initialized || !desc._tc_event_cb) return {};
    sapp_event event{};
    event.type = SAPP_EVENTTYPE_QUIT_REQUESTED;
    _sapp_tc_event_result result{true, false};
    try { desc._tc_event_cb(&event, {0}, nullptr); }
    catch (...) { result.client_threw = true; }
    return result;
}

template<class Record, class Lookup>
static _sapp_tc_event_result _sapp_tc_deliver_event(Record* w, sapp_event* event, uint64_t frame, Lookup lookup) noexcept {
    if (!w || !event || !w->ready || !w->desc.event_cb || (w->events_enabled && !w->events_enabled())) return {};
    const auto id = w->win_id;
    const bool busy = w->in_tick;
    w->in_tick = true;
    event->frame_count = frame;
    event->window_width = w->win_width; event->window_height = w->win_height;
    event->framebuffer_width = w->fb_width; event->framebuffer_height = w->fb_height;
    if (_sapp_tc_event_wakes(event->type)) w->input_delivered = w->wake_on_input;
    _sapp_tc_event_result result{true, false};
    try { w->desc.event_cb(event, {id}, w->desc.user_data); }
    catch (...) { result.client_threw = true; } // Report failure without unwinding through an OS callback.
    if (auto* live = lookup(sapp_tc_window{id})) live->in_tick = busy;
    return result;
}

// Adapt the single-window ports' existing state to the same delivery gate.
// The local record is only a view of that state; it owns no native resources.
template<class State>
static bool _sapp_tc_deliver_single_event(State& state, const sapp_event* input) noexcept {
    // [TrussC] Shared native fork record.
    struct Client {
        uint32_t win_id = 0;
        sapp_tc_window_desc desc{};
        bool ready, in_tick = false, input_delivered = false, wake_on_input = false;
        bool (*events_enabled)() = nullptr;
        int win_width, win_height, fb_width, fb_height;
    } client{0, {}, !state.cleanup_called, false, false, false, nullptr,
        state.window_width, state.window_height, state.framebuffer_width, state.framebuffer_height};
    client.desc.user_data = &state;
    if (state.desc.event_cb || state.desc.event_userdata_cb) {
        client.desc.event_cb = [](const sapp_event* event, sapp_tc_window, void* user) {
            auto& receiver = *static_cast<State*>(user);
            if (receiver.desc.event_cb) receiver.desc.event_cb(event);
            else receiver.desc.event_userdata_cb(event, receiver.desc.user_data);
        };
    }
    sapp_event event = *input;
    _sapp_tc_deliver_event(&client, &event, state.frame_count,
        [&](sapp_tc_window handle) { return handle.id == 0 ? &client : nullptr; });
    const bool consumed = state.event_consumed;
    state.event_consumed = false;
    return consumed;
}
