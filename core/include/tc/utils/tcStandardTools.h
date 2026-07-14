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
#include <algorithm>
#include <cmath>
#include <cstdlib>

// Platform bits for detail::currentPid() / detail::processRssBytes()
#if defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#elif !defined(__EMSCRIPTEN__)
#include <fstream>
#include <unistd.h>
#endif

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
// were omitted so a caller can drill in with another tc_get_node_tree(id) call.
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

namespace detail {

// Area-average (box filter) downscale for interleaved 8-bit images. Good
// enough for monitoring thumbnails at any ratio; never called to upscale.
inline void downscaleImage(const unsigned char* src, int srcW, int srcH, int channels,
                           std::vector<unsigned char>& dst, int dstW, int dstH) {
    dst.resize(size_t(dstW) * dstH * channels);
    for (int y = 0; y < dstH; ++y) {
        int sy0 = int(size_t(y) * srcH / dstH);
        int sy1 = std::max(int(size_t(y + 1) * srcH / dstH), sy0 + 1);
        for (int x = 0; x < dstW; ++x) {
            int sx0 = int(size_t(x) * srcW / dstW);
            int sx1 = std::max(int(size_t(x + 1) * srcW / dstW), sx0 + 1);
            uint64_t acc[4] = {0, 0, 0, 0};
            for (int sy = sy0; sy < sy1; ++sy) {
                const unsigned char* p = src + (size_t(sy) * srcW + sx0) * channels;
                for (int sx = sx0; sx < sx1; ++sx, p += channels)
                    for (int c = 0; c < channels; ++c) acc[c] += p[c];
            }
            uint64_t n = uint64_t(sy1 - sy0) * (sx1 - sx0);
            unsigned char* o = &dst[(size_t(y) * dstW + x) * channels];
            for (int c = 0; c < channels; ++c) o[c] = (unsigned char)(acc[c] / n);
        }
    }
}

// Optional downscale + PNG/JPEG encode + Base64. Runs on the HTTP worker
// inside two-stage deferral thunks (tc_get_screenshot / tc_get_status_image),
// never on the main loop. reqWidth <= 0 means "no resize"; never upscales.
inline json pixelsToImageJson(const trussc::Pixels& px, const std::string& format,
                              int reqWidth, int quality) {
    int srcW = px.getWidth(), srcH = px.getHeight();
    int ch   = px.getChannels();
    int dstW = (reqWidth > 0) ? std::min(reqWidth, srcW) : srcW;
    int dstH = std::max(1, (int)std::lround((double)srcH * dstW / srcW));

    const unsigned char* data = px.getData();
    std::vector<unsigned char> small;
    if (dstW != srcW) {
        downscaleImage(px.getData(), srcW, srcH, ch, small, dstW, dstH);
        data = small.data();
    }

    std::vector<unsigned char> out;
    if (format == "png") {
        int len = 0;
        unsigned char* png = stbi_write_png_to_mem(data, 0, dstW, dstH, ch, &len);
        if (png) {
            out.assign(png, png + len);
            std::free(png);
        }
    } else {
        stbi_write_jpg_to_func(
            [](void* ctx, void* d, int size) {
                auto* v = static_cast<std::vector<unsigned char>*>(ctx);
                auto* b = static_cast<unsigned char*>(d);
                v->insert(v->end(), b, b + size);
            },
            &out, dstW, dstH, ch, data, quality);
    }
    if (out.empty()) {
        return json{{"status", "error"}, {"message", format + " encode failed"}};
    }
    return json{{"mimeType", format == "png" ? "image/png" : "image/jpeg"},
                {"data", trussc::toBase64(out)},
                {"width", dstW},
                {"height", dstH}};
}

// App-published ops status registries (see mcp::status / statusGraph /
// statusImage below). Main-thread only: registration happens in
// setup()/update() and the getters run inside MCP tool handlers, which
// execute on the main loop.
struct StatusEntry {
    std::string name;
    std::function<json()> getter;   // returns a number or string json value
    bool graph = false;             // display hint: plot as time series
};

inline std::vector<StatusEntry>& statusRegistry() {
    static std::vector<StatusEntry> reg;
    return reg;
}

struct StatusImageEntry {
    std::string name;
    std::function<trussc::Pixels()> getter;
};

inline std::vector<StatusImageEntry>& statusImageRegistry() {
    static std::vector<StatusImageEntry> reg;
    return reg;
}

inline void addStatusEntry(StatusEntry entry) {
    auto& reg = statusRegistry();
    for (auto& e : reg) {
        if (e.name == entry.name) { e = std::move(entry); return; }
    }
    reg.push_back(std::move(entry));
}

// Process identity + real memory footprint for tc_get_health. RSS is the
// resident set of the whole process — the number that matters for leak
// hunting and for telling "the app grew" from "the OS ran out".
inline int64_t currentPid() {
#if defined(_WIN32)
    return (int64_t)GetCurrentProcessId();
#elif defined(__EMSCRIPTEN__)
    return 0;
#else
    return (int64_t)getpid();
#endif
}

inline int64_t processRssBytes() {
#if defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  (task_info_t)&info, &count) == KERN_SUCCESS) {
        return (int64_t)info.resident_size;
    }
    return 0;
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (K32GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (int64_t)pmc.WorkingSetSize;
    }
    return 0;
#elif defined(__EMSCRIPTEN__)
    return 0;
#else
    // Linux: /proc/self/statm field 2 = resident pages
    std::ifstream in("/proc/self/statm");
    long total = 0, resident = 0;
    if (in >> total >> resident) {
        return (int64_t)resident * sysconf(_SC_PAGESIZE);
    }
    return 0;
#endif
}

} // namespace detail

