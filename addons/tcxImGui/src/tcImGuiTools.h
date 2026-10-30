#pragma once

// =============================================================================
// tcImGuiTools.h - ImGui MCP Tools
// Register ImGui MCP tools; exposure is controlled by mcp::setToolLevel()
//
// Uses ImGui Test Engine hooks (IMGUI_ENABLE_TEST_ENGINE) plus the [TrussC]
// value hook to collect widget info and values each frame, then provides MCP
// tools to query and interact with them, and to read back what the user
// changed by hand ("touched").
// Definitions live in tcImGuiToolsImpl.h, compiled only by the platform TU.
// =============================================================================

#include <TrussC.h>
#include "tcImGuiHooks.h"
#include "tc/utils/tcMCP.h"
#include "tc/utils/tcLog.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <limits>
#include <type_traits>

namespace tcx::imgui {

// ---------------------------------------------------------------------------
// Other records of hand-made changes, merged into tcx_imgui_get_touched and
// cleared by tcx_imgui_reset_touched (tcxNodeInspector registers "inspector").
// `get` returns a JSON array; registering the same key again replaces it.
// ---------------------------------------------------------------------------
struct TouchedSource {
    std::string key;
    std::function<nlohmann::json()> get;
    std::function<void()> reset;
};

std::vector<TouchedSource>& touchedSources();

void addTouchedSource(const std::string& key,
                             std::function<nlohmann::json()> get,
                             std::function<void()> reset);

// ---------------------------------------------------------------------------
// Contexts and windows
// ---------------------------------------------------------------------------

// Positive native Window ID from tc_list_windows; -1 means unknown or closed.
int64_t windowIdFor(ImGuiContext* ctx);

// Contexts ordered by windowId (stable output across calls)
std::vector<std::pair<int64_t, ImGuiContext*>> orderedContexts();

namespace detail {
// Context operations are implemented in the addon, including for templated callers.
class ToolContext {
public:
    explicit ToolContext(ImGuiContext* ctx);
    void restore();
private:
    ImGuiContext* previous_;
};
} // namespace detail

// Runs f with ctx as the current ImGui context (input goes to that window's imgui)
template <class F>
inline void withContext(ImGuiContext* ctx, F&& f) {
    detail::ToolContext previous(ctx);
    f();
    previous.restore();
}

// ---------------------------------------------------------------------------
// Widget lookup helpers
// ---------------------------------------------------------------------------

struct WidgetRef {
    const WidgetInfo* widget = nullptr;
    ImGuiContext* ctx = nullptr;
};

// Find widget by label, optionally filtered by ImGui window (panel) name and
// OS window id. Returns an empty ref if not found or ambiguous (sets outError)
WidgetRef findWidget(const std::string& label,
                            const std::string& window,
                            int64_t windowId,
                            std::string& outError);

// ---------------------------------------------------------------------------
// Input injection helpers
// ---------------------------------------------------------------------------

void clickWidget(const WidgetInfo& w);

bool inputText(const WidgetInfo& w, const std::string& text);

// ---------------------------------------------------------------------------
// Widget type classification from status flags
// ---------------------------------------------------------------------------
std::string classifyWidget(ImGuiItemStatusFlags flags);

// ---------------------------------------------------------------------------
// Values -> JSON
// ---------------------------------------------------------------------------
namespace detail {

// The shortest decimal that reads back as the same float (0.1f -> 0.1, not
// 0.10000000149011612), as a double for the JSON writer. Full precision: the
// float is recovered exactly from it.
double shortestFloat(float f);

const char* dataTypeName(ImGuiDataType t);

nlohmann::json scalarToJson(ImGuiDataType t, const unsigned char* p);

nlohmann::json componentsToJson(const WidgetValue& v);

// Adds widget / valueType / value (and kind-specific extras) to an entry.
// Only what a value hook reported: an item without one (a button, an action
// menu item) gets none of these fields.
void addValueFields(nlohmann::json& e, const WidgetValue& v);

nlohmann::json rectToJson(const ImRect& r);

// Whether a touched widget was drawn in the last completed frame
bool drawnLastFrame(const TouchedWidget& t);

nlohmann::json touchedWidgetsJson();

// ---------------------------------------------------------------------------
// Setting values through the value hook (tcx_imgui_input on a value widget)
// ---------------------------------------------------------------------------

// A composite widget (DragFloat3, SliderInt2, InputFloat4, ColorEdit4,
// ColorPicker4, ...) has no item of its own: its entry is the group of its
// parts, so a click at its centre lands on one part, or in a gap.
bool isComposite(const WidgetValue& v);

const char* kindName(int kind);

// What tcx_imgui_input takes for this widget, in the units the tools report
std::string expectedValue(const WidgetValue& v);

template <class T>
inline bool jsonToInt(const nlohmann::json& v, unsigned char* out, std::string& err) {
    using L = std::numeric_limits<T>;
    T x;
    if (v.is_number_unsigned()) {
        uint64_t u = v.get<uint64_t>();
        // Bound (abi): external MCP values must fit the destination numeric width. Overflow returns an out-of-range error.
        if (u > (uint64_t)L::max()) { err = "out of range"; return false; }
        x = (T)u;
    } else if (v.is_number_integer()) {
        int64_t i = v.get<int64_t>();
        // Bound (abi): external MCP values must fit the destination numeric width. Overflow returns an out-of-range error.
        if (std::is_signed<T>::value ? (i < (int64_t)L::min() || i > (int64_t)L::max()) : i < 0) {
            err = "out of range";
            return false;
        }
        x = (T)i;
    } else {
        double d = v.get<double>();
        if (d != std::floor(d)) { err = "not an integer"; return false; }
        // max() + 1 is a power of two, exact as a double (max() itself may round up)
        // Bound (abi): external MCP values must fit the destination numeric width. Overflow returns an out-of-range error.
        if (!(d >= (double)L::min() && d < (double)L::max() + 1.0)) { err = "out of range"; return false; }
        x = (T)d;
    }
    std::memcpy(out, &x, sizeof(x));
    return true;
}

// One component of type t from v, into out. False, with err, when v is not a
// value of that type.
bool jsonToScalar(const nlohmann::json& v, ImGuiDataType t, unsigned char* out, std::string& err);

// The bytes of the widget's variable holding `v`: a single value for one
// component, an array of exactly `components` values otherwise.
bool valueToBytes(const WidgetValue& w, const nlohmann::json& v,
                         std::vector<unsigned char>& out, std::string& err);

// A queued value is written only within kPendingValueLifetime of the call, so
// it never lands after the tool has answered: a widget that runs later gets the
// Expired error, and nothing is written. The check in the widget's next frame
// is waited for until kValueCheckDeadline after the call; a value written but
// not checked by then (its window rendered no frame since the write: slower
// than one frame per 2 s, or it stopped rendering) is settled as applied on
// its read-back at the widget's return, from the addressed Window's afterFrame
// (settleOverdueValues()), as for a widget not drawn again. Both come before
// tc::mcp's timeout for a deferred reply that is never produced
// (kTargetedDeferralTimeout, 5 s), whose generic error would hide that the
// value was written. So a window that renders more often than once every 2 s
// gets its value written and checked in time.
// Bound (reply latency): stop writing after 2 s; settle verification by 4 s, before the core's 5 s timeout.
inline constexpr std::chrono::seconds kValueCheckDeadline{4};
static_assert(kPendingValueLifetime < kValueCheckDeadline,
              "a written value must get time for its check frame");
static_assert(kValueCheckDeadline + std::chrono::seconds{1} <= tc::mcp::internal::kTargetedDeferralTimeout,
              "the check must be settled before the deferred reply's timeout");

// The refusal of a RadioButton(int*) given a value other than its own: in
// ImGui, true from that button means its variable holds its own value, so
// only the button whose value it is can set it.
std::string otherButtonMessage(const std::string& label, int buttonValue, int wanted);

// The reply of tcx_imgui_input for a value written through the value hook,
// once its outcome is known: after the frame after the call (not drawn, a
// same-frame change, a refusal), or after the frame after the write, which
// checks that the variable still holds the value.
nlohmann::json injectionResult(ImGuiContext* ctx, const std::shared_ptr<PendingValue>& p,
                                      const std::string& label, const std::string& window);

} // namespace detail

// ---------------------------------------------------------------------------
// MCP tool registration (called by imguiSetup)
// ---------------------------------------------------------------------------
void registerImGuiTools();

} // namespace tcx::imgui

// -----------------------------------------------------------------------------
// Backward compatibility: tcxImGui's integration helpers historically lived in
// `trussc::imgui_tools`. Canonical is now `tcx::imgui`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]]: under `using namespace tc;` it would warn on idiomatic use.)
// -----------------------------------------------------------------------------
namespace trussc { namespace imgui_tools { // deprecated: remove at v1.0.0
using tcx::imgui::registerImGuiTools;   // one per public symbol
} }  // namespace trussc::imgui_tools
