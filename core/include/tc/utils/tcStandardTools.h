#pragma once

// =============================================================================
// tcStandardTools.h - Standard MCP Tools for TrussC
// =============================================================================

#include "tcMCP.h"
#include "tcUtils.h"
#include "tcJsonReflect.h"
#include "../events/tcCoreEvents.h"
#include "stb/stb_image_write.h"
#include "../graphics/tcPixels.h"
#include <cstdlib>

// Forward declaration for stbi_write_png_to_mem (missing in older stb_image_write.h headers)
extern "C" unsigned char *stbi_write_png_to_mem(const unsigned char *pixels, int stride_bytes, int x, int y, int n, int *out_len);

namespace trussc {

// ---------------------------------------------------------------------------
// Node -> JSON (serialize-only; the reverse needs a type factory and is out
// of scope — apply values to existing nodes with reflectFromJson instead)
// ---------------------------------------------------------------------------

// One node as JSON: type, optional instance name, instance id, reflected
// members (same encoding as JsonWriteReflector), mods ({type, members} each),
// and the children in draw order. maxDepth limits recursion (-1 = unlimited,
// 0 = this node only); where children are cut off, "childCount" says how many
// were omitted so a caller can drill in with another get_node_tree(id) call.
inline Json nodeToJson(Node& node, int maxDepth = -1) {
    Json j = Json::object();
    j["type"] = node.getTypeName();
    if (node.hasName()) j["name"] = node.getName();
    j["id"] = node.getInstanceId();
    j["members"] = reflectToJson(node);
    auto mods = node.getMods();
    if (!mods.empty()) {
        Json jmods = Json::array();
        for (Mod* m : mods) {
            Json jm = Json::object();
            Mod& mod = *m;
            jm["type"] = shortTypeName(typeid(mod));
            Json members = reflectToJson(mod);
            if (!members.empty()) jm["members"] = std::move(members);
            jmods.push_back(std::move(jm));
        }
        j["mods"] = std::move(jmods);
    }
    if (node.getChildCount() > 0) {
        if (maxDepth == 0) {
            j["childCount"] = node.getChildCount();
        } else {
            Json children = Json::array();
            for (auto& c : node.getChildren()) {
                children.push_back(nodeToJson(*c, maxDepth < 0 ? -1 : maxDepth - 1));
            }
            // Move — an lvalue assignment would deep-copy the whole subtree
            // JSON at every tree level (O(n^2) on deep chains).
            j["children"] = std::move(children);
        }
    }
    return j;
}

namespace mcp {

// ---------------------------------------------------------------------------
// Inspection Tools (read-only, always available when MCP is enabled)
// ---------------------------------------------------------------------------

inline void registerInspectionTools() {

    tool("get_screenshot", "Get screenshot as Base64 PNG")
        .bind(std::function<json()>([]() -> json {
            // Defer the actual capture+encode to just after present(): during a
            // frame nothing has been rendered yet (drawing is deferred), so a
            // readback here would be blank (black on Linux). The producer below
            // runs at the afterFrame safe point and its result is what the MCP
            // client receives.
            mcp::deferToolResultUntilAfterFrame([]() -> json {
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
            });
            return json(nullptr);  // ignored — deferred result is sent instead
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

    // --- Recording tools (native encoder, no ffmpeg) ---

    tool("start_recording", "Start recording the window to a video file (the screenshot's video counterpart). Omit path for a timestamped file in the data dir; give duration for a fixed-length clip that auto-stops and finalizes itself.")
        .arg<std::string>("path", "Output file path (relative paths resolve to the data dir). Omit for recording-<timestamp>.mp4/.mov in the data dir.", false)
        .arg<float>("duration", "Fixed length in seconds; the file auto-stops & finalizes at that length. Omit or 0 = unlimited (stop_recording to end).", false)
        .arg<float>("fps", "Target frame rate (default 60; ProMotion frames are decimated to it)", false)
        .arg<std::string>("codec", "h264 (default) | hevc | prores422 | prores4444 (.mov, macOS)", false)
        .bind([](const json& args) -> json {
            trussc::VideoRecordSettings settings;
            if (args.contains("fps") && args.at("fps").is_number()) {
                settings.fps = args.at("fps").get<float>();
            }
            if (args.contains("duration") && args.at("duration").is_number()) {
                settings.duration = args.at("duration").get<float>();
            }
            std::string codec = args.value("codec", std::string());
            if      (codec == "hevc")       settings.codec = trussc::VideoCodec::HEVC;
            else if (codec == "prores422")  settings.codec = trussc::VideoCodec::ProRes422;
            else if (codec == "prores4444") settings.codec = trussc::VideoCodec::ProRes4444;
            else if (!codec.empty() && codec != "h264") {
                return json{{"status", "error"}, {"message", "unknown codec: " + codec}};
            }
            // Default output: recording-<timestamp> in the data dir. ProRes is a
            // .mov container; everything else is .mp4. startRecording() resolves
            // the relative path via getDataPath(); recordingPath() returns it.
            std::string path = args.value("path", std::string());
            if (path.empty()) {
                bool isProRes = settings.codec == trussc::VideoCodec::ProRes422 ||
                                settings.codec == trussc::VideoCodec::ProRes4444;
                path = "recording-" + trussc::getTimestampString("%Y-%m-%d-%H-%M-%S")
                     + (isProRes ? ".mov" : ".mp4");
            }
            bool ok = trussc::startRecording(path, settings);
            json r{{"status", ok ? "ok" : "error"},
                   {"path", trussc::recordingPath()},
                   {"fps", settings.fps},
                   {"codec", trussc::videoCodecName(settings.codec)}};
            if (settings.duration > 0.0f) r["duration"] = settings.duration;
            return r;
        });

    tool("stop_recording", "Stop the current recording and finalize the file. Wins over a pending fixed duration (finalizes immediately at the current length); a no-op if nothing is recording.")
        .bind(std::function<json()>([]() -> json {
            if (!trussc::isRecording()) {
                // Harmless no-op (e.g. a fixed-duration recording already
                // auto-stopped, or nothing was started). Not an error.
                return json{{"status", "ok"}, {"recording", false},
                            {"message", "not recording"}};
            }
            std::string path = trussc::recordingPath();
            int frames = trussc::recordingFrameCount();
            float fps  = trussc::internal::globalScreenRecorder().writer().getFps();
            trussc::stopRecording();
            json r{{"status", "ok"}, {"recording", false},
                   {"path", path}, {"frames", frames}};
            if (fps > 0.0f) r["length"] = frames / fps;   // measured output length (s)
            return r;
        }));

    // --- Node tree tools ---

    tool("get_node_tree", "Dump the node tree as JSON: per node {type, name, id, members (reflected, rotation in degrees, colors 0-1), mods, children}. Where depth cuts children off, childCount marks how many were omitted — drill in with another call passing that node's id")
        .arg<int>("id", "Subtree root instance id (omit for the whole tree)", false)
        .arg<int>("depth", "Max child depth (omit for unlimited, 0 = the node only)", false)
        .bind([](const json& args) -> json {
            Node* root = getRootNode();
            if (!root) {
                return json{{"status", "error"}, {"message", "App is not running"}};
            }
            if (args.contains("id") && !args.at("id").is_null()) {
                uint64_t id = args.at("id").get<uint64_t>();
                root = root->findByInstanceId(id);
                if (!root) {
                    return json{{"status", "error"}, {"message", "No node with id " + std::to_string(id)}};
                }
            }
            int depth = -1;
            if (args.contains("depth") && args.at("depth").is_number()) {
                depth = args.at("depth").get<int>();
            }
            return json{{"status", "ok"}, {"tree", nodeToJson(*root, depth)}};
        });

    tool("get_selected_node", "Get the currently selected node (type, name, id, reflected members), or null if nothing is selected")
        .bind(std::function<json()>([]() -> json {
            Node* n = getSelectedNode();
            if (!n) {
                return json{{"status", "ok"}, {"selected", nullptr}};
            }
            return json{{"status", "ok"}, {"selected", nodeToJson(*n, 0)}};
        }));
}

// ---------------------------------------------------------------------------
// Debugger Tools (input injection, opt-in via mcp::registerDebuggerTools())
// ---------------------------------------------------------------------------

inline void registerDebuggerTools() {

    // Registering the debugger surface IS the opt-in to input injection /
    // scene mutation. Mark it so isDebuggerEnabled() reflects reality.
    detail::isDebuggerEnabled().store(true);

    // --- Mouse Tools ---

    tool("mouse_move", "Move mouse cursor (with a button held, emits a drag)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button held during the move (0:left, 1:right, 2:middle; omit or -1 for a plain move)", false)
        .bind([](const json& a) -> json {
            // json bind, not the typed one — the typed bind requires every
            // declared arg, which contradicted "button" being optional.
            float x = a.at("x").get<float>();
            float y = a.at("y").get<float>();
            int button = a.value("button", -1);
            // If button is pressed, treat as drag
            if (button >= 0) {
                internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(x, y);
                args.button = button;
                MouseDragEventArgs dragArgs = internal::toDragArgs(args);
                events().mouseDragged.notify(dragArgs);

                if (::trussc::internal::appMouseDraggedFunc)
                    ::trussc::internal::appMouseDraggedFunc(args);
            } else {
                internal::MouseEventRaw args;
                args.pos = args.globalPos = Vec2(x, y);
                MouseMoveEventArgs moveArgs = internal::toMoveArgs(args);
                events().mouseMoved.notify(moveArgs);

                if (::trussc::internal::appMouseMovedFunc)
                    ::trussc::internal::appMouseMovedFunc(args);
            }

            // Update global mouse state
            ::trussc::internal::mouseX = x;
            ::trussc::internal::mouseY = y;

            return json{{"status", "ok"}};
        });

    // Split press/release. A drag gesture is press → mouse_move(button) × N →
    // release; mouse_click fires press+release back-to-back, so drag consumers
    // (e.g. EasyCam orbit) never see an open gesture. Anchoring the global
    // mouse position at press keeps the first drag delta sane.
    tool("mouse_press", "Press and hold a mouse button (start of a drag; pair with mouse_move + mouse_release)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(a.at("x").get<float>(), a.at("y").get<float>());
            args.button = a.value("button", 0);
            args.syncLegacy();

            ::trussc::internal::mouseX = args.pos.x;
            ::trussc::internal::mouseY = args.pos.y;

            events().mousePressed.notify(args);
            if (::trussc::internal::appMousePressedFunc)
                ::trussc::internal::appMousePressedFunc(args);

            return json{{"status", "ok"}};
        });

    tool("mouse_release", "Release a mouse button (end of a drag)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(a.at("x").get<float>(), a.at("y").get<float>());
            args.button = a.value("button", 0);
            args.syncLegacy();

            ::trussc::internal::mouseX = args.pos.x;
            ::trussc::internal::mouseY = args.pos.y;

            events().mouseReleased.notify(args);
            if (::trussc::internal::appMouseReleasedFunc)
                ::trussc::internal::appMouseReleasedFunc(args);

            return json{{"status", "ok"}};
        });

    tool("mouse_click", "Click mouse button (optionally with modifier keys held)")
        .arg<float>("x", "X coordinate")
        .arg<float>("y", "Y coordinate")
        .arg<int>("button", "Button (0:left, 1:right, 2:middle)", false)
        .arg<bool>("shift", "Hold Shift", false)
        .arg<bool>("ctrl", "Hold Ctrl", false)
        .arg<bool>("alt", "Hold Alt", false)
        .arg<bool>("super", "Hold Cmd/Super", false)
        .bind([](const json& a) -> json {
            MouseEventArgs args;
            args.pos = args.globalPos = Vec2(a.at("x").get<float>(), a.at("y").get<float>());
            args.button = a.value("button", 0);
            args.shift = a.value("shift", false);
            args.ctrl  = a.value("ctrl", false);
            args.alt   = a.value("alt", false);
            args.super = a.value("super", false);
            args.syncLegacy();

            // Press
            events().mousePressed.notify(args);
            if (::trussc::internal::appMousePressedFunc)
                ::trussc::internal::appMousePressedFunc(args);

            // Release (fresh consumed flag — the press consumer may differ)
            MouseEventArgs rel = args;
            rel.consumed = false;
            events().mouseReleased.notify(rel);
            if (::trussc::internal::appMouseReleasedFunc)
                ::trussc::internal::appMouseReleasedFunc(rel);

            return json{{"status", "ok"}};
        });

    tool("mouse_scroll", "Scroll mouse wheel")
        .arg<float>("dx", "Horizontal scroll delta")
        .arg<float>("dy", "Vertical scroll delta")
        .bind<float, float>([](float dx, float dy) {
            ScrollEventArgs args;
            args.pos = args.globalPos = Vec2(::trussc::internal::mouseX, ::trussc::internal::mouseY);
            args.scroll = Vec2(dx, dy);
            args.syncLegacy();
            events().mouseScrolled.notify(args);
            if (::trussc::internal::appMouseScrolledFunc)
                ::trussc::internal::appMouseScrolledFunc(args);
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
                ::trussc::internal::appKeyPressedFunc(args);
            return json{{"status", "ok"}};
        });

    tool("key_release", "Release a key")
        .arg<int>("key", "Key code (sokol_app keycode)")
        .bind<int>([](int key) {
            KeyEventArgs args;
            args.key = key;
            events().keyReleased.notify(args);
            if (::trussc::internal::appKeyReleasedFunc)
                ::trussc::internal::appKeyReleasedFunc(args);
            return json{{"status", "ok"}};
        });

    // --- Node Tools ---

    tool("select_node", "Select a node by instance id (0 clears the selection); drives the same selection an inspector shows")
        .arg<int>("id", "Instance id from get_node_tree (0 to clear)")
        .bind<int>([](int id) {
            if (id == 0) {
                setSelectedNode(nullptr);
                return json{{"status", "ok"}, {"selected", nullptr}};
            }
            Node* root = getRootNode();
            Node* n = root ? root->findByInstanceId(static_cast<uint64_t>(id)) : nullptr;
            if (!n) {
                return json{{"status", "error"}, {"message", "No node with id " + std::to_string(id)}};
            }
            setSelectedNode(n);
            return json{{"status", "ok"}, {"selected", nodeToJson(*n, 0)}};
        });

    tool("set_node_members", "Set reflected members of a node — or one of its mods — from a JSON object (same encoding as get_node_tree: Vec3 [x,y,z], Color [r,g,b,a], rotation in degrees, enums by label string)")
        .arg<int>("id", "Instance id from get_node_tree")
        .arg<json>("members", "Member values to apply, e.g. {\"pos\":[10,20,0],\"visible\":true}")
        .arg<std::string>("mod", "Mod short type name (e.g. \"LayoutMod\") to target a mod attached to the node instead of the node itself", false)
        .bind([](const json& args) -> json {
            Node* root = getRootNode();
            uint64_t id = args.at("id").get<uint64_t>();
            Node* n = root ? root->findByInstanceId(id) : nullptr;
            if (!n) {
                return json{{"status", "error"}, {"message", "No node with id " + std::to_string(id)}};
            }

            JsonReadReflector r(args.at("members"));
            json after;
            if (args.contains("mod") && args.at("mod").is_string()) {
                std::string modName = args.at("mod").get<std::string>();
                Mod* mod = n->getModByTypeName(modName);
                if (!mod) {
                    return json{{"status", "error"},
                                {"message", "No mod \"" + modName + "\" on node " + std::to_string(id)}};
                }
                mod->reflectMembers(r);
                after = reflectToJson(*mod);
            } else {
                n->reflectMembers(r);
                after = reflectToJson(*n);
            }

            json result{
                {"status", "ok"},
                {"applied", r.applied},
                {"members", std::move(after)}
            };
            if (!r.skipped.empty()) result["skipped"] = r.skipped;
            if (!r.readOnly.empty()) result["readOnly"] = r.readOnly;
            auto unknown = r.unknownKeys();
            if (!unknown.empty()) result["unknown"] = unknown;
            return result;
        });

}

} // namespace mcp
} // namespace trussc
