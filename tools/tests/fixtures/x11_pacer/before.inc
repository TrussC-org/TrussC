// Frozen production seams from e0936c83 (before #997 movable pacing).
// Retain unchanged: the single-visible-window native trace is the reference.
static void _sapp_tc_glx_set_swapinterval(_sapp_tc_window_t* w, int interval) {
    /* GLX_EXT_swap_control is per-drawable: each window gets its own interval.
       The MESA fallback is context-wide -- with more than one window it is set
       to 0 (never block) and the main tick self-heals to timer pacing. */
    if (_sapp_tc.SwapIntervalEXT) {
        _sapp_tc.SwapIntervalEXT(_sapp_tc.display, w->drawable, interval);
    } else if (_sapp_tc.SwapIntervalMESA) {
        _sapp_tc.SwapIntervalMESA(interval);
    }
}

static bool _sapp_tc_x11_make_current(_sapp_tc_window_t* w) {
    // Host services have no Window; use the existing idle drawable.
    if (!w) return sapp_tc_gl_activate_idle();
#if defined(SOKOL_GLES3)
    return eglMakeCurrent(_sapp_tc.egl_display, w->drawable, w->drawable, _sapp_tc.ctx) == EGL_TRUE;
#else
    return glXMakeCurrent(_sapp_tc.display, w->drawable, _sapp_tc.ctx) == True;
#endif
}

static void _sapp_tc_x11_swap(_sapp_tc_window_t* w) {
#if defined(SOKOL_GLES3)
    // A callback may have activated another window; present this window's surface.
    if (!_sapp_tc_x11_make_current(w)) return;
    eglSwapBuffers(_sapp_tc.egl_display, w->drawable);
#else
    glXSwapBuffers(_sapp_tc.display, w->drawable);
#endif
}

static void _sapp_tc_x11_set_swapinterval(_sapp_tc_window_t* w, int interval) {
#if defined(SOKOL_GLES3)
    _sapp_tc_x11_surface_scope restore;
    if (_sapp_tc_x11_make_current(w)) eglSwapInterval(_sapp_tc.egl_display, interval);
#else
    _sapp_tc_glx_set_swapinterval(w, interval);
#endif
}

static void _sapp_tc_x11_pace(_sapp_tc_window_t* w, double seconds) {
    w->earliest_next = _sapp_tc_now() + seconds;
}

static void _sapp_tc_x11_pace_periodic(_sapp_tc_window_t* w, double period) {
    const double now = _sapp_tc_now();
    double next = w->earliest_next + period;
    if (w->earliest_next <= 0.0 || next > now + 2.0 * period) {
        next = now + period;
    } else if (next <= now) {
        // Skip elapsed deadlines without moving the timer's phase to tick completion.
        next += (floor((now - next) / period) + 1.0) * period;
        // Rounding can land exactly on now; a skipped deadline must stay in the future.
        if (next <= now) next += period;
    }
    w->earliest_next = next;
}

static bool _sapp_tc_x11_window_due(_sapp_tc_window_t* w, double now) {
    if (!w || !w->ready || w->constructing || !w->drawable || w->in_tick) return false;
    return w->earliest_next <= now;
}