// ---------------------------------------------------------------------------
// App-published ops status
//
// Apps expose custom monitoring data with one line per value; a supervisor
// (e.g. anchorbolt start) discovers the tc_get_status tool via tools/list
// and forwards the payload to its server. No supervisor-side configuration.
//
//   mcp::status("scene",         [&]{ return sceneName; });     // shown as-is
//   mcp::statusGraph("visitors",  [&]{ return visitorCount; }); // plotted over time
//   mcp::statusImage("entranceCam", [&]{ return camPixels; });
//
// Registering the same name again replaces the previous entry.
// ---------------------------------------------------------------------------

inline void status(const std::string& name, std::function<double()> getter) {
    detail::addStatusEntry({name, [getter]() { return json(getter()); }, false});
}

inline void status(const std::string& name, std::function<std::string()> getter) {
    detail::addStatusEntry({name, [getter]() { return json(getter()); }, false});
}

inline void statusGraph(const std::string& name, std::function<double()> getter) {
    detail::addStatusEntry({name, [getter]() { return json(getter()); }, true});
}

inline void statusImage(const std::string& name, std::function<trussc::Pixels()> getter) {
    auto& reg = detail::statusImageRegistry();
    for (auto& e : reg) {
        if (e.name == name) { e.getter = getter; return; }
    }
    reg.push_back({name, getter});
}

// ---------------------------------------------------------------------------
// Inspection Tools (read-only, always available when MCP is enabled)
// ---------------------------------------------------------------------------

