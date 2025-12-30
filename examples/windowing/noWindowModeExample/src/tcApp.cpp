// =============================================================================
// noWindowModeExample - Headless Mode Demo
// =============================================================================
//
// This example demonstrates running a TrussC app without a window or graphics
// context. Useful for server applications, background services, or utilities
// that only need non-graphics features.
//
// Usage:
//   - Press Ctrl+C to exit
//   - Or call requestExit() programmatically
//
// Available features in headless mode:
//   - Serial communication (tc::Serial)
//   - Network: TCP/UDP sockets (tc::TcpClient, TcpServer, UdpSocket)
//   - OSC: Open Sound Control (tc::OscSender, OscReceiver)
//   - File I/O: read/write files (tc::loadFile, saveFile, etc.)
//   - JSON/XML parsing (tc::Json, tc::Xml)
//   - Math utilities (tc::Vec2, Vec3, Mat4, noise, etc.)
//   - Threading (tc::Thread, ThreadChannel)
//   - Timers and timing (tc::getElapsedTime, headless::getElapsedTime)
//   - Logging (tc::logNotice, logWarning, logError)
//   - Console input (tc::console)
//
// NOT available in headless mode (no-op or skipped):
//   - All graphics/drawing functions
//   - Window management
//   - Mouse/keyboard events
//   - FBO (off-screen rendering)
//   - Texture, Shader, Font (GPU resources)
//   - Video player/grabber
//   - ImGui
//
// =============================================================================

#include "tcApp.h"

void tcApp::setup() {
    logNotice("noWindowMode") << "=== noWindowMode Example ===";
    logNotice("noWindowMode") << "Running in headless mode (no window)";
    logNotice("noWindowMode") << "Press Ctrl+C to exit";
}

void tcApp::update() {
    frameCount_++;

    // Print status every second (60 frames at 60fps)
    if (frameCount_ % 60 == 0) {
        double elapsed = headless::getElapsedTime();
        logNotice("noWindowMode") << "Running... Frame " << frameCount_
                                    << " | Elapsed: " << elapsed << "s";
    }
}

void tcApp::cleanup() {
    logNotice("noWindowMode") << "cleanup() called";
    logNotice("noWindowMode") << "Total frames: " << frameCount_;
    logNotice("noWindowMode") << "=== Done ===";
}