static void _sapp_tc_x11_tick(_sapp_tc_window_t* w) {
    if (w->constructing || w->in_tick || !w->drawable) return;
    // [TrussC #1263] Count both main presentation credits and fallback timer
    // ticks. Keep a partial count when the main is minimized or absent.
    if (!w->is_main && ++w->pacing_ticks < w->requested_swap_interval) {
        _sapp_tc_x11_pace_periodic(w, _sapp_tc.refresh_period);
        return;
    }
    w->pacing_ticks = 0;
    const sapp_window handle = {w->win_id};
    w->in_tick = true;
    struct TickGuard {
        sapp_window handle;
        ~TickGuard() { if (auto* live = _sapp_tc_lookup(handle)) live->in_tick = false; }
    } guard{handle};
    _sapp_tc_x11_surface_scope restore_surface;
    const double frame_start = _sapp_tc_now();
    // [TrussC #998] A present interval teaches the refresh estimate only when the
    // previous tick also presented: after a skipped tick it spans the app's own
    // frame rate (setFps below the display), not the display.
    const bool after_present = w->last_tick_presented;
    w->last_tick_presented = false;
    _sapp_tc_timing_update(&w->timing);
    w->frame_duration = w->timing.smooth_dt;
    const bool resized = _sapp_tc_x11_update_dimensions(w);
    if (resized || w->surface_resize_pending) {
        w->surface_resize_pending = false;
        if (!_sapp_tc_x11_resize_surface(w)) {
            sapp_destroy_window(handle);
            _sapp_tc.app.quit_ordered = true;
            return;
        }
        // DRI3 may still acquire an old-sized buffer on the first resize present.
        // Request a follow-up on the next tick, coalesced by the Runtime.
        if (!w->desc.hidden) {
            _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_TC_RESIZE_REDRAW_REQUESTED);
            // The event callback may have destroyed this window.
            if (_sapp_tc_lookup(handle) != w) return;
        }
        _sapp_tc_x11_app_event(w, SAPP_EVENTTYPE_RESIZED);
    }
    if (_sapp_tc_lookup(handle) != w) return;
    if (!w->draw_while_hidden && _sapp_tc_native_hidden(w)) {
        if (w->is_main) _sapp_tc_x11_init_client();
        if (_sapp_tc_lookup(handle) != w) return;
        if (w->desc.tick_cb) w->desc.tick_cb(handle, w->desc.user_data);
        if (_sapp_tc_lookup(handle) != w) return;
        ++w->frame_count;
        w->skip_present = false; // Consume the one-shot at the completed hidden tick boundary.
        _sapp_tc_x11_pace(w, _sapp_tc.refresh_period);
        return;
    }
    if (!_sapp_tc_x11_make_current(w)) {
        _sapp_tc_x11_pace(w, _sapp_tc.refresh_period);
        return;
    }
    if (w->is_main) _sapp_tc_x11_init_client();
    if (_sapp_tc_lookup(handle) != w) return;
    if (w->desc.tick_cb) w->desc.tick_cb(handle, w->desc.user_data);
    if (_sapp_tc_lookup(handle) != w) return;
    ++w->frame_count;
    bool presented = false;
    // Sample requested visibility after every client callback, just before swap.
    if (!w->skip_present && (w->draw_while_hidden || !w->desc.hidden)
#if defined(SOKOL_GLCORE)
        && !_sapp_tc_native_iconified(w)
#endif
    ) {
        _sapp_tc_x11_swap(w);
        presented = true;
    }
    w->skip_present = false;
    w->last_tick_presented = presented;
    if (w->swap_interval == 0) {
        _sapp_tc_x11_pace_periodic(w, _sapp_tc.refresh_period);
        return;
    }
    const double now = _sapp_tc_now();
#if defined(SOKOL_GLES3)
    if (!presented) {
        w->earliest_next = frame_start + 0.0166 * (double)w->swap_interval;
    } else {
        const double swap_dt = now - w->last_present;
        w->last_present = now;
        const double per_frame = swap_dt / (double)w->swap_interval;
        if (after_present && per_frame > 1.0 / 240.0 && per_frame < 1.0 / 30.0)
            _sapp_tc.refresh_period += 0.1 * (per_frame - _sapp_tc.refresh_period);
        w->earliest_next = 0.0;
    }
#else
    (void)frame_start;
    if (_sapp_tc_native_iconified(w)) {
        _sapp_tc_x11_pace(w, 0.0166 * (double)w->swap_interval);
    } else if (presented) {
        const double swap_dt = now - w->last_present;
        w->last_present = now;
        if (swap_dt > 0.002) {
            const double per_frame = swap_dt / (double)w->swap_interval;
            if (after_present && per_frame > 1.0 / 240.0 && per_frame < 1.0 / 30.0)
                _sapp_tc.refresh_period += 0.1 * (per_frame - _sapp_tc.refresh_period);
            w->earliest_next = 0.0;
        } else {
            _sapp_tc_x11_pace_periodic(w, _sapp_tc.refresh_period * (double)w->swap_interval);
        }
    } else {
        _sapp_tc_x11_pace(w, _sapp_tc.refresh_period * (double)w->swap_interval);
    }
#endif
}

