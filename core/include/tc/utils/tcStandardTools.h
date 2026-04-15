#pragma once

// =============================================================================
// tcStandardTools.h - Standard MCP Tools for TrussC
// =============================================================================

#include "tcMCP.h"
#include "tcUtils.h"
#include "../events/tcCoreEvents.h"
#include "stb/stb_image_write.h"
#include "../graphics/tcPixels.h"
#include <cstdlib>

// Forward declaration for stbi_write_png_to_mem (missing in older stb_image_write.h headers)
extern "C" unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len);

namespace trussc {
namespace mcp {

// ---------------------------------------------------------------------------
// Inspection Tools (read-only, always available when MCP is enabled)
// ---------------------------------------------------------------------------

inline void registerInspectionTools() {

    tool("get_screenshot", "Get screenshot as Base64 PNG")
        .bind(std::function<json()>([]() -> json {
            // Capture screen to pixels
            Pixels pixels;
            if (!grabScreen(pixels)) {
                return json{{"status", "error"}, {"message", "Failed to grab screen"}};
            }

            // Encode to PNG in memory
            int pngSize = 0;
            unsigned char* pngData = stbi_write_png_to_mem(
                pixels.getData(), 0,
                pixels.getWidth(), pixels.getHeight(), pixels.getChannels(),
                &pngSize);

            if (!pngData) {
                return json{{"status", "error"}, {"message", "Failed to encode PNG"}};
            }

            // Convert to Base64
            std::string b64 = toBase64(pngData, pngSize);

            // Free PNG data
            std::free(pngData);

            return json{
                {"mimeType", "image/png"},
                {"data", b64}
            };
        }));

    tool("save_screenshot", "Save screenshot to file")
        .arg<std::string>("path", "File path")
        .bind<std::string>([](std::string path) {
            if (trussc::saveScreenshot(path)) {
                return json{{"status", "ok"}, {"path", path}};
            } else {
                return json{{"status", "error"}, {"message", "Failed to save screenshot"}};
            }
        });

    tool("quit", "Quit the application gracefully")
        .bind(std::function<json()>([]() -> json {
            sapp_request_quit();
            return json{{"status", "ok"}};
        }));
}

// ---------------------------------------------------------------------------
// Debugger Tools (input injection, opt-in via mcp::enableDebugger())
// ---------------------------------------------------------------------------

inline void registerDebuggerTools() {

    // --- Mouse Tools ---

    tool("mouse_move", "Move mouse cursor")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button state (0:left, 1:right, 2:middle, -1:none)", false)
        .bind<float, float, int>([](float x, float y, int button) {
            // If button is pressed, treat as drag
            if (button >= 0) {
                MouseDragEventArgs args;
                args.x = x;
                args.y = y;
                args.deltaX = 0;
                args.deltaY = 0;
                args.button = button;
                events().mouseDragged.notify(args);

                if (::trussc::internal::appMouseDraggedFunc)
                    ::trussc::internal::appMouseDraggedFunc((int)x, (int)y, button);
            } else {
                MouseMoveEventArgs args;
                args.x = x;
                args.y = y;
                args.deltaX = 0;
                args.deltaY = 0;
                events().mouseMoved.notify(args);

                if (::trussc::internal::appMouseMovedFunc)
                    ::trussc::internal::appMouseMovedFunc((int)x, (int)y);
            }

            // Update global mouse state
            ::trussc::internal::mouseX = x;
            ::trussc::internal::mouseY = y;

            return json{{"status", "ok"}};
        });

    tool("mouse_click", "Click mouse button")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind<float, float, int>([](float x, float y, int button) {
            // Press
            MouseEventArgs args;
            args.x = x;
            args.y = y;
            args.button = button;
            events().mousePressed.notify(args);
            if (::trussc::internal::appMousePressedFunc)
                ::trussc::internal::appMousePressedFunc((int)x, (int)y, button);

            // Release
            events().mouseReleased.notify(args);
            if (::trussc::internal::appMouseReleasedFunc)
                ::trussc::internal::appMouseReleasedFunc((int)x, (int)y, button);

            return json{{"status", "ok"}};
        });

    tool("mouse_scroll", "Scroll mouse wheel")
        .arg<float>("dx", "Horizontal scroll delta")
        .arg<float>("dy", "Vertical scroll delta")
        .bind<float, float>([](float dx, float dy) {
            ScrollEventArgs args;
            args.scrollX = dx;
            args.scrollY = dy;
            events().mouseScrolled.notify(args);
            if (::trussc::internal::appMouseScrolledFunc)
                ::trussc::internal::appMouseScrolledFunc(dx, dy);
            return json{{"status", "ok"}};
        });

    // --- Key Tools ---

    tool("key_press", "Press a key")
        .arg<int>("key", "Key code (sokol_app keycode)")
        .bind<int>([](int key) {
            KeyEventArgs args;
            args.key = key;
            events().keyPressed.notify(args);
            if (::trussc::internal::appKeyPressedFunc)
                ::trussc::internal::appKeyPressedFunc(key);
            return json{{"status", "ok"}};
        });

    tool("key_release", "Release a key")
        .arg<int>("key", "Key code (sokol_app keycode)")
        .bind<int>([](int key) {
            KeyEventArgs args;
            args.key = key;
            events().keyReleased.notify(args);
            if (::trussc::internal::appKeyReleasedFunc)
                ::trussc::internal::appKeyReleasedFunc(key);
            return json{{"status", "ok"}};
        });

}

} // namespace mcp
} // namespace trussc
