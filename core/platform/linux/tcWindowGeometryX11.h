#pragma once

#include <X11/Xlib.h>
#include <X11/Xatom.h>
#include <X11/Xutil.h>
#include <climits>
#include <cmath>

// #219 geometry, shared by main GLX, main X11/EGL and secondary windows.
// Explicit native handles and per-window state; no GLX or EGL ownership.
namespace trussc::internal {
struct X11GeometryState {
    // Last requested logical content size, independent of WM auto-maximization.
    int requestedWidth = 0;
    int requestedHeight = 0;
};

inline void x11State(Display* dpy, ::Window window, bool add,
                     const char* state1, const char* state2 = nullptr) {
    const Atom wmState = XInternAtom(dpy, "_NET_WM_STATE", False);
    const Atom atom1 = XInternAtom(dpy, state1, False);
    const Atom atom2 = state2 ? XInternAtom(dpy, state2, False) : None;
    if (wmState == None || atom1 == None) return;
    XEvent event{};
    event.xclient.type = ClientMessage;
    event.xclient.window = window;
    event.xclient.message_type = wmState;
    event.xclient.format = 32;
    event.xclient.data.l[0] = add ? 1 : 0;
    event.xclient.data.l[1] = long(atom1);
    event.xclient.data.l[2] = long(atom2);
    event.xclient.data.l[3] = 1; // normal application
    XSendEvent(dpy, DefaultRootWindow(dpy), False,
        SubstructureRedirectMask | SubstructureNotifyMask, &event);
}

inline IVec2 x11FramePosition(Display* dpy, ::Window window) {
    if (!dpy || !window) return {-1, -1};
    int x = 0, y = 0;
    ::Window child = 0;
    if (!XTranslateCoordinates(dpy, window, DefaultRootWindow(dpy), 0, 0, &x, &y, &child)) return {-1, -1};
    Atom type = None;
    int format = 0;
    unsigned long count = 0, remaining = 0;
    unsigned char* data = nullptr;
    if (XGetWindowProperty(dpy, window, XInternAtom(dpy, "_NET_FRAME_EXTENTS", False),
        0, 4, False, XA_CARDINAL, &type, &format, &count, &remaining, &data) == Success &&
        type == XA_CARDINAL && format == 32 && count == 4 && data) {
        const auto* extents = reinterpret_cast<unsigned long*>(data);
        // Bound (defensive): retain round 1's INT16_MAX decoration-offset cap.
        // Ignore larger CARDINAL properties before narrowing and subtracting
        // from the translated INT16 coordinates.
        if (extents[0] <= SHRT_MAX && extents[2] <= SHRT_MAX) {
            x -= int(extents[0]); y -= int(extents[2]);
        }
    }
    if (data) XFree(data);
    return {x, y};
}

inline void x11PinMoveGravity(Display* dpy, ::Window window) {
    XSizeHints hints{};
    long supplied = 0;
    if (!XGetWMNormalHints(dpy, window, &hints, &supplied)) hints = XSizeHints{};
    if (hints.win_gravity == NorthWestGravity && (hints.flags & PWinGravity)) return;
    hints.flags |= PWinGravity;
    hints.win_gravity = NorthWestGravity;
    XSetWMNormalHints(dpy, window, &hints);
}

inline void x11Geometry(Display* dpy, ::Window window, X11GeometryState& requested,
                        bool move, int x, int y, int width, int height, float scale,
                        bool fullscreenIntent = false) {
    if (!dpy || !window) {
        logWarning("Platform") << "window geometry requires X11 (unsupported on headless KMS)";
        return;
    }
    // While fullscreen, leave the WM's saved windowed content size alone.
    // Reapplying it to the undecorated live window can subtract the title bar
    // from Mutter's restore size. A position request still moves/reenters as
    // before, but uses XMoveWindow without a size override.
    const bool resize = !move || (!fullscreenIntent && requested.requestedWidth > 0 && requested.requestedHeight > 0);
    // Keep #219's truncating logical-to-device conversion, including fractional
    // DPI. Bound (X11 protocol): positions are INT16 and extents are CARD16.
    // Check the truncated result's range before converting to an unsigned int.
    const float factor = std::isfinite(scale) && scale > 0 ? scale : 1.0f;
    const double pixelWidth = std::trunc((move ? requested.requestedWidth : width) * factor);
    const double pixelHeight = std::trunc((move ? requested.requestedHeight : height) * factor);
    if ((move && (x < SHRT_MIN || x > SHRT_MAX || y < SHRT_MIN || y > SHRT_MAX)) ||
        (resize && (pixelWidth < 1 || pixelWidth > USHRT_MAX || pixelHeight < 1 || pixelHeight > USHRT_MAX))) {
        logWarning("Platform") << "window geometry exceeds X11 coordinate range";
        return;
    }
    // Pin the top-left on the first resize too: CenterGravity rounds odd
    // deltas and can leave Mutter's content size one pixel off indefinitely.
    x11PinMoveGravity(dpy, window);
    if (!move) {
        // Clear auto-maximize even before the WM publishes its state. Keep
        // fullscreen's existing live resize attempt, without changing restore
        // intent: an ignored fullscreen size must never leak into a later move.
        if (!fullscreenIntent) x11State(dpy, window, false, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
        XResizeWindow(dpy, window, unsigned(pixelWidth), unsigned(pixelHeight));
        XFlush(dpy);
        if (!fullscreenIntent) requested = {width, height};
        return;
    }
    // #219: CenterGravity causes KWin/Mutter round-trip offsets. Pin the
    // decorated frame's top-left, drop fullscreen/maximize before the move,
    // and restore fullscreen only. Send unmaximize even before the WM has
    // published _NET_WM_STATE (creation-time auto-maximize on KWin).
    if (fullscreenIntent) x11State(dpy, window, false, "_NET_WM_STATE_FULLSCREEN");
    x11State(dpy, window, false, "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ");
    if (resize) {
        XMoveResizeWindow(dpy, window, x, y, unsigned(pixelWidth), unsigned(pixelHeight));
    } else {
        XMoveWindow(dpy, window, x, y);
    }
    if (fullscreenIntent) x11State(dpy, window, true, "_NET_WM_STATE_FULLSCREEN");
    XFlush(dpy);
}
}
