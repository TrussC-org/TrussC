// Internal native window contract. No TrussC types or process storage.
#ifndef SOKOL_WINDOW_NATIVE_INCLUDED
#define SOKOL_WINDOW_NATIVE_INCLUDED
#include <stdint.h>
#include "sokol_window_fullscreen.h"

struct _sapp_tc_timing_t {
    double last, ema, smooth_dt, dt;
};

// The platform constructor owns these fields until publication; afterwards the
// native window owns them until its unwinder runs. Native handles stay per OS.
struct _sapp_tc_native_window_t {
    uint32_t win_id;
    sapp_window_desc desc;
    uint64_t frame_count;
    bool is_main; // process quit/session authority only
    bool ready, in_tick, close_requested;
    bool skip_present, draw_while_hidden;
    bool trust_hidden_signals;
    bool iconified, occluded;
    int fb_width, fb_height, win_width, win_height;
    float dpi_scale;
    double earliest_next, last_present, frame_duration, last_tick_time;
    _sapp_tc_timing_t timing;
    _sapp_tc_fullscreen_state_t fullscreen_state;
    bool fullscreen;
    // Scheduling data: blocking presentation versus periodic timer admission.
    int swap_interval;
    bool wake_on_input, input_delivered;
    bool (*events_enabled)();
    bool fallback_active;
    double refresh_period;
    // Owned initial/legacy title. Limit includes terminator; handle-based updates bypass it.
    char* title;
    size_t title_limit;
    sapp_pixel_format color_format, depth_format;
    double restore_x, restore_y, restore_width, restore_height;
    bool restore_valid;
};

// Every consumer uses effective visibility, including when old observations remain.
static inline bool _sapp_tc_native_iconified(const _sapp_tc_native_window_t* w) {
    return w->trust_hidden_signals && w->iconified;
}

static inline bool _sapp_tc_native_occluded(const _sapp_tc_native_window_t* w) {
    return w->trust_hidden_signals && w->occluded;
}

static inline bool _sapp_tc_native_hidden(const _sapp_tc_native_window_t* w) {
    return _sapp_tc_native_iconified(w) || _sapp_tc_native_occluded(w);
}

struct _sapp_tc_native_desc {
    sapp_window_desc window;
    int swap_interval;
    bool fullscreen;
    bool draw_while_hidden;
    size_t title_limit;
    sapp_pixel_format color_format, depth_format;
    float clear_color[4];
    bool clear_on_create;
    bool center_on_create;
};

// Preserve per-dimension startup defaults: non-positive main dimensions are
// passed to the OS; secondary descriptors keep the existing 640 x 480 fallback.
static inline sapp_window_desc _sapp_tc_normalize_main_window(const sapp_desc* d) {
    sapp_window_desc window = {};
    window.x = window.y = -1;
    window.width = d->width;
    window.height = d->height;
    window._tc_pixel_width = d->_tc_pixel_width;
    window._tc_pixel_height = d->_tc_pixel_height;
    window.sample_count = d->sample_count > 0 ? d->sample_count : 1;
    window.no_high_dpi = !d->high_dpi;
    window.title = d->window_title ? d->window_title : "sokol";
    return window;
}

static inline sapp_window_desc _sapp_tc_normalize_window(const sapp_window_desc* d) {
    sapp_window_desc window = *d;
    if (window.width <= 0) window.width = 640;
    if (window.height <= 0) window.height = 480;
    if (window.sample_count <= 0) window.sample_count = 1;
    return window;
}

static inline bool _sapp_tc_next_window_id(uint32_t* counter, uint32_t* id) {
    // Bound (identity): exhausted creation identities must never wrap or be reused.
    if (*counter == UINT32_MAX) return false;
    *id = ++*counter;
    return true;
}
#endif
