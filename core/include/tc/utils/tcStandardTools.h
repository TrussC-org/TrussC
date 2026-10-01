#pragma once

// =============================================================================
// tcStandardTools.h - Standard MCP Tools for TrussC
// =============================================================================

#include "tcMCP.h"
#include "tcUtils.h"
#include "tcTime.h"
#include "tcJsonReflect.h"
#include "../events/tcCoreEvents.h"
#include "stb/stb_image_write.h"
#include "../graphics/tcPixels.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <mutex>

// Platform bits for detail::currentPid() / detail::processRssBytes()
#if defined(__APPLE__)
#include <mach/mach.h>
#include <unistd.h>
#elif defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
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

// Reflected members of obj. With includeDerived (the live MCP view), derived
// values (TC_DERIVED) are included and their member paths listed under
// "derived" in out; without it they are left out, as in saved data.
namespace internal {
template <class T>
inline void reflectMembersToJson(T& obj, Json& out, bool includeDerived) {
    JsonWriteReflector w;
    w.includeDerived = includeDerived;
    obj.reflectMembers(w);
    if (!w.members.empty()) out["members"] = std::move(w.members);
    if (includeDerived && !w.derived.empty()) out["derived"] = std::move(w.derived);
}
} // namespace internal

// One node as JSON: type, optional instance name, instance id, reflected
// members (same encoding as JsonWriteReflector), mods ({type, members} each),
// and the children in draw order. Derived values (TC_DERIVED, e.g. globalPos)
// are left out unless includeDerived is true, as reflectToJson() does; then
// they are included and named under "derived" (the MCP tools pass true).
// maxDepth limits recursion (-1 = unlimited, 0 = this node only); where
// children are cut off, "childCount" says how many were omitted so a caller
// can drill in with another tc_get_node_tree(id) call.
inline Json nodeToJson(Node& node, int maxDepth = -1, bool includeDerived = false) {
    Json j = Json::object();
    j["type"] = node.getTypeName();
    if (node.hasName()) j["name"] = node.getName();
    j["id"] = node.getInstanceId();
    j["members"] = Json::object();
    internal::reflectMembersToJson(node, j, includeDerived);
    auto mods = node.getMods();
    if (!mods.empty()) {
        Json jmods = Json::array();
        for (Mod* m : mods) {
            Json jm = Json::object();
            Mod& mod = *m;
            jm["type"] = shortTypeName(typeid(mod));
            internal::reflectMembersToJson(mod, jm, includeDerived);
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
                children.push_back(nodeToJson(*c, maxDepth < 0 ? -1 : maxDepth - 1, includeDerived));
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
    std::vector<unsigned char> scaled;
    if (dstW != srcW) {
        downscaleImage(px.getData(), srcW, srcH, ch, scaled, dstW, dstH);
        data = scaled.data();
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

// Wrap an encoded image as MCP-standard content blocks: an image block
// (clients like Claude Code render it inline) followed by a text block with
// the metadata (width/height as JSON, for scripted consumers). Error objects
// pass through untouched and get the normal text wrapping.
inline json imageContentResult(json img) {
    if (!img.contains("data")) return img;  // error object
    json meta = json::object();
    for (auto& key : {"width", "height"}) {
        if (img.contains(key)) meta[key] = img[key];
    }
    return json::array({
        json{{"type", "image"}, {"data", img["data"]}, {"mimeType", img["mimeType"]}},
        json{{"type", "text"}, {"text", meta.dump()}},
    });
}

// App-published ops status registries (see mcp::status / statusGraph /
// statusImage below). Main-thread only: registration happens in
// setup()/update() and the getters run inside MCP tool handlers, which
// execute on the main loop.
struct StatusEntry {
    std::string name;
    std::function<json()> getter;   // returns a number or string json value
    bool graph = false;             // display hint: plot as time series
    const void* owner = nullptr;    // mcp::detail::registrationOwner() at registration
};

// Both registries drop an owner's entries in mcp::detail::removeRegistrationsOwnedBy()
// (hot reload: a guest generation's getters capture its App, #227).
template <class Entry>
inline void hookOwnerCleanup(std::vector<Entry>& reg) {
    mcp::detail::ownerCleanupHooks().push_back([&reg](const void* owner) {
        reg.erase(std::remove_if(reg.begin(), reg.end(),
                                 [owner](const Entry& e) { return e.owner == owner; }),
                  reg.end());
    });
}

// The registries and the alert queue below are defined in tcMCP.cpp, one per
// process: a hot reload guest publishes into the host's (#249).
std::vector<StatusEntry>& statusRegistry();

struct StatusImageEntry {
    std::string name;
    std::function<trussc::Pixels()> getter;
    const void* owner = nullptr;    // as StatusEntry::owner
};

std::vector<StatusImageEntry>& statusImageRegistry();

inline void addStatusEntry(StatusEntry entry) {
    entry.owner = mcp::detail::registrationOwner();
    auto& reg = statusRegistry();
    for (auto& e : reg) {
        if (e.name == entry.name) { e = std::move(entry); return; }
    }
    reg.push_back(std::move(entry));
}

// Operator-alert queue (see mcp::alert below). Unlike the status getters,
// alerts can fire from any thread — a sensor callback, an async timer — so
// the queue carries its own lock. Bounded: past 100 pending, oldest drop.
std::mutex& alertMutex();
std::deque<json>& alertQueue();

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
    const void* owner = mcp::detail::registrationOwner();
    for (auto& e : reg) {
        if (e.name == name) { e.getter = getter; e.owner = owner; return; }
    }
    reg.push_back({name, getter, owner});
}

// ---------------------------------------------------------------------------
// Operator alerts
//
//   mcp::alert("IR camera disconnected!");
//
// Queues a message for the supervisor: anchorbolt drains tc_get_alerts on
// its health cadence and forwards each entry to its notification sinks
// (Slack / Discord / ntfy...), so this can literally end up on someone's
// phone. Deliberately named ALERT, not notify — it is for "a human should
// hear about this", not a general message bus. The message is also written
// to the log (so it survives locally even with no supervisor attached).
// Thread-safe; callable from sensor callbacks / async timers.
// ---------------------------------------------------------------------------

inline void alert(const std::string& msg) {
    trussc::logWarning("alert") << msg;
    std::lock_guard<std::mutex> lock(detail::alertMutex());
    auto& q = detail::alertQueue();
    q.push_back(json{{"at", trussc::getTimestampString("%Y-%m-%dT%H:%M:%S")},
                     {"text", msg}});
    if (q.size() > 100) q.pop_front();
}

// ---------------------------------------------------------------------------
// Inspection Tools (read-only, always available when MCP is enabled)
//
// Defined in tcStandardTools.cpp, not inline: the handlers are then host code
// whoever calls this, so under hot reload they read the core loop's own state
// (window list, recorder, input dispatch) rather than a guest DLL's copy of it.
// ---------------------------------------------------------------------------

void registerInspectionTools();

// ---------------------------------------------------------------------------
// Key injection helpers (shared by tc_key_press / tc_key_release)
//
// These mirror the real SAPP_EVENTTYPE_KEY_DOWN / KEY_UP path in TrussC.h
// step for step, including the ordering: notify listeners, update the
// held-key set, then call the app/Node-tree callback. Modifier flags are
// DERIVED from the held-key set, the way sokol gets ev->modifiers from the
// keys the OS reports as down — so an injected modifier hold (tc_key_press
// with the modifier keycode) shows up on every event that follows it, and
// e.shift agrees with what isShiftPressed() reports.
// ---------------------------------------------------------------------------
namespace detail {

// Modifier flags for the event carrying `key`, as of AFTER this event's own
// state change — which is what the OS reports: a Shift key-down already has
// the Shift flag set, its key-up already has it cleared. Every other key is
// read straight from the held-key set.
inline void fillKeyModifiers(KeyEventArgs& args, int key, bool down) {
    auto heldAfter = [key, down](int code) {
        return code == key ? down : isKeyPressed(code);
    };
    args.shift = heldAfter(SAPP_KEYCODE_LEFT_SHIFT)   || heldAfter(SAPP_KEYCODE_RIGHT_SHIFT);
    args.ctrl  = heldAfter(SAPP_KEYCODE_LEFT_CONTROL) || heldAfter(SAPP_KEYCODE_RIGHT_CONTROL);
    args.alt   = heldAfter(SAPP_KEYCODE_LEFT_ALT)     || heldAfter(SAPP_KEYCODE_RIGHT_ALT);
    args.super = heldAfter(SAPP_KEYCODE_LEFT_SUPER)   || heldAfter(SAPP_KEYCODE_RIGHT_SUPER);
}

inline void injectKeyDown(int key) {
    KeyEventArgs args;
    args.key = key;
    fillKeyModifiers(args, key, true);
    events().keyPressed.notify(args);

    ::trussc::internal::currentWindowContext().keysPressed.insert(key);

    if (::trussc::internal::appKeyPressedFunc)
        ::trussc::internal::appKeyPressedFunc(args);
}

inline void injectKeyUp(int key) {
    KeyEventArgs args;
    args.key = key;
    fillKeyModifiers(args, key, false);
    events().keyReleased.notify(args);

    ::trussc::internal::currentWindowContext().keysPressed.erase(key);

    if (::trussc::internal::appKeyReleasedFunc)
        ::trussc::internal::appKeyReleasedFunc(args);
}

// Currently held keycodes, for the tool reply (handy when scripting chords).
inline json heldKeysJson() {
    std::vector<int> held(::trussc::internal::currentWindowContext().keysPressed.begin(),
                          ::trussc::internal::currentWindowContext().keysPressed.end());
    std::sort(held.begin(), held.end());
    return json(held);
}

} // namespace detail

// ---------------------------------------------------------------------------
// Control Tools (opt-in via mcp::registerControlTools())
//
// The always-on standard set is strictly read-only; everything that lets the
// outside OPERATE the app — input injection, node selection/mutation, quit —
// lives behind this one opt-in. (Formerly registerDebuggerTools(); renamed
// because the read-only tools are debugging aids too — the real boundary is
// read vs control.)
//
// Defined in tcStandardTools.cpp (see registerInspectionTools above): apps call
// this from setup(), which is guest code under hot reload, yet the injected
// input must go through the host's dispatch pointers.
// ---------------------------------------------------------------------------

void registerControlTools();

[[deprecated("renamed to registerControlTools(); will be removed in v1.0.0")]]
inline void registerDebuggerTools() { registerControlTools(); }

} // namespace mcp
} // namespace trussc
