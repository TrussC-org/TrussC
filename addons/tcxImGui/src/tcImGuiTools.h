#pragma once

// =============================================================================
// tcImGuiTools.h - ImGui MCP Tools
// Auto-expose ImGui widgets as MCP tools (enabled via registerControlTools())
//
// Uses ImGui Test Engine hooks (IMGUI_ENABLE_TEST_ENGINE) plus the [TrussC]
// value hook to collect widget info and values each frame, then provides MCP
// tools to query and interact with them, and to read back what the user
// changed by hand ("touched").
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

inline std::vector<TouchedSource>& touchedSources() {
    static std::vector<TouchedSource> sources;
    return sources;
}

inline void addTouchedSource(const std::string& key,
                             std::function<nlohmann::json()> get,
                             std::function<void()> reset) {
    for (auto& s : touchedSources()) {
        if (s.key == key) { s.get = std::move(get); s.reset = std::move(reset); return; }
    }
    touchedSources().push_back({key, std::move(get), std::move(reset)});
}

// ---------------------------------------------------------------------------
// Contexts and windows
// ---------------------------------------------------------------------------

// The OS window an ImGui context draws into, as tc_list_windows numbers it
// (0 = main, 1..N = open secondary windows). -1 = unknown / closed.
inline int windowIdFor(ImGuiContext* ctx) {
    auto& contexts = detail::contexts();
    auto it = contexts.find(ctx);
    if (it == contexts.end() || !it->second.owner) return -1;
    const void* owner = it->second.owner;
    if (owner == &tc::internal::mainWindowContext()) return 0;
    int i = 1;
    for (tc::Window* w : tc::internal::openWindows()) {
        if (owner == static_cast<const void*>(&w->context())) return i;
        ++i;
    }
    return -1;
}

// Contexts ordered by windowId (stable output across calls)
inline std::vector<std::pair<int, ImGuiContext*>> orderedContexts() {
    std::vector<std::pair<int, ImGuiContext*>> out;
    for (auto& [ctx, cs] : detail::contexts()) out.emplace_back(windowIdFor(ctx), ctx);
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) {
        return a.first != b.first ? (unsigned)a.first < (unsigned)b.first : a.second < b.second;
    });
    return out;
}

