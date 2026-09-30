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
    tc::mcp::tool("tcx_imgui_get_touched", "Values the user changed by hand (dragging, typing, clicking a widget) since startup or the last tcx_imgui_reset_touched, with their current value. widgets: the ImGui value widgets changed (sliders, drags, inputs, colors, combos, text fields, Checkbox, MenuItem/Selectable with a bool*, RadioButton with an int*, ListBox — the list box under its own label with the index), same fields as tcx_imgui_get_widgets; one that is not drawn right now (collapsed, closed) keeps its last known value with visible=false. Items that change no variable are not recorded: buttons, menu headers, action menu items, MenuItem(label, shortcut, bool selected) even when used as a toggle, plain Selectables, RadioButton(label, bool). Other keys come from addons that keep their own record (inspector: tcxNodeInspector edits per node — node type/name/id, mod, member path, value in tc_get_node_tree encoding). Values set from code or by tc_set_node_members are not recorded. Read-only")
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
    tc::mcp::tool("tcx_imgui_click", "Click an ImGui widget by label")
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

            withContext(ref.ctx, [&] { clickWidget(*ref.widget); });
            return json{
                {"status", "ok"},
                {"label", ref.widget->label},
                {"window", ref.widget->windowName},
                {"windowId", windowIdFor(ref.ctx)}
            };
        }));

    // tcx_imgui_input — set the value of an input/slider/drag widget
    tc::mcp::tool("tcx_imgui_input", "Set the value of an ImGui widget: text inputs, and numeric entry on slider/drag widgets")
        .arg<std::string>("label", "Widget label")
        .arg<std::string>("text", "Replacement text (or numeric value for slider/drag)")
        .arg<std::string>("window", "ImGui window (panel) name (optional)", false)
        .arg<int>("windowId", "OS window id from tc_list_windows (optional)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string text = args.at("text").get<std::string>();
            std::string window = args.value("window", "");
            int windowId = (args.contains("windowId") && args.at("windowId").is_number())
                         ? args.at("windowId").get<int>() : -1;
            std::string error;

            WidgetRef ref = findWidget(label, window, windowId, error);
            if (!ref.widget) {
                return json{{"status", "error"}, {"message", error}};
            }

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
