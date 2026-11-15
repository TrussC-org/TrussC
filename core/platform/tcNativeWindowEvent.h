// TrussC's private adapter to the native send gate. Included only after the
// sokol implementation, so no injection API or provenance lives in sokol.
#pragma once
namespace trussc::internal {
bool sendNativeWindowEvent(sapp_tc_window win, sapp_event* event) {
#if defined(_WIN32) || (defined(__APPLE__) && TARGET_OS_OSX) || (defined(__linux__) && !defined(__ANDROID__) && !defined(__EMSCRIPTEN__))
    auto* w = _sapp_tc_lookup(win);
#if defined(__APPLE__) && TARGET_OS_OSX
    if (w && w->native_closed) return false;
#endif
    if (!w || !event || !w->ready ||
        (w->close_recorded && w->close_recorded_frame == w->frame_count) ||
        !w->desc.event_cb || (w->events_enabled && !w->events_enabled())) return false;
    _sapp_tc_send(w, event);
    return true;
#elif defined(__EMSCRIPTEN__) || defined(__ANDROID__) || (defined(__APPLE__) && TARGET_OS_IPHONE)
    if (win.id || !event || !_sapp_tc.valid || !_sapp_tc.init_called || _sapp_tc.cleanup_called) return false;
    event->frame_count = _sapp_tc.frame_count;
    event->window_width = _sapp_tc.window_width; event->window_height = _sapp_tc.window_height;
    event->framebuffer_width = _sapp_tc.framebuffer_width; event->framebuffer_height = _sapp_tc.framebuffer_height;
    try { _sapp_tc_call_event(event); } catch (...) { return false; }
    return true;
#else
    (void)win; (void)event;
    return false;
#endif
}

}
