#pragma once

// =============================================================================
// tcDebugInput - Debug input simulation and capture
// =============================================================================
//
// Provides stdin-based input simulation for debugging and testing.
// Commands are received via tcdebug prefix in console input.
//
// Usage:
//   // Enable in main.cpp
//   WindowSettings settings;
//   settings.enableDebugInput = true;
//   runApp<tcApp>(settings);
//
//   // Send commands via stdin
//   echo 'tcdebug {"type":"mouse_click","x":100,"y":200}' | ./myapp
//   echo 'tcdebug stream normal' | ./myapp
//
// =============================================================================

#include <string>
#include <vector>
#include <iostream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>
#include "../events/tcEventArgs.h"
#include "../events/tcCoreEvents.h"
#include "nlohmann/json.hpp"

namespace trussc {

// Forward declarations (defined in TrussC.h)
double getElapsedTime();
double getFrameRate();
int getWindowWidth();
int getWindowHeight();
uint64_t getUpdateCount();
uint64_t getDrawCount();
bool saveScreenshot(const std::filesystem::path& path);
float getDpiScale();
bool isFullscreen();
float getMouseX();
float getMouseY();
std::string getBackendName();
size_t getMemoryUsage();
size_t getNodeCount();
size_t getTextureCount();
size_t getFboCount();

namespace internal {
    extern void (*appKeyPressedFunc)(int);
    extern void (*appKeyReleasedFunc)(int);
    extern void (*appMousePressedFunc)(int, int, int);
    extern void (*appMouseReleasedFunc)(int, int, int);
    extern void (*appMouseMovedFunc)(int, int);
    extern void (*appMouseDraggedFunc)(int, int, int);
    extern void (*appMouseScrolledFunc)(float, float);
    extern void (*appFilesDroppedFunc)(const std::vector<std::string>&);
}

namespace debuginput {

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

inline bool enabled = false;  // Set from WindowSettings.enableDebugInput

enum class StreamMode { Disabled, Normal, Detail };
inline StreamMode streamMode = StreamMode::Disabled;

enum class PlaybackMode { Immediate, Realtime };
inline PlaybackMode playbackMode = PlaybackMode::Immediate;

// Drag tracking for normal stream mode
inline bool isDragging = false;
inline float dragStartX = 0, dragStartY = 0;
inline int dragButton = -1;

// Playback timing
inline float lastEventTime = 0.0f;

// Flag to prevent echo when injecting events
inline bool isInjecting = false;

// ---------------------------------------------------------------------------
// Stream output helpers
// ---------------------------------------------------------------------------

inline void streamOutput(const std::string& type, float x, float y, int button = -1) {
    if (streamMode == StreamMode::Disabled || isInjecting) return;

    std::string buttonStr = (button == 0) ? "left" : (button == 1) ? "right" : (button == 2) ? "middle" : "";

    std::cout << "tcdebug {\"type\":\"" << type << "\"";
    std::cout << ",\"x\":" << x << ",\"y\":" << y;
    if (!buttonStr.empty()) {
        std::cout << ",\"button\":\"" << buttonStr << "\"";
    }
    std::cout << ",\"time\":" << getElapsedTime() << "}" << std::endl;
}

inline void streamOutputKey(const std::string& type, int key) {
    if (streamMode == StreamMode::Disabled || isInjecting) return;

    std::cout << "tcdebug {\"type\":\"" << type << "\",\"key\":" << key
              << ",\"time\":" << getElapsedTime() << "}" << std::endl;
}

inline void streamOutputScroll(float dx, float dy) {
    if (streamMode == StreamMode::Disabled || isInjecting) return;

    std::cout << "tcdebug {\"type\":\"mouse_scroll\",\"dx\":" << dx << ",\"dy\":" << dy
              << ",\"time\":" << getElapsedTime() << "}" << std::endl;
}

inline void streamOutputDrop(const std::vector<std::string>& files) {
    if (streamMode == StreamMode::Disabled || isInjecting) return;

    std::cout << "tcdebug {\"type\":\"drop\",\"files\":[";
    for (size_t i = 0; i < files.size(); i++) {
        if (i > 0) std::cout << ",";
        std::cout << "\"" << files[i] << "\"";
    }
    std::cout << "],\"time\":" << getElapsedTime() << "}" << std::endl;
}

// ---------------------------------------------------------------------------
// Command handler
// ---------------------------------------------------------------------------

inline void handleCommand(ConsoleEventArgs& e) {
    if (e.args.empty()) return;
    if (e.args[0] != "tcdebug" || e.args.size() < 2) return;

    // Check if JSON format (second arg starts with '{')
    bool isJson = !e.args[1].empty() && e.args[1][0] == '{';

    std::string type;
    nlohmann::json j;

    if (isJson) {
        // Parse JSON: reconstruct from raw line after "tcdebug "
        size_t jsonStart = e.raw.find('{');
        if (jsonStart == std::string::npos) return;
        std::string jsonStr = e.raw.substr(jsonStart);
        // Strip trailing comment if any
        size_t commentPos = jsonStr.find('#');
        if (commentPos != std::string::npos) {
            jsonStr = jsonStr.substr(0, commentPos);
        }
        try {
            j = nlohmann::json::parse(jsonStr);
            type = j.value("type", "");
        } catch (...) {
            std::cout << "tcdebug {\"status\":\"error\",\"message\":\"invalid JSON\"}" << std::endl;
            return;
        }
    } else {
        // Space-separated format: e.args[1] is the type/category
        type = e.args[1];
    }

    // Track button state for drag detection
    static int pressedButton = -1;

    // Helper lambdas for event injection
    auto injectMouseEvent = [](const std::string& action, float x, float y, int button) {
        isInjecting = true;
        if (action == "mouse_press" || action == "press") {
            pressedButton = button;
            MouseEventArgs args;
            args.x = x;
            args.y = y;
            args.button = button;
            events().mousePressed.notify(args);
            if (internal::appMousePressedFunc) internal::appMousePressedFunc((int)x, (int)y, button);
        } else if (action == "mouse_release" || action == "release") {
            pressedButton = -1;
            MouseEventArgs args;
            args.x = x;
            args.y = y;
            args.button = button;
            events().mouseReleased.notify(args);
            if (internal::appMouseReleasedFunc) internal::appMouseReleasedFunc((int)x, (int)y, button);
        } else if (action == "mouse_move" || action == "move") {
            if (pressedButton >= 0) {
                // Dragging
                MouseDragEventArgs args;
                args.x = x;
                args.y = y;
                args.button = pressedButton;
                events().mouseDragged.notify(args);
                if (internal::appMouseDraggedFunc) internal::appMouseDraggedFunc((int)x, (int)y, pressedButton);
            } else {
                // Just moving
                MouseMoveEventArgs args;
                args.x = x;
                args.y = y;
                events().mouseMoved.notify(args);
                if (internal::appMouseMovedFunc) internal::appMouseMovedFunc((int)x, (int)y);
            }
        }
        isInjecting = false;
    };

    auto injectKeyEvent = [](const std::string& action, int key) {
        isInjecting = true;
        KeyEventArgs args;
        args.key = key;
        if (action == "key_press" || action == "press") {
            events().keyPressed.notify(args);
            if (internal::appKeyPressedFunc) internal::appKeyPressedFunc(key);
        } else if (action == "key_release" || action == "release") {
            events().keyReleased.notify(args);
            if (internal::appKeyReleasedFunc) internal::appKeyReleasedFunc(key);
        }
        isInjecting = false;
    };

    auto buttonFromString = [](const std::string& s) -> int {
        if (s == "left" || s == "0") return 0;
        if (s == "right" || s == "1") return 1;
        if (s == "middle" || s == "2") return 2;
        return 0;
    };

    // --- Built-in commands (always available) ---
    if (type == "info") {
        // Generate ISO 8601 timestamp
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        std::stringstream ts;
        ts << std::put_time(std::gmtime(&time_t_now), "%Y-%m-%dT%H:%M:%SZ");

        std::cout << "tcdebug {\"type\":\"info\""
                  << ",\"timestamp\":\"" << ts.str() << "\""
                  << ",\"fps\":" << getFrameRate()
                  << ",\"width\":" << getWindowWidth()
                  << ",\"height\":" << getWindowHeight()
                  << ",\"dpiScale\":" << getDpiScale()
                  << ",\"fullscreen\":" << (isFullscreen() ? "true" : "false")
                  << ",\"mouseX\":" << getMouseX()
                  << ",\"mouseY\":" << getMouseY()
                  << ",\"updateCount\":" << getUpdateCount()
                  << ",\"drawCount\":" << getDrawCount()
                  << ",\"elapsedTime\":" << getElapsedTime()
                  << ",\"backend\":\"" << getBackendName() << "\""
                  << ",\"memoryBytes\":" << getMemoryUsage()
                  << ",\"nodeCount\":" << getNodeCount()
                  << ",\"textureCount\":" << getTextureCount()
                  << ",\"fboCount\":" << getFboCount()
                  << ",\"debugInputEnabled\":" << (enabled ? "true" : "false")
                  << "}" << std::endl;
        return;
    }

    if (type == "screenshot") {
        std::string path = "/tmp/trussc_screenshot.png";
        if (isJson && j.contains("path")) {
            path = j["path"].get<std::string>();
        } else if (!isJson && e.args.size() >= 3) {
            path = e.args[2];
        }
        bool success = saveScreenshot(path);
        std::cout << "tcdebug {\"type\":\"screenshot\",\"status\":\""
                  << (success ? "ok" : "error") << "\",\"path\":\"" << path << "\"}" << std::endl;
        return;
    }

    if (type == "help") {
        std::cout << "tcdebug {\"type\":\"help\",\"commands\":["
                  << "\"info\",\"screenshot\",\"help\","
                  << "\"mouse_move\",\"mouse_press\",\"mouse_release\",\"mouse_click\",\"mouse_scroll\","
                  << "\"key_press\",\"key_release\",\"key_send\","
                  << "\"drop\",\"stream\",\"playback\""
                  << "],\"debugInputEnabled\":" << (enabled ? "true" : "false")
                  << "}" << std::endl;
        return;
    }

    // --- Input simulation commands (requires enabled) ---
    if (!enabled) {
        std::cout << "tcdebug {\"status\":\"error\",\"message\":\"debug input disabled\"}" << std::endl;
        return;
    }

    // Mouse commands
    if (type == "mouse_move" || type == "mouse_press" || type == "mouse_release" ||
        type == "mouse_click" || type == "mouse_scroll" || type == "mouse") {

        float x = 0, y = 0;
        int button = 0;
        std::string action = type;

        if (isJson) {
            x = j.value("x", 0.0f);
            y = j.value("y", 0.0f);
            button = buttonFromString(j.value("button", "left"));
            if (j.contains("action")) action = "mouse_" + j["action"].get<std::string>();
        } else {
            // Space-separated: tcdebug mouse <action> <x> <y> [button]
            if (type == "mouse" && e.args.size() >= 5) {
                action = "mouse_" + e.args[2];
                x = std::stof(e.args[3]);
                y = std::stof(e.args[4]);
                if (e.args.size() >= 6) button = buttonFromString(e.args[5]);
            } else if (e.args.size() >= 4) {
                // tcdebug mouse_press <x> <y> [button]
                x = std::stof(e.args[2]);
                y = std::stof(e.args[3]);
                if (e.args.size() >= 5) button = buttonFromString(e.args[4]);
            }
        }

        if (action == "mouse_click") {
            // click = press + release
            injectMouseEvent("press", x, y, button);
            injectMouseEvent("release", x, y, button);
        } else if (action == "mouse_scroll") {
            float dx = 0.0f, dy = 0.0f;
            if (isJson) {
                // Support both dx/dy and deltaX/deltaY
                dx = j.contains("dx") ? j.value("dx", 0.0f) : j.value("deltaX", 0.0f);
                dy = j.contains("dy") ? j.value("dy", 0.0f) : j.value("deltaY", 0.0f);
            } else if (e.args.size() >= 4) {
                dx = std::stof(e.args[2]);
                dy = std::stof(e.args[3]);
            }
            isInjecting = true;
            ScrollEventArgs args;
            args.scrollX = dx;
            args.scrollY = dy;
            events().mouseScrolled.notify(args);
            if (internal::appMouseScrolledFunc) internal::appMouseScrolledFunc(dx, dy);
            isInjecting = false;
        } else {
            injectMouseEvent(action, x, y, button);
        }

        std::cout << "tcdebug {\"status\":\"ok\",\"type\":\"" << action << "\"}" << std::endl;
        return;
    }

    // Key commands
    if (type == "key_press" || type == "key_release" || type == "key_send" || type == "key") {
        int key = 0;
        std::string action = type;

        if (isJson) {
            key = j.value("key", 0);
            if (j.contains("action")) action = "key_" + j["action"].get<std::string>();
        } else {
            // Space-separated: tcdebug key <action> <keycode> or tcdebug key_press <keycode>
            if (type == "key" && e.args.size() >= 4) {
                action = "key_" + e.args[2];
                key = std::stoi(e.args[3]);
            } else if (e.args.size() >= 3) {
                key = std::stoi(e.args[2]);
            }
        }

        if (action == "key_send") {
            // send = press + release
            injectKeyEvent("press", key);
            injectKeyEvent("release", key);
        } else {
            injectKeyEvent(action, key);
        }

        std::cout << "tcdebug {\"status\":\"ok\",\"type\":\"" << action << "\",\"key\":" << key << "}" << std::endl;
        return;
    }

    // Drop command
    if (type == "drop") {
        std::vector<std::string> files;
        if (isJson && j.contains("files")) {
            for (const auto& f : j["files"]) {
                files.push_back(f.get<std::string>());
            }
        } else {
            // Space-separated: tcdebug drop <file1> [file2] ...
            for (size_t i = 2; i < e.args.size(); i++) {
                files.push_back(e.args[i]);
            }
        }

        isInjecting = true;
        DragDropEventArgs args;
        args.files = files;
        events().filesDropped.notify(args);
        if (internal::appFilesDroppedFunc) internal::appFilesDroppedFunc(files);
        isInjecting = false;

        std::cout << "tcdebug {\"status\":\"ok\",\"type\":\"drop\",\"files\":" << files.size() << "}" << std::endl;
        return;
    }

    // Stream command
    if (type == "stream") {
        std::string mode = "disable";
        if (isJson && j.contains("mode")) {
            mode = j["mode"].get<std::string>();
        } else if (!isJson && e.args.size() >= 3) {
            mode = e.args[2];
        }

        if (mode == "disable" || mode == "off") {
            streamMode = StreamMode::Disabled;
        } else if (mode == "normal") {
            streamMode = StreamMode::Normal;
        } else if (mode == "detail") {
            streamMode = StreamMode::Detail;
        }

        std::cout << "tcdebug {\"status\":\"ok\",\"type\":\"stream\",\"mode\":\"" << mode << "\"}" << std::endl;
        return;
    }

    // Playback command
    if (type == "playback") {
        std::string mode = "immediate";
        if (isJson && j.contains("mode")) {
            mode = j["mode"].get<std::string>();
        } else if (!isJson && e.args.size() >= 3) {
            mode = e.args[2];
        }

        if (mode == "immediate") {
            playbackMode = PlaybackMode::Immediate;
        } else if (mode == "realtime") {
            playbackMode = PlaybackMode::Realtime;
        }

        std::cout << "tcdebug {\"status\":\"ok\",\"type\":\"playback\",\"mode\":\"" << mode << "\"}" << std::endl;
        return;
    }

    // Unknown command
    std::cout << "tcdebug {\"status\":\"error\",\"message\":\"unknown command\",\"type\":\"" << type << "\"}" << std::endl;
}

} // namespace debuginput
} // namespace trussc