static void _sapp_tc_x11_run_loop(void) {
    XFlush(_sapp_tc.display);

    while (!_sapp_tc.app.quit_ordered) {
        double now = _sapp_tc_now();
        double wake_at = now + 0.1;     /* robustness cap */
        bool any_due = false;
        for (size_t i = 0; i < _sapp_tc.window_slots; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (!w || !w->ready || !w->drawable || w->in_tick) continue;
            if (w->earliest_next > now) {
                if (w->earliest_next < wake_at) wake_at = w->earliest_next;
            } else {
                any_due = true;
            }
        }
        if (!any_due && !XPending(_sapp_tc.display)) {
#if defined(SOKOL_GLES3)
            _sapp_tc_x11_wait_until(wake_at);
#else
            struct pollfd pfd;
            pfd.fd = ConnectionNumber(_sapp_tc.display);
            pfd.events = POLLIN;
            pfd.revents = 0;
            const double timeout_s = wake_at - now;
            poll(&pfd, 1, (timeout_s > 0.0) ? (int)(timeout_s * 1000.0) + 1 : 0);
#endif
        }
        if (_sapp_tc.app.desc._tc_before_turn) {
            // Reload polling precedes queue processing and can release GPU resources.
            sapp_tc_gl_ensure_context();
            _sapp_tc.app.desc._tc_before_turn();
        }
        /* drain exactly the already-queued events (can't block: count bounded) */
        int count = XPending(_sapp_tc.display);
        // [TrussC #1012] GLX and EGL both admit input before the saved deadline.
        for (size_t i = 0; i < _sapp_tc.window_slots; ++i)
            if (_sapp_tc.windows[i]) _sapp_tc.windows[i]->input_delivered = false;
        while (count--) {
            XEvent event;
            XNextEvent(_sapp_tc.display, &event);
            _sapp_tc_x11_process_event(&event);
        }
        for (size_t i = 0; i < _sapp_tc.window_slots; ++i) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (w && w->input_delivered) w->earliest_next = 0.0;
        }
        /* tick every due window (re-fetch each slot: a tick or a processed
           event may have destroyed windows) */
        bool main_drives_secondaries = false;
        for (size_t i = 0; i < _sapp_tc.window_slots; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            // An earlier window's native work may have crossed this deadline.
            const double tick_now = _sapp_tc_now();
            if (w && w->visibility_op != _SAPP_TC_X11_VISIBILITY_NONE)
                _sapp_tc_x11_sync_visibility(w);
            // The launch/main window occupies the first slot. At refresh rate
            // the main pass is the admission clock, independent of timer phase.
            // Lower setFps requests are frame-skipped by the window callback;
            // native interval-zero ticks still supply its refresh-rate source.
            // A timer tick just before this main pass has already spent the
            // secondary's refresh credit; don't tick it again on the handoff.
            const bool follow_main = main_drives_secondaries && w && w != _sapp_tc.app.main &&
                w->swap_interval == 0 && w->ready && w->drawable && !w->in_tick &&
                tick_now - w->timing.last >= 0.5 * _sapp_tc.refresh_period;
            if (follow_main || _sapp_tc_x11_window_due(w, tick_now)) {
                const bool is_main = w == _sapp_tc.app.main;
                const double previous_present = w->last_present;
                // This tick spends the main pass's refresh credit, not a
                // periodic deadline. Re-arm the fallback timer from completion
                // so losing the main source cannot immediately replay a tick.
                if (follow_main) w->earliest_next = 0.0;
                _sapp_tc_x11_tick(w);
                // Re-fetch after callbacks. A visible interval-positive main
                // must present near refresh cadence; EGL can block in activation
                // or rendering instead of swap itself.
                if (is_main && _sapp_tc.app.main == w) {
                    const double main_period = w->last_present - previous_present;
                    main_drives_secondaries = w->swap_interval > 0 &&
                        !_sapp_tc_native_hidden(w) && !_sapp_tc_native_iconified(w) &&
                        w->last_present > previous_present &&
                        main_period >= 0.5 * _sapp_tc.refresh_period && main_period <= 1.5 * _sapp_tc.refresh_period;
                }
            }
        }
        if (_sapp_tc.app.desc._tc_after_turn) _sapp_tc.app.desc._tc_after_turn();
        XFlush(_sapp_tc.display);
        bool callbacks_active = _sapp_tc.app.desc._tc_callbacks_active && _sapp_tc.app.desc._tc_callbacks_active();
        for (size_t i = 0; i < _sapp_tc.window_slots; ++i)
            if (_sapp_tc.windows[i] && _sapp_tc.windows[i]->in_tick) callbacks_active = true;
        if (callbacks_active) continue;
        /* sapp_window_request_close() lands here, outside every tick and
           event: the same close_cb a WM_DELETE_WINDOW sends (re-fetch each
           slot: a close may destroy windows) */
        for (size_t i = 0; i < _sapp_tc.window_slots; i++) {
            _sapp_tc_window_t* w = _sapp_tc.windows[i];
            if (!w || !w->close_requested) continue;
            w->close_requested = false;
            _sapp_tc_x11_close(w);
        }
        /* the cancellable quit dance (upstream parity): WM_DELETE_WINDOW or
           sapp_request_quit() land here; sapp_quit() pre-sets quit_ordered */
        if (_sapp_tc.app.quit_requested && !_sapp_tc.app.quit_ordered) {
            _sapp_tc_x11_request_exit();
        }
    }
}