// Runs f with ctx as the current ImGui context (input goes to that window's imgui)
template <class F>
inline void withContext(ImGuiContext* ctx, F&& f) {
    ImGuiContext* prev = ImGui::GetCurrentContext();
    if (ctx) ImGui::SetCurrentContext(ctx);
    f();
    ImGui::SetCurrentContext(prev);
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
inline WidgetRef findWidget(const std::string& label,
                            const std::string& window,
                            int windowId,
                            std::string& outError) {
    std::vector<WidgetRef> found;
    for (auto& [wid, ctx] : orderedContexts()) {
        if (windowId >= 0 && wid != windowId) continue;
        for (auto& w : detail::contexts()[ctx].lastFrame) {
            if (w.label != label) continue;
            if (!window.empty() && w.windowName != window) continue;
            found.push_back({&w, ctx});
        }
    }

    if (found.empty()) {
        outError = "Widget '" + label + "' not found";
        if (!window.empty()) outError += " in window '" + window + "'";
        if (windowId >= 0) outError += " in windowId " + std::to_string(windowId);
        return {};
    }
    if (found.size() > 1) {
        outError = "Ambiguous label '" + label + "' found in: ";
        for (size_t i = 0; i < found.size(); i++) {
            if (i) outError += ", ";
            outError += "'" + found[i].widget->windowName + "' (windowId " +
                        std::to_string(windowIdFor(found[i].ctx)) + ")";
        }
        outError += ". Specify 'window' (and 'windowId' across OS windows).";
        return {};
    }
    return found[0];
}

// ---------------------------------------------------------------------------
// Input injection helpers
// ---------------------------------------------------------------------------

inline void clickWidget(const WidgetInfo& w) {
    auto& io = ImGui::GetIO();
    auto center = w.rect.GetCenter();
    io.AddMousePosEvent(center.x, center.y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
}

inline void inputText(const WidgetInfo& w, const std::string& text) {
    auto& io = ImGui::GetIO();
    auto center = w.rect.GetCenter();
    // ImGui's shortcut modifier is internal-Ctrl. With ConfigMacOSXBehaviors,
    // AddKeyEvent swaps Cmd<->Ctrl, so submit Super to land on internal Ctrl.
    // (Submitting Ctrl there would become internal Super, and Super+LeftClick
    // gets aliased into a right click.)
    ImGuiKey mod = io.ConfigMacOSXBehaviors ? ImGuiMod_Super : ImGuiMod_Ctrl;
    // Ctrl+Click: focuses text inputs, and turns slider/drag widgets into a
    // temp text input (a plain click would just jump the slider value).
    // The modifier must be down DURING the click for temp input activation.
    io.AddMousePosEvent(center.x, center.y);
    io.AddKeyEvent(mod, true);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
    // Select all (shortcut+A) while the modifier is still held
    io.AddKeyEvent(ImGuiKey_A, true);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(mod, false);
    // Delete selection
    io.AddKeyEvent(ImGuiKey_Delete, true);
    io.AddKeyEvent(ImGuiKey_Delete, false);
    // Type text
    io.AddInputCharactersUTF8(text.c_str());
    // Commit: applies slider/drag temp input; deactivates InputText
    io.AddKeyEvent(ImGuiKey_Enter, true);
    io.AddKeyEvent(ImGuiKey_Enter, false);
}

// ---------------------------------------------------------------------------
// Widget type classification from status flags
// ---------------------------------------------------------------------------
inline std::string classifyWidget(ImGuiItemStatusFlags flags) {
    if (flags & ImGuiItemStatusFlags_Checkable) return "checkbox";
    if (flags & ImGuiItemStatusFlags_Inputable)  return "input";
    if (flags & ImGuiItemStatusFlags_Openable)   return "tree";
    return "button";
}

// ---------------------------------------------------------------------------
// Values -> JSON
// ---------------------------------------------------------------------------
namespace detail {

// The shortest decimal that reads back as the same float (0.1f -> 0.1, not
// 0.10000000149011612), as a double for the JSON writer. Full precision: the
// float is recovered exactly from it.
inline double shortestFloat(float f) {
    if (!std::isfinite(f)) return (double)f;
    char buf[32];
    for (int prec = 6; prec <= 9; ++prec) {
        std::snprintf(buf, sizeof(buf), "%.*g", prec, (double)f);
        if (std::strtof(buf, nullptr) == f) break;
    }
    return std::strtod(buf, nullptr);
}

inline const char* dataTypeName(ImGuiDataType t) {
    switch (t) {
    case ImGuiDataType_S8:     return "int8";
    case ImGuiDataType_U8:     return "uint8";
    case ImGuiDataType_S16:    return "int16";
    case ImGuiDataType_U16:    return "uint16";
    case ImGuiDataType_S32:    return "int";
    case ImGuiDataType_U32:    return "uint";
    case ImGuiDataType_S64:    return "int64";
    case ImGuiDataType_U64:    return "uint64";
    case ImGuiDataType_Float:  return "float";
    case ImGuiDataType_Double: return "double";
    case ImGuiDataType_Bool:   return "bool";
    default:                   return "unknown";
    }
}

inline nlohmann::json scalarToJson(ImGuiDataType t, const unsigned char* p) {
    auto read = [p](auto v) { std::memcpy(&v, p, sizeof(v)); return v; };
    switch (t) {
    case ImGuiDataType_S8:     return read(int8_t{});
    case ImGuiDataType_U8:     return read(uint8_t{});
    case ImGuiDataType_S16:    return read(int16_t{});
    case ImGuiDataType_U16:    return read(uint16_t{});
    case ImGuiDataType_S32:    return read(int32_t{});
    case ImGuiDataType_U32:    return read(uint32_t{});
    case ImGuiDataType_S64:    return read(int64_t{});
    case ImGuiDataType_U64:    return read(uint64_t{});
    case ImGuiDataType_Float:  return shortestFloat(read(float{}));
    case ImGuiDataType_Double: return read(double{});
    case ImGuiDataType_Bool:   return read(bool{});
    default:                   return nullptr;
    }
}

inline nlohmann::json componentsToJson(const WidgetValue& v) {
    const size_t size = ImGui::DataTypeGetInfo(v.dataType)->Size;
    if (v.components == 1) return scalarToJson(v.dataType, v.bytes.data());
    nlohmann::json arr = nlohmann::json::array();
    for (int i = 0; i < v.components; i++) arr.push_back(scalarToJson(v.dataType, v.bytes.data() + size * i));
    return arr;
}

// Adds widget / valueType / value (and kind-specific extras) to an entry.
// Only what a value hook reported: an item without one (a button, an action
// menu item) gets none of these fields.
inline void addValueFields(nlohmann::json& e, const WidgetValue& v) {
    switch (v.kind) {
    case ImGuiTcValueKind_Drag:
    case ImGuiTcValueKind_Slider:
    case ImGuiTcValueKind_Input:
        e["widget"] = v.kind == ImGuiTcValueKind_Drag ? "drag" : v.kind == ImGuiTcValueKind_Slider ? "slider" : "input";
        e["valueType"] = dataTypeName(v.dataType);
        e["value"] = componentsToJson(v);
        return;
    case ImGuiTcValueKind_SliderAngle:
        e["widget"] = "slider_angle";   // displayed in degrees; the variable is radians
        e["valueType"] = "float";
        e["value"] = componentsToJson(v);
        e["unit"] = "rad";
        return;
    case ImGuiTcValueKind_Color:
        e["widget"] = "color";
        e["valueType"] = "color";
        e["value"] = componentsToJson(v);   // the variable as is: [r,g,b] or [r,g,b,a], 0-1
        e["colorSpace"] = v.hsv ? "hsv" : "rgb";
        return;
    case ImGuiTcValueKind_Combo:
        e["widget"] = "combo";
        e["valueType"] = "int";
        e["value"] = componentsToJson(v);   // index of the selected item
        if (v.hasText) e["item"] = v.text;
        return;
    case ImGuiTcValueKind_ComboPreview:
        e["widget"] = "combo";              // custom BeginCombo: only the item shown is known
        if (v.hasText) e["item"] = v.text;
        return;
    case ImGuiTcValueKind_Text:
        e["widget"] = "text";
        e["valueType"] = "string";
        if (v.password) e["password"] = true;   // never reported
        else            e["value"] = v.text;
        if (v.truncated) e["truncated"] = true;
        return;
    case ImGuiTcValueKind_Bool:
        e["widget"] = "checkbox";           // Checkbox, MenuItem(bool*), Selectable(bool*)
        e["valueType"] = "bool";
        e["value"] = componentsToJson(v);
        return;
    case ImGuiTcValueKind_Radio:
        e["widget"] = "radio";
        e["valueType"] = "int";
        e["value"] = componentsToJson(v);   // the variable the button group sets
        return;
    case ImGuiTcValueKind_ListBox:
        e["widget"] = "listbox";
        e["valueType"] = "int";
        e["value"] = componentsToJson(v);   // index of the selected item
        return;
    case ImGuiTcValueKind_ListBoxBegin:
        e["widget"] = "listbox";            // custom BeginListBox: no value known
        return;
    default:
        return;
    }
}

inline nlohmann::json rectToJson(const ImRect& r) {
    return {{"x", (int)r.Min.x}, {"y", (int)r.Min.y},
            {"w", (int)(r.Max.x - r.Min.x)}, {"h", (int)(r.Max.y - r.Min.y)}};
}

// Whether a touched widget was drawn in the last completed frame
inline bool drawnLastFrame(const TouchedWidget& t) {
    if (!t.ctx) return false;
    auto& contexts = detail::contexts();
    auto cit = contexts.find(t.ctx);
    if (cit == contexts.end()) return false;
    auto it = cit->second.lastIdMap.find(t.id);
    return it != cit->second.lastIdMap.end() && !cit->second.lastFrame[it->second].label.empty();
}

inline nlohmann::json touchedWidgetsJson() {
    nlohmann::json arr = nlohmann::json::array();
    for (auto& t : detail::touched()) {
        nlohmann::json e = {{"label", t.label}, {"window", t.windowName}};
        int wid = t.ctx ? windowIdFor(t.ctx) : -1;
        e["windowId"] = wid >= 0 ? nlohmann::json(wid) : nlohmann::json(nullptr);
        addValueFields(e, t.value);
        e["visible"] = drawnLastFrame(t);
        arr.push_back(std::move(e));
    }
    return arr;
}

// ---------------------------------------------------------------------------
// Setting values through the value hook (tcx_imgui_input on a value widget)
// ---------------------------------------------------------------------------

// A composite widget (DragFloat3, SliderInt2, InputFloat4, ColorEdit4,
// ColorPicker4, ...) has no item of its own: its entry is the group of its
// parts, so a click at its centre lands on one part, or in a gap.
inline bool isComposite(const WidgetValue& v) {
    if (v.kind == ImGuiTcValueKind_Color) return true;
    return (v.kind == ImGuiTcValueKind_Drag || v.kind == ImGuiTcValueKind_Slider ||
            v.kind == ImGuiTcValueKind_Input) && v.components > 1;
}

inline const char* kindName(int kind) {
    switch (kind) {
    case ImGuiTcValueKind_Drag:        return "drag";
    case ImGuiTcValueKind_Slider:      return "slider";
    case ImGuiTcValueKind_SliderAngle: return "slider_angle";
    case ImGuiTcValueKind_Input:       return "input";
    case ImGuiTcValueKind_Color:       return "color";
    case ImGuiTcValueKind_Combo:       return "combo";
    case ImGuiTcValueKind_Bool:        return "checkbox";
    case ImGuiTcValueKind_Radio:       return "radio";
    case ImGuiTcValueKind_ListBox:     return "listbox";
    default:                           return "widget";
    }
}

// What tcx_imgui_input takes for this widget, in the units the tools report
inline std::string expectedValue(const WidgetValue& v) {
    const std::string n = std::to_string(v.components);
    switch (v.kind) {
    case ImGuiTcValueKind_Bool:        return "true or false";
    case ImGuiTcValueKind_Combo:
    case ImGuiTcValueKind_ListBox:     return "the index of an item (an integer)";
    case ImGuiTcValueKind_Radio:       return "the integer the button group sets";
    case ImGuiTcValueKind_SliderAngle: return "a number, in radians";
    case ImGuiTcValueKind_Color:
        return "an array of " + n + " numbers, 0-1 (" + (v.hsv ? "[h, s, v" : "[r, g, b") +
               (v.components == 4 ? ", a])" : "])");
    default:
        if (v.components == 1) return std::string("a number (") + dataTypeName(v.dataType) + ")";
        return "an array of " + n + " numbers (" + dataTypeName(v.dataType) + ")";
    }
}

template <class T>
inline bool jsonToInt(const nlohmann::json& v, unsigned char* out, std::string& err) {
    using L = std::numeric_limits<T>;
    T x;
    if (v.is_number_unsigned()) {
        uint64_t u = v.get<uint64_t>();
        if (u > (uint64_t)L::max()) { err = "out of range"; return false; }
        x = (T)u;
    } else if (v.is_number_integer()) {
        int64_t i = v.get<int64_t>();
        if (std::is_signed<T>::value ? (i < (int64_t)L::min() || i > (int64_t)L::max()) : i < 0) {
            err = "out of range";
            return false;
        }
        x = (T)i;
    } else {
        double d = v.get<double>();
        if (d != std::floor(d)) { err = "not an integer"; return false; }
        // max() + 1 is a power of two, exact as a double (max() itself may round up)
        if (!(d >= (double)L::min() && d < (double)L::max() + 1.0)) { err = "out of range"; return false; }
        x = (T)d;
    }
    std::memcpy(out, &x, sizeof(x));
    return true;
}

// One component of type t from v, into out. False, with err, when v is not a
// value of that type.
inline bool jsonToScalar(const nlohmann::json& v, ImGuiDataType t, unsigned char* out, std::string& err) {
    if (t == ImGuiDataType_Bool) {
        if (!v.is_boolean()) { err = "not true or false"; return false; }
        bool b = v.get<bool>();
        std::memcpy(out, &b, sizeof(b));
        return true;
    }
    if (!v.is_number()) { err = "not a number"; return false; }
    switch (t) {
    case ImGuiDataType_Float: {
        double d = v.get<double>();
        if (std::fabs(d) > (double)std::numeric_limits<float>::max()) { err = "out of range"; return false; }
        float f = (float)d;
        std::memcpy(out, &f, sizeof(f));
        return true;
    }
    case ImGuiDataType_Double: { double d = v.get<double>(); std::memcpy(out, &d, sizeof(d)); return true; }
    case ImGuiDataType_S8:     return jsonToInt<int8_t>(v, out, err);
    case ImGuiDataType_U8:     return jsonToInt<uint8_t>(v, out, err);
    case ImGuiDataType_S16:    return jsonToInt<int16_t>(v, out, err);
    case ImGuiDataType_U16:    return jsonToInt<uint16_t>(v, out, err);
    case ImGuiDataType_S32:    return jsonToInt<int32_t>(v, out, err);
    case ImGuiDataType_U32:    return jsonToInt<uint32_t>(v, out, err);
    case ImGuiDataType_S64:    return jsonToInt<int64_t>(v, out, err);
    case ImGuiDataType_U64:    return jsonToInt<uint64_t>(v, out, err);
    default:                   err = "unsupported type"; return false;
    }
}

// The bytes of the widget's variable holding `v`: a single value for one
// component, an array of exactly `components` values otherwise.
inline bool valueToBytes(const WidgetValue& w, const nlohmann::json& v,
                         std::vector<unsigned char>& out, std::string& err) {
    const size_t size = ImGui::DataTypeGetInfo(w.dataType)->Size;
    out.assign(size * (size_t)w.components, 0);
    if (w.components == 1) {
        if (!jsonToScalar(v, w.dataType, out.data(), err)) { err = v.dump() + ": " + err; return false; }
        return true;
    }
    if (!v.is_array() || v.size() != (size_t)w.components) {
        err = v.is_array() ? std::to_string(v.size()) + " values, not " + std::to_string(w.components)
                           : v.dump() + " is not an array";
        return false;
    }
    for (int i = 0; i < w.components; i++) {
        if (!jsonToScalar(v[(size_t)i], w.dataType, out.data() + size * (size_t)i, err)) {
            err = "element " + std::to_string(i) + " (" + v[(size_t)i].dump() + "): " + err;
            return false;
        }
    }
    return true;
}

// A queued value is dropped when it could not be written this long after the
// call, so it never lands after the tool has answered. Shorter than tc::mcp's
// timeout for a deferred reply that is never produced (kTargetedDeferralTimeout).
inline constexpr std::chrono::seconds kPendingValueLifetime{4};

// The reply of tcx_imgui_input for a value written through the value hook,
// once its outcome is known: after the frame after the call (not drawn, a
// same-frame change, a refusal), or after the frame after the write, which
// checks that the variable still holds the value.
inline nlohmann::json injectionResult(ImGuiContext* ctx, const std::shared_ptr<PendingValue>& p,
                                      const std::string& label, const std::string& window) {
    using State = PendingValue::State;
    nlohmann::json r = {{"label", label}, {"window", window}};
    int wid = windowIdFor(ctx);
    r["windowId"] = wid >= 0 ? nlohmann::json(wid) : nlohmann::json(nullptr);
    auto error = [&](const std::string& message) {
        r["status"] = "error";
        r["message"] = message;
        return r;
    };
    switch (p->state) {
    case State::Applied:
        r["status"] = "ok";
        addValueFields(r, p->readBack);   // read back from the variable
        return r;
    case State::Changed:
        addValueFields(r, p->readBack);
        return error("The value was written, but '" + label + "' changed it again in the same frame "
                     "(a hand edit at the same time?). value = what the variable holds now");
    case State::Reverted:
        addValueFields(r, p->readBack);
        return error("The value was written and '" + label + "' returned true, but in the next frame "
                     "its variable held another value again (value = that value). The app ignores the "
                     "widget's return value and copies its own value into the variable every frame, so "
                     "this widget cannot be set from MCP; change the value in the app instead");
    case State::Disabled:
        return error("'" + label + "' is disabled (inside BeginDisabled()). Nothing was written");
    case State::ReadOnly:
        return error("'" + label + "' is read-only. Nothing was written");
    case State::ShapeChanged:
        return error("'" + label + "' no longer takes this kind of value (it changed since it was listed). "
                     "Read tcx_imgui_get_widgets again. Nothing was written");
    case State::Superseded:
        return error("Superseded by a later tcx_imgui_input on '" + label + "'. Nothing was written");
    default:
        return error("'" + label + "' was not drawn in the frame after the call (collapsed header, "
                     "closed or hidden window?). Nothing was written");
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// MCP tool registration (call from registerControlTools)
// ---------------------------------------------------------------------------
inline void registerImGuiTools() {
    using json = nlohmann::json;

    // Idempotent: safe to call from imguiSetup() and/or the app. Keyed on the
    // tools actually being registered, not a one-shot flag: after a hot reload
    // the host removes what the old guest registered (#227), and the new guest
    // must be able to register them again.
    if (tc::mcp::hasTool("tcx_imgui_get_widgets")) return;

    // Activate collection
    enableCollection();

    // tcx_imgui_get_widgets — list all widgets
    tc::mcp::tool("tcx_imgui_get_widgets", "List the ImGui widgets drawn in the last frame of every window running imgui: label, window (ImGui panel), windowId (OS window as tc_list_windows numbers it), type, rect, and for value widgets the current value (widget, valueType, value — floats at full precision; colors as the variable holds them, 0-1, with colorSpace; SliderAngle in radians). touched = changed by hand since startup / tcx_imgui_reset_touched")
        .arg<std::string>("window", "Filter by ImGui window (panel) name (optional, omit for all)", false)
        .arg<int>("windowId", "Filter by OS window id from tc_list_windows (optional)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string window = args.value("window", "");
            int windowId = (args.contains("windowId") && args.at("windowId").is_number())
                         ? args.at("windowId").get<int>() : -1;
            json widgets = json::array();

            for (auto& [wid, ctx] : orderedContexts()) {
                if (windowId >= 0 && wid != windowId) continue;
                for (auto& w : detail::contexts()[ctx].lastFrame) {
                    // Skip widgets with empty labels
                    if (w.label.empty()) continue;

                    // Filter by window if specified
                    if (!window.empty() && w.windowName != window) continue;

                    json entry = {
                        {"label", w.label},
                        {"window", w.windowName},
                        {"windowId", wid},
                        {"type", classifyWidget(w.statusFlags)},
                        {"rect", detail::rectToJson(w.rect)}
                    };

                    // Add checked state if checkbox
                    if (w.statusFlags & ImGuiItemStatusFlags_Checkable) {
                        entry["checked"] = (bool)(w.statusFlags & ImGuiItemStatusFlags_Checked);
                    }
                    // Add opened state if tree
                    if (w.statusFlags & ImGuiItemStatusFlags_Openable) {
                        entry["opened"] = (bool)(w.statusFlags & ImGuiItemStatusFlags_Opened);
                    }

                    detail::addValueFields(entry, w.value);
                    entry["touched"] = detail::findTouched(ctx, w.id) != nullptr;

                    widgets.push_back(entry);
                }
            }

            return json{{"widgets", widgets}, {"count", (int)widgets.size()}};
        }));

    // tcx_imgui_get_touched — everything changed by hand
    tc::mcp::tool("tcx_imgui_get_touched", "Values the user changed by hand (dragging, typing, clicking a widget) since startup or the last tcx_imgui_reset_touched, with their current value. widgets: the ImGui value widgets changed (sliders, drags, inputs, colors, combos, text fields, Checkbox, MenuItem/Selectable with a bool*, RadioButton with an int*, ListBox — the list box under its own label with the index), same fields as tcx_imgui_get_widgets; one that is not drawn right now (collapsed, closed) keeps its last known value with visible=false. Items that change no variable are not recorded: buttons, menu headers, action menu items, MenuItem(label, shortcut, bool selected) even when used as a toggle, plain Selectables, RadioButton(label, bool). Other keys come from addons that keep their own record (inspector: tcxNodeInspector edits per node — node type/name/id, mod, member path, value in tc_get_node_tree encoding). Values set from code, by tc_set_node_members or by tcx_imgui_input on a value widget are not recorded (text typed by tcx_imgui_input and clicks by tcx_imgui_click are). Read-only")
        .bind(std::function<json()>([]() -> json {
            json result = {{"status", "ok"}};
            json widgets = detail::touchedWidgetsJson();
            size_t count = widgets.size();
            result["widgets"] = std::move(widgets);
            for (auto& s : touchedSources()) {
                json items = s.get ? s.get() : json::array();
                count += items.is_array() ? items.size() : 0;
                result[s.key] = std::move(items);
            }
            result["count"] = count;
            return result;
        }));

    // tcx_imgui_reset_touched — forget the record (does not change any value)
    tc::mcp::tool("tcx_imgui_reset_touched", "Clear the record read by tcx_imgui_get_touched (ImGui widgets and addon records such as the inspector's). Values are not changed. Typical use: after copying the values into code")
        .bind(std::function<json()>([]() -> json {
            size_t cleared = detail::touched().size();
            resetTouched();
            for (auto& s : touchedSources()) {
                if (s.reset) s.reset();
            }
            return json{{"status", "ok"}, {"clearedWidgets", cleared}};
        }));

    // tcx_imgui_click — click a widget by label
    tc::mcp::tool("tcx_imgui_click", "Click an ImGui widget by label. A composite widget (DragFloat3, SliderInt2, InputFloat4, ColorEdit4, ColorPicker4, ...) returns an error: a click would hit one of its parts; set its value with tcx_imgui_input")
        .arg<std::string>("label", "Widget label text")
        .arg<std::string>("window", "ImGui window (panel) name (optional, required if label is ambiguous)", false)
        .arg<int>("windowId", "OS window id from tc_list_windows (optional, when the same panel exists in several OS windows)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string window = args.value("window", "");
            int windowId = (args.contains("windowId") && args.at("windowId").is_number())
                         ? args.at("windowId").get<int>() : -1;
            std::string error;

            WidgetRef ref = findWidget(label, window, windowId, error);
            if (!ref.widget) {
                return json{{"status", "error"}, {"message", error}};
            }
            const WidgetValue& v = ref.widget->value;
            if (detail::isComposite(v)) {
                return json{{"status", "error"},
                            {"message", "'" + label + "' is a composite widget (" + detail::kindName(v.kind) + ", " +
                                        std::to_string(v.components) + " components): a click at its centre "
                                        "would hit one of its parts, or a gap. Set its value with tcx_imgui_input: " +
                                        detail::expectedValue(v)}};
            }

            withContext(ref.ctx, [&] { clickWidget(*ref.widget); });
            return json{
                {"status", "ok"},
                {"label", ref.widget->label},
                {"window", ref.widget->windowName},
                {"windowId", windowIdFor(ref.ctx)}
            };
        }));

    // tcx_imgui_input — set the value of a widget: value widgets through the
    // value hook, text fields by typing
    tc::mcp::tool("tcx_imgui_input", "Set the value of an ImGui widget. Value widgets (slider, drag, number input, SliderAngle, color, checkbox, combo, radio, list box, MenuItem/Selectable with a bool*; composites such as DragFloat3 and ColorEdit4 included): text is the value as JSON, in the units tcx_imgui_get_widgets reports — a number; an array for a composite ([x, y, z]; colors [r, g, b(, a)] as floats 0-1, raw HSV with colorSpace hsv); true/false for a bool; the item index for Combo/ListBox; the variable's integer for RadioButton; radians for SliderAngle. It is written into the app's variable on the widget's next frame, and the widget returns true in that frame (so if (ImGui::DragFloat(\"x\", &x)) node->setX(x); and recompute-on-change code run once; the Edited flag is not set). status ok (with the value read back) means the variable held it at the widget's return and still held it in the next frame. Errors (nothing written): wrong shape or type (component count, not a number, out of the type's range), the widget not drawn in the frame after the call (collapsed, closed or hidden), disabled (BeginDisabled) or read-only, no variable (an action MenuItem). Errors after the write: a hand edit in the same frame; app code that ignores the return value and copies its own value in every frame (the value is gone in the next frame: such a widget cannot be set from MCP). No clamping to the widget's min/max. Not recorded in tcx_imgui_get_touched. Text fields (InputText): text replaces the text, typed as keystrokes")
        .arg<std::string>("label", "Widget label")
        .arg<std::string>("text", "Value widgets: the value as JSON (5, 0.25, [1, 2, 3], true, an index). Text fields: the replacement text")
        .arg<std::string>("window", "ImGui window (panel) name (optional)", false)
        .arg<int>("windowId", "OS window id from tc_list_windows (optional)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string window = args.value("window", "");
            int windowId = (args.contains("windowId") && args.at("windowId").is_number())
                         ? args.at("windowId").get<int>() : -1;
            std::string error;

            WidgetRef ref = findWidget(label, window, windowId, error);
            if (!ref.widget) {
                return json{{"status", "error"}, {"message", error}};
            }
            const WidgetInfo& w = *ref.widget;

            // A value widget: queue the value for the value hook, which writes
            // it through the widget's variable on its next frame. The reply
            // waits for that frame.
            if (detail::isWritableKind(w.value.kind)) {
                const json& arg = args.at("text");
                json value = arg.is_string() ? json::parse(arg.get<std::string>(), nullptr, false) : arg;
                std::vector<unsigned char> bytes;
                if (value.is_discarded() || !detail::valueToBytes(w.value, value, bytes, error)) {
                    return json{{"status", "error"},
                                {"message", "'" + label + "' (" + detail::kindName(w.value.kind) + ") takes " +
                                            detail::expectedValue(w.value) + " as JSON; got " +
                                            (value.is_discarded() ? arg.dump() : error) + ". Nothing was written"}};
                }
                // The reply is produced when the hooks know the outcome (the
                // end of the frame after the call, or of the one after the
                // write). The pending value itself is the deferral's target:
                // no window drains it; its onDone does.
                ImGuiContext* ctx = ref.ctx;
                auto pending = detail::queueValue(ctx, w.id, w.value.kind, w.value.dataType, w.value.components,
                                                  std::move(bytes), detail::kPendingValueLifetime, nullptr);
                const void* token = pending.get();
                pending->onDone = [token]() { tc::mcp::drainDeferredResponses(token); };
                std::string panel = w.windowName;
                tc::mcp::deferToolResultUntilAfterFrame([ctx, pending, label, panel]() -> json {
                    return detail::injectionResult(ctx, pending, label, panel);
                }, token);
                return json::object();   // the deferred result replaces this
            }
            // A check box, radio button or menu item without a variable (an
            // action MenuItem, MenuItem(label, shortcut, bool selected)):
            // nothing to write, and typing into it would do nothing.
            if (w.value.kind == 0 && (w.statusFlags & ImGuiItemStatusFlags_Checkable)) {
                return json{{"status", "error"},
                            {"message", "'" + label + "' reports no variable (a MenuItem without a bool*, or a "
                                        "check box that was not drawn): nothing can be written. Use tcx_imgui_click"}};
            }
            // A custom BeginCombo / BeginListBox reports no variable to write.
            if (w.value.kind == ImGuiTcValueKind_ComboPreview || w.value.kind == ImGuiTcValueKind_ListBoxBegin) {
                return json{{"status", "error"},
                            {"message", "'" + label + "' is a custom " +
                                        (w.value.kind == ImGuiTcValueKind_ListBoxBegin ? "list box" : "combo") +
                                        " (BeginCombo / BeginListBox): it has no variable to set. "
                                        "Pick an item by clicking it with tcx_imgui_click (open a combo first)"}};
            }

            std::string text = args.at("text").is_string() ? args.at("text").get<std::string>()
                                                            : args.at("text").dump();
            withContext(ref.ctx, [&] { inputText(*ref.widget, text); });
            return json{
                {"status", "ok"},
                {"label", ref.widget->label},
                {"text", text},
                {"window", ref.widget->windowName},
                {"windowId", windowIdFor(ref.ctx)}
            };
        }));

    // tcx_imgui_checkbox — toggle or set a checkbox
    tc::mcp::tool("tcx_imgui_checkbox", "Toggle an ImGui checkbox")
        .arg<std::string>("label", "Checkbox label")
        .arg<bool>("value", "Desired state (true/false)", false)
        .arg<std::string>("window", "ImGui window (panel) name (optional)", false)
        .arg<int>("windowId", "OS window id from tc_list_windows (optional)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string window = args.value("window", "");
            int windowId = (args.contains("windowId") && args.at("windowId").is_number())
                         ? args.at("windowId").get<int>() : -1;
            std::string error;

            WidgetRef ref = findWidget(label, window, windowId, error);
            if (!ref.widget) {
                return json{{"status", "error"}, {"message", error}};
            }
            const WidgetInfo* w = ref.widget;

            // If value is specified, only click if current state differs
            if (args.contains("value") && (w->statusFlags & ImGuiItemStatusFlags_Checkable)) {
                bool desired = args.at("value").get<bool>();
                bool current = (w->statusFlags & ImGuiItemStatusFlags_Checked) != 0;
                if (desired == current) {
                    return json{
                        {"status", "ok"},
                        {"label", w->label},
                        {"checked", current},
                        {"action", "no_change"}
                    };
                }
            }

            withContext(ref.ctx, [&] { clickWidget(*w); });

            bool wasChecked = (w->statusFlags & ImGuiItemStatusFlags_Checked) != 0;
            return json{
                {"status", "ok"},
                {"label", w->label},
                {"checked", !wasChecked},
                {"window", w->windowName},
                {"windowId", windowIdFor(ref.ctx)}
            };
        }));

    tc::logNotice() << "[MCP] ImGui tools registered (tcx_imgui_get_widgets, tcx_imgui_get_touched, tcx_imgui_reset_touched, tcx_imgui_click, tcx_imgui_input, tcx_imgui_checkbox)";
}

} // namespace tcx::imgui

// -----------------------------------------------------------------------------
// Backward compatibility: tcxImGui's integration helpers historically lived in
// `trussc::imgui_tools`. Canonical is now `tcx::imgui`. DEPRECATED — removed in v1.0.0.
// (No [[deprecated]]: under `using namespace tc;` it would warn on idiomatic use.)
// -----------------------------------------------------------------------------
namespace trussc { namespace imgui_tools { // deprecated: remove at v1.0.0
using tcx::imgui::registerImGuiTools;   // one per public symbol
} }  // namespace trussc::imgui_tools