inline void registerInspectionTools() {

    tool("tc_get_screenshot", "Screenshot as Base64 PNG/JPEG. Defaults to full-resolution PNG; pass width for a downscaled monitoring thumbnail (aspect preserved, never upscales) and format 'jpg' for small payloads. Cheap to poll at any settings: only the framebuffer readback touches the frame loop — downscale + encode run on the HTTP worker thread, so polling does not stutter the app.")
        .arg<std::string>("format", "'png' (default, lossless) or 'jpg'", false)
        .arg<int>("width", "Target width in pixels, aspect preserved (clamped 16-4096; never upscales; omit = full resolution)", false)
        .arg<int>("quality", "JPEG quality 1-100 (default 75; ignored for png)", false)
        .bind([](const json& args) -> json {
            std::string format = "png";
            if (args.contains("format") && args.at("format").is_string()) {
                format = args.at("format").get<std::string>();
                if (format != "png" && format != "jpg" && format != "jpeg") {
                    return json{{"status", "error"},
                                {"message", "format must be 'png' or 'jpg'"}};
                }
                if (format == "jpeg") format = "jpg";
            }
            int reqWidth = 0;  // 0 = full resolution
            int quality  = 75;
            if (args.contains("width") && args.at("width").is_number())
                reqWidth = std::clamp(args.at("width").get<int>(), 16, 4096);
            if (args.contains("quality") && args.at("quality").is_number())
                quality = std::clamp(args.at("quality").get<int>(), 1, 100);

            // Two-stage deferral: the main stage runs at the afterFrame safe
            // point (mid-frame the framebuffer is blank — drawing is deferred)
            // and only grabs the pixels; the returned closure — downscale +
            // encode + Base64 — runs on the HTTP worker blocked on this reply.
            mcp::deferToolResultTwoStage([format, reqWidth, quality]() -> std::function<json()> {
                auto px = std::make_shared<trussc::Pixels>();
                bool grabbed = trussc::grabScreen(*px);
                return [px, grabbed, format, reqWidth, quality]() -> json {
                    if (!grabbed) {
                        return json{{"status", "error"}, {"message", "Failed to grab screen"}};
                    }
                    return detail::pixelsToImageJson(*px, format, reqWidth, quality);
                };
            });
            return json(nullptr);  // ignored — deferred result is sent instead
        });

    tool("tc_save_screenshot", "Save screenshot to file")
        .arg<std::string>("path", "File path")
        .bind<std::string>([](std::string path) {
            if (trussc::saveScreenshot(path)) {
                return json{{"status", "ok"}, {"path", path}};
            } else {
                return json{{"status", "error"}, {"message", "Failed to save screenshot"}};
            }
        });

    tool("tc_get_status", "App-published ops status: values registered via mcp::status()/mcp::statusGraph() plus the names of mcp::statusImage() images. mode 'graph' means the value wants to be plotted over time. Empty when the app publishes nothing. Supervisors discover this tool via tools/list and forward the payload to their monitoring server.")
        .bind(std::function<json()>([]() -> json {
            json values = json::array();
            for (auto& e : detail::statusRegistry()) {
                json v;
                try {
                    v = e.getter();
                } catch (...) {
                    continue;  // a throwing getter drops its entry, not the tool
                }
                values.push_back({{"name", e.name},
                                  {"value", v},
                                  {"mode", e.graph ? "graph" : "status"}});
            }
            json images = json::array();
            for (auto& e : detail::statusImageRegistry()) images.push_back(e.name);
            return json{{"values", values}, {"images", images}};
        }));

    tool("tc_get_status_image", "Fetch an app-published image registered via mcp::statusImage(), downscaled + JPEG-encoded like tc_get_thumbnail (pixel grab on the main loop, encode on the HTTP worker — no frame stutter).")
        .arg<std::string>("name", "Image name as listed by tc_get_status")
        .arg<int>("width", "Target width in pixels, aspect preserved (default 512, clamped 16-4096; never upscales)", false)
        .arg<int>("quality", "JPEG quality 1-100 (default 75)", false)
        .bind([](const json& args) -> json {
            std::string name = args.value("name", "");
            int reqWidth = 512;
            int quality  = 75;
            if (args.contains("width") && args.at("width").is_number())
                reqWidth = std::clamp(args.at("width").get<int>(), 16, 4096);
            if (args.contains("quality") && args.at("quality").is_number())
                quality = std::clamp(args.at("quality").get<int>(), 1, 100);

            std::function<trussc::Pixels()> getter;
            for (auto& e : detail::statusImageRegistry()) {
                if (e.name == name) { getter = e.getter; break; }
            }
            if (!getter) {
                return json{{"status", "error"},
                            {"message", "unknown image '" + name + "' (see tc_get_status)"}};
            }
            mcp::deferToolResultTwoStage([getter, reqWidth, quality]() -> std::function<json()> {
                auto px = std::make_shared<trussc::Pixels>();
                bool ok = true;
                try {
                    *px = getter();  // app getter runs on the main loop
                } catch (...) {
                    ok = false;
                }
                if (ok && (px->getWidth() <= 0 || px->getHeight() <= 0 || !px->getData())) ok = false;
                return [px, ok, reqWidth, quality]() -> json {
                    if (!ok) {
                        return json{{"status", "error"},
                                    {"message", "image getter returned no pixels"}};
                    }
                    return detail::pixelsToImageJson(*px, "jpg", reqWidth, quality);
                };
            });
            return json(nullptr);  // ignored — deferred result is sent instead
        });

    tool("tc_get_health", "Lightweight liveness snapshot: fps (measured average), frame count, uptime seconds, window size, TrussC version, pid, process RSS bytes, sokol-tracked bytes. Cheap enough to poll — reads counters only, touches no GPU state. pid lets a supervisor confirm it is talking to ITS child (port collisions); rssBytes is the number to graph for leak hunting.")
        .bind(std::function<json()>([]() -> json {
            return json{{"status", "ok"},
                        {"fps", trussc::getFps()},
                        {"frameCount", trussc::getFrameCount()},
                        {"uptimeSec", trussc::getElapsedTime()},
                        {"width", trussc::getWindowWidth()},
                        {"height", trussc::getWindowHeight()},
                        {"version", trussc::getVersion()},
                        {"pid", detail::currentPid()},
                        {"rssBytes", detail::processRssBytes()},
                        {"memoryBytes", trussc::getSokolMemoryBytes()}};
        }));

    tool("tc_quit", "Quit the application gracefully")
        .bind(std::function<json()>([]() -> json {
            sapp_request_quit();
            return json{{"status", "ok"}};
        }));

    // --- Recording tools (native encoder, no ffmpeg) ---

    tool("tc_start_recording", "Start recording the window to a video file (the screenshot's video counterpart). Omit path for a timestamped file in the data dir; give duration for a fixed-length clip that auto-stops and finalizes itself.")
        .arg<std::string>("path", "Output file path (relative paths resolve to the data dir). Omit for recording-<timestamp>.mp4/.mov in the data dir.", false)
        .arg<float>("duration", "Fixed length in seconds; the file auto-stops & finalizes at that length. Omit or 0 = unlimited (tc_stop_recording to end).", false)
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

    tool("tc_stop_recording", "Stop the current recording and finalize the file. Wins over a pending fixed duration (finalizes immediately at the current length); a no-op if nothing is recording.")
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

    tool("tc_get_node_tree", "Dump the node tree as JSON: per node {type, name, id, members (reflected, rotation in degrees, colors 0-1), mods, children}. Where depth cuts children off, childCount marks how many were omitted — drill in with another call passing that node's id")
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

    tool("tc_get_selected_node", "Get the currently selected node (type, name, id, reflected members), or null if nothing is selected")
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

    tool("tc_mouse_move", "Move mouse cursor (with a button held, emits a drag)")
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

    // Split press/release. A drag gesture is press → tc_mouse_move(button) × N →
    // release; tc_mouse_click fires press+release back-to-back, so drag consumers
    // (e.g. EasyCam orbit) never see an open gesture. Anchoring the global
    // mouse position at press keeps the first drag delta sane.
    tool("tc_mouse_press", "Press and hold a mouse button (start of a drag; pair with tc_mouse_move + tc_mouse_release)")
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

    tool("tc_mouse_release", "Release a mouse button (end of a drag)")
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

    tool("tc_mouse_click", "Click mouse button (optionally with modifier keys held)")
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

    tool("tc_mouse_scroll", "Scroll mouse wheel")
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

    tool("tc_key_press", "Press a key")
        .arg<int>("key", "Key code (sokol_app keycode)")
        .bind<int>([](int key) {
            KeyEventArgs args;
            args.key = key;
            events().keyPressed.notify(args);
            if (::trussc::internal::appKeyPressedFunc)
                ::trussc::internal::appKeyPressedFunc(args);
            return json{{"status", "ok"}};
        });

    tool("tc_key_release", "Release a key")
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

    tool("tc_select_node", "Select a node by instance id (0 clears the selection); drives the same selection an inspector shows")
        .arg<int>("id", "Instance id from tc_get_node_tree (0 to clear)")
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

    tool("tc_set_node_members", "Set reflected members of a node — or one of its mods — from a JSON object (same encoding as tc_get_node_tree: Vec3 [x,y,z], Color [r,g,b,a], rotation in degrees, enums by label string)")
        .arg<int>("id", "Instance id from tc_get_node_tree")
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
