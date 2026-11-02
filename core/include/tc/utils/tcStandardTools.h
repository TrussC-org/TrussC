#pragma once

// =============================================================================
// tcStandardTools.h - Standard MCP Tools for TrussC
// =============================================================================

#include "tcMCP.h"
#include "tcJsonReflect.h"
#include <algorithm>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace trussc {

class Node;
class Pixels;
struct KeyEventArgs;

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
Json nodeToJson(Node& node, int maxDepth = -1, bool includeDerived = false);

namespace mcp {

namespace internal {

// Area-average (box filter) downscale for interleaved 8-bit images. Good
// enough for monitoring thumbnails at any ratio; never called to upscale.
void downscaleImage(const unsigned char* src, int srcW, int srcH, int channels,
                    std::vector<unsigned char>& dst, int dstW, int dstH);

// Optional downscale + PNG/JPEG encode + Base64. Runs on the HTTP worker
// inside two-stage deferral thunks (tc_get_screenshot / tc_get_status_image),
// never on the main loop. reqWidth <= 0 means "no resize"; never upscales.
json pixelsToImageJson(const trussc::Pixels& px, const std::string& format,
                       int reqWidth, int quality);

// Wrap an encoded image as MCP-standard content blocks: an image block
// (clients like Claude Code render it inline) followed by a text block with
// the metadata (width/height as JSON, for scripted consumers). Error objects
// pass through untouched and get the normal text wrapping.
json imageContentResult(json img);

// App-published ops status registries (see mcp::status / statusGraph /
// statusImage below). Main-thread only: registration happens in
// setup()/update() and the getters run inside MCP tool handlers, which
// execute on the main loop.
struct StatusEntry {
    std::string name;
    std::function<json()> getter;   // returns a number or string json value
    bool graph = false;             // display hint: plot as time series
    const void* owner = nullptr;    // mcp::internal::registrationOwner() at registration
};

// Both registries drop an owner's entries in mcp::internal::removeRegistrationsOwnedBy()
// (hot reload: a guest generation's getters capture its App, #227).
template <class Entry>
inline void hookOwnerCleanup(std::vector<Entry>& reg) {
    mcp::internal::ownerCleanupHooks().push_back([&reg](const void* owner) {
        // One match at a time: move it out, close the gap, and only then let
        // its getter die. A getter's capture can run reentrant retirement code,
        // so no iterator is held across the destruction and the scan restarts.
        // Allocation-free: nothing here can fail halfway.
        for (;;) {
            auto it = std::find_if(reg.begin(), reg.end(),
                                   [owner](const Entry& e) { return e.owner == owner; });
            if (it == reg.end()) break;
            Entry retired = std::move(*it);
            reg.erase(it);
        }   // retired's captures destroyed here
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

void addStatusEntry(StatusEntry entry);

// Operator-alert queue (see mcp::alert below). Unlike the status getters,
// alerts can fire from any thread — a sensor callback, an async timer — so
// the queue carries its own lock. Bounded: past 100 pending, oldest drop.
std::mutex& alertMutex();
std::deque<json>& alertQueue();

// Process identity + real memory footprint for tc_get_health. RSS is the
// resident set of the whole process — the number that matters for leak
// hunting and for telling "the app grew" from "the OS ran out".
int64_t currentPid();

int64_t processRssBytes();

} // namespace internal

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

/// Publish a numeric or string getter through ReadOnly tc_get_status.
void status(const std::string& name, std::function<double()> getter);

/// Publish a numeric or string getter through ReadOnly tc_get_status.
void status(const std::string& name, std::function<std::string()> getter);

/// Publish a numeric getter marked for time-series plotting.
void statusGraph(const std::string& name, std::function<double()> getter);

/// Publish a Pixels getter through ReadOnly tc_get_status_image.
void statusImage(const std::string& name, std::function<trussc::Pixels()> getter);

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

/// Log an operator warning and queue it for ReadOnly tc_get_alerts (thread-safe).
void alert(const std::string& msg);

// ---------------------------------------------------------------------------
// Compatibility alias; registration is independent of exposure.
// ---------------------------------------------------------------------------

[[deprecated("use setToolLevel(ToolLevel::ReadOnly); will be removed in v1.0.0")]]
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
namespace internal {

// Modifier flags for the event carrying `key`, as of AFTER this event's own
// state change — which is what the OS reports: a Shift key-down already has
// the Shift flag set, its key-up already has it cleared. Every other key is
// read straight from the held-key set.
void fillKeyModifiers(KeyEventArgs& args, int key, bool down);

void injectKeyDown(int key);

void injectKeyUp(int key);

// Currently held keycodes, for the tool reply (handy when scripting chords).
json heldKeysJson();

} // namespace internal

// ---------------------------------------------------------------------------
// Compatibility aliases for granting control access.
// ---------------------------------------------------------------------------

[[deprecated("use setToolLevel(ToolLevel::Control); will be removed in v1.0.0")]]
void registerControlTools();

[[deprecated("use setToolLevel(ToolLevel::Control); will be removed in v1.0.0")]]
void registerDebuggerTools();

} // namespace mcp
} // namespace trussc