static _sapp_tc_window_t* _sapp_tc_x11_create_window(const _sapp_tc_native_desc* d) {
    _sapp_tc_x11_surface_scope restore;
    const size_t slot = _sapp_tc_x11_reserve_slot();
    if (slot == SIZE_MAX || _sapp_tc.next_id == UINT32_MAX) return NULL;
#if defined(SOKOL_GLES3)
    _sapp_tc_window_t* w = (_sapp_tc_window_t*)_sapp_tc_x11_alloc(sizeof(_sapp_tc_window_t), true);
#else
    _sapp_tc_window_t* w = new (std::nothrow) _sapp_tc_window_t();
#endif
    if (!w) return NULL;
    if (!_sapp_tc_next_window_id(&_sapp_tc.next_id, &w->win_id)) {
        _sapp_tc_x11_unwind(w);
        return NULL;
    }
    w->desc = d->window;
    w->events_enabled = d->events_enabled;
    w->swap_interval = d->swap_interval;
    w->draw_while_hidden = d->draw_while_hidden;
    w->trust_hidden_signals = _sapp_tc_x11_trust_hidden_signals();
    w->wake_on_input = d->swap_interval > 0;
    w->title_limit = d->title_limit;
    w->color_format = d->color_format;
    w->depth_format = d->depth_format;
    w->dpi_scale = w->desc.no_high_dpi ? 1.0f : _sapp_tc.dpi / 96.0f;
    if (!(w->dpi_scale > 0.0f) || !isfinite(w->dpi_scale)) w->dpi_scale = 1.0f;
    const char* title = w->desc.title ? w->desc.title : "TrussC";
    size_t length = strlen(title);
    if (w->title_limit && length >= w->title_limit) length = w->title_limit - 1;
    // Bound (compatibility): legacy title capacity preserves its byte prefix and stable descriptor pointer.
    const size_t title_bytes = w->title_limit ? w->title_limit : length + 1;
    w->title = (char*)_sapp_tc_x11_alloc(title_bytes, false);
    if (!w->title) { _sapp_tc_x11_unwind(w); return NULL; }
    memcpy(w->title, title, length); w->title[length] = 0;
    w->desc.title = w->title;
    if (!_sapp_tc_x11_create_native_window(w, w->title, w->desc.width,
            w->desc.height, w->desc.borderless) ||
        !_sapp_tc_x11_initialize_dimensions(w)) {
        _sapp_tc_x11_unwind(w);
        return NULL;
    }
    if (!_sapp_tc_x11_make_current(w)) {
        _sapp_tc_x11_unwind(w);
        return NULL;
    }
    _sapp_tc_x11_set_swapinterval(w, w->swap_interval);
    _sapp_tc_creation_transaction creation(w, [](_sapp_tc_window_t* record) {
        // A nested creation may grow the registry; resolve the slot again.
        for (size_t i = 0; i < _sapp_tc.window_slots; ++i)
            if (_sapp_tc.windows[i] == record) { _sapp_tc.windows[i] = nullptr; break; }
        _sapp_tc_x11_unwind(record);
    });
    _sapp_tc.windows[slot] = w;
    if (w->desc.bind_cb && !w->desc.bind_cb(w->win_id, w->desc.user_data)) return nullptr;
    w->ready = true;
    const sapp_window identity{w->win_id};
    w->fullscreen_requested = d->fullscreen;
    if (!w->desc.hidden) _sapp_tc_x11_show_window(w);
    if (_sapp_tc_lookup(identity) != w) { creation.removed(); return nullptr; }
    if (d->fullscreen && !w->desc.hidden) {
        _sapp_tc_x11_set_fullscreen(w, true);
        if (_sapp_tc_lookup(identity) != w) { creation.removed(); return nullptr; }
        _sapp_tc_x11_update_dimensions(w);
    }
    creation.commit();
    return w;
}
