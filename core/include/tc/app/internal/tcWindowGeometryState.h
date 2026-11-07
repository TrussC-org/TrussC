#pragma once
namespace trussc::internal {
struct WindowGeometryState {
    // Last requested logical content size, independent of WM auto-maximization.
    double requestedWidth = 0;
    double requestedHeight = 0;
    bool pixelSize = false; // pixel requests retain fractions until native conversion
    // X11: a withdrawn window no longer has a decorated frame to query.
    bool hiddenPosition = false;
    int hiddenX = 0, hiddenY = 0;
};
}
