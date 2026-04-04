#pragma once

// =============================================================================
// tcImGuiTools.h - ImGui MCP Tools
// Auto-expose ImGui widgets as MCP tools (enabled via enableDebugger())
//
// Uses ImGui Test Engine hooks (IMGUI_ENABLE_TEST_ENGINE) to collect
// widget info each frame, then provides MCP tools to query and interact
// with them.
// =============================================================================

#include "tcImGuiHooks.h"
#include "tc/utils/tcMCP.h"
#include "tc/utils/tcLog.h"

namespace trussc {
namespace imgui_tools {

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
    // Click to focus
    io.AddMousePosEvent(center.x, center.y);
    io.AddMouseButtonEvent(0, true);
    io.AddMouseButtonEvent(0, false);
    // Select all (Ctrl+A)
    io.AddKeyEvent(ImGuiMod_Ctrl, true);
    io.AddKeyEvent(ImGuiKey_A, true);
    io.AddKeyEvent(ImGuiKey_A, false);
    io.AddKeyEvent(ImGuiMod_Ctrl, false);
    // Delete selection
    io.AddKeyEvent(ImGuiKey_Delete, true);
    io.AddKeyEvent(ImGuiKey_Delete, false);
    // Type text
    io.AddInputCharactersUTF8(text.c_str());
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

    // Activate collection
    enableCollection();

    // imgui_get_widgets — list all widgets
    mcp::tool("imgui_get_widgets", "List ImGui widgets with labels, types, and positions")
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
    mcp::tool("imgui_click", "Click an ImGui widget by label")
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

    // imgui_input — type text into an input widget
    mcp::tool("imgui_input", "Type text into an ImGui input widget")
        .arg<std::string>("label", "Input widget label")
        .arg<std::string>("text", "Text to type")
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
    mcp::tool("imgui_checkbox", "Toggle an ImGui checkbox")
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

    logNotice() << "[MCP] ImGui tools registered (imgui_get_widgets, imgui_click, imgui_input, imgui_checkbox)";
}

} // namespace imgui_tools
} // namespace trussc
