#pragma once
namespace trussc::internal {
struct WindowGeometryState {
    // Last requested logical content size, independent of WM auto-maximization.
    double requestedWidth = 0;
    double requestedHeight = 0;
    bool pixelSize = false; // pixel requests retain fractions until native conversion
    // X11: keep the snapshot through withdrawal and the next pending map.
    bool hiddenPosition = false;
    int hiddenX = 0, hiddenY = 0;
};
}
