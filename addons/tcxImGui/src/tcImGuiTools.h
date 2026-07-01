#pragma once

// =============================================================================
// tcImGuiTools.h - ImGui MCP Tools
// Auto-expose ImGui widgets as MCP tools (enabled via registerDebuggerTools())
//
// Uses ImGui Test Engine hooks (IMGUI_ENABLE_TEST_ENGINE) to collect
// widget info each frame, then provides MCP tools to query and interact
// with them.
// =============================================================================

#include "tcImGuiHooks.h"
#include "tc/utils/tcMCP.h"
#include "tc/utils/tcLog.h"

namespace tcx::imgui {

// ---------------------------------------------------------------------------
// Widget lookup helpers
// ---------------------------------------------------------------------------

// Find widget by label, optionally filtered by window name
// Returns nullptr if not found or ambiguous (sets outError)
inline const WidgetInfo* findWidget(const std::string& label,
                                     const std::string& window,
                                     std::string& outError) {
    const WidgetInfo* found = nullptr;
    std::vector<std::string> candidateWindows;

    for (auto& w : detail::lastFrame) {
        if (w.label == label) {
            if (!window.empty()) {
                if (w.windowName == window) {
                    found = &w;
                    break;
                }
            } else {
                candidateWindows.push_back(w.windowName);
                if (!found) {
                    found = &w;
                } else {
                    // Ambiguous
                    found = nullptr;
                    outError = "Ambiguous label '" + label + "' found in multiple windows: ";
                    std::unordered_map<std::string, bool> seen;
                    for (auto& cw : candidateWindows) {
                        if (!seen[cw]) {
                            if (seen.size() > 1) outError += ", ";
                            outError += "'" + cw + "'";
                            seen[cw] = true;
                        }
                    }
                    outError += ". Specify 'window' argument.";
                    return nullptr;
                }
            }
        }
    }

    if (!found) {
        if (!window.empty()) {
            outError = "Widget '" + label + "' not found in window '" + window + "'";
        } else {
            outError = "Widget '" + label + "' not found";
        }
    }
    return found;
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
// MCP tool registration (call from registerDebuggerTools)
// ---------------------------------------------------------------------------
inline void registerImGuiTools() {
    using json = nlohmann::json;

    // Idempotent: safe to call from imguiSetup() and/or the app.
    static bool registered = false;
    if (registered) return;
    registered = true;

    // Activate collection
    enableCollection();

    // imgui_get_widgets — list all widgets
    tc::mcp::tool("imgui_get_widgets", "List ImGui widgets with labels, types, and positions")
        .arg<std::string>("window", "Filter by window name (optional, omit for all)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string window = args.value("window", "");
            json widgets = json::array();

            for (auto& w : detail::lastFrame) {
                // Skip widgets with empty labels
                if (w.label.empty()) continue;

                // Filter by window if specified
                if (!window.empty() && w.windowName != window) continue;

                std::string type = classifyWidget(w.statusFlags);

                json entry = {
                    {"label", w.label},
                    {"window", w.windowName},
                    {"type", type},
                    {"rect", {
                        {"x", (int)w.rect.Min.x},
                        {"y", (int)w.rect.Min.y},
                        {"w", (int)(w.rect.Max.x - w.rect.Min.x)},
                        {"h", (int)(w.rect.Max.y - w.rect.Min.y)}
                    }}
                };

                // Add checked state if checkbox
                if (w.statusFlags & ImGuiItemStatusFlags_Checkable) {
                    entry["checked"] = (bool)(w.statusFlags & ImGuiItemStatusFlags_Checked);
                }
                // Add opened state if tree
                if (w.statusFlags & ImGuiItemStatusFlags_Openable) {
                    entry["opened"] = (bool)(w.statusFlags & ImGuiItemStatusFlags_Opened);
                }

                widgets.push_back(entry);
            }

            return json{{"widgets", widgets}, {"count", (int)widgets.size()}};
        }));

    // imgui_click — click a widget by label
    tc::mcp::tool("imgui_click", "Click an ImGui widget by label")
        .arg<std::string>("label", "Widget label text")
        .arg<std::string>("window", "Window name (optional, required if label is ambiguous)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string window = args.value("window", "");
            std::string error;

            auto* w = findWidget(label, window, error);
            if (!w) {
                return json{{"status", "error"}, {"message", error}};
            }

            clickWidget(*w);
            return json{
                {"status", "ok"},
                {"label", w->label},
                {"window", w->windowName}
            };
        }));

    // imgui_input — set the value of an input/slider/drag widget
    tc::mcp::tool("imgui_input", "Set the value of an ImGui widget: text inputs, and numeric entry on slider/drag widgets")
        .arg<std::string>("label", "Widget label")
        .arg<std::string>("text", "Replacement text (or numeric value for slider/drag)")
        .arg<std::string>("window", "Window name (optional)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string text = args.at("text").get<std::string>();
            std::string window = args.value("window", "");
            std::string error;

            auto* w = findWidget(label, window, error);
            if (!w) {
                return json{{"status", "error"}, {"message", error}};
            }

            inputText(*w, text);
            return json{
                {"status", "ok"},
                {"label", w->label},
                {"text", text},
                {"window", w->windowName}
            };
        }));

    // imgui_checkbox — toggle or set a checkbox
    tc::mcp::tool("imgui_checkbox", "Toggle an ImGui checkbox")
        .arg<std::string>("label", "Checkbox label")
        .arg<bool>("value", "Desired state (true/false)", false)
        .arg<std::string>("window", "Window name (optional)", false)
        .bind(std::function<json(const json&)>([](const json& args) -> json {
            std::string label = args.at("label").get<std::string>();
            std::string window = args.value("window", "");
            std::string error;

            auto* w = findWidget(label, window, error);
            if (!w) {
                return json{{"status", "error"}, {"message", error}};
            }

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

            clickWidget(*w);

            bool wasChecked = (w->statusFlags & ImGuiItemStatusFlags_Checked) != 0;
            return json{
                {"status", "ok"},
                {"label", w->label},
                {"checked", !wasChecked},
                {"window", w->windowName}
            };
        }));

    tc::logNotice() << "[MCP] ImGui tools registered (imgui_get_widgets, imgui_click, imgui_input, imgui_checkbox)";
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
