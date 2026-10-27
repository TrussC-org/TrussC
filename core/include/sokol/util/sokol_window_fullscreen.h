#ifndef SOKOL_WINDOW_FULLSCREEN_INCLUDED
#define SOKOL_WINDOW_FULLSCREEN_INCLUDED
enum class _sapp_tc_fullscreen_phase { Windowed, Entering, Fullscreen, Exiting };

// Fullscreen notifications only update this plain state. Native callers toggle
// when request()/reconcile() returns true; reconciliation runs on a normal tick.
struct _sapp_tc_fullscreen_state_t {
    _sapp_tc_fullscreen_phase phase = _sapp_tc_fullscreen_phase::Windowed;
    bool desired = false;
    bool reconcilePending = false;
    bool transitioning() const noexcept {
        return phase == _sapp_tc_fullscreen_phase::Entering || phase == _sapp_tc_fullscreen_phase::Exiting;
    }
    bool blocksGeometry() const noexcept { return phase != _sapp_tc_fullscreen_phase::Windowed; }
    bool request(bool full) noexcept {
        desired = full;
        reconcilePending = false;
        if (transitioning() || full == (phase == _sapp_tc_fullscreen_phase::Fullscreen)) return false;
        phase = full ? _sapp_tc_fullscreen_phase::Entering : _sapp_tc_fullscreen_phase::Exiting;
        return true;
    }
    void will(bool full) noexcept {
        // A pre-toggle request already owns the desired state, including a
        // reversal. A user/menu transition supplies its own intent instead.
        if (!transitioning()) desired = full;
        phase = full ? _sapp_tc_fullscreen_phase::Entering : _sapp_tc_fullscreen_phase::Exiting;
    }
    void terminal(bool observedFullscreen) noexcept {
        const bool destination = phase == _sapp_tc_fullscreen_phase::Entering;
        const bool reversed = transitioning() && desired != destination;
        phase = observedFullscreen ? _sapp_tc_fullscreen_phase::Fullscreen : _sapp_tc_fullscreen_phase::Windowed;
        // Failures settle without retrying the failed direction. Only a
        // reversal recorded during the animation can need one reconciliation.
        reconcilePending = reversed && desired != observedFullscreen;
        if (!reconcilePending) desired = observedFullscreen;
    }
    bool reconcile() noexcept {
        if (!reconcilePending) return false;
        reconcilePending = false;
        return request(desired);
    }
};

#endif
