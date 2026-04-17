// =============================================================================
// main.cpp - System Info Example
//
// Displays system sensor and status information as a dashboard.
// Full functionality on iOS and Android (battery, volume, brightness,
// accelerometer, gyroscope, compass, proximity, location).
// Desktop (macOS/Windows/Linux) and WASM only support a subset
// (battery, volume, thermal state on macOS; stubs elsewhere).
// =============================================================================

#include "tcApp.h"

int main() {
    tc::WindowSettings settings;
    settings.setSize(480, 800);
    settings.setTitle("System Info");

    return TC_RUN_APP(tcApp, settings);
}
