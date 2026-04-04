#pragma once

// =============================================================================
// tcImGuiHooks.h - ImGui Test Engine Hook implementations + Widget Registry
//
// Provides the extern functions required by IMGUI_ENABLE_TEST_ENGINE.
// Separated from tcImGuiTools.h so that imgui_impl.mm/.cpp can include
// this without pulling in MCP / nlohmann dependencies.
// =============================================================================

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

#include <vector>
#include <string>
#include <unordered_map>

namespace trussc {
namespace imgui_tools {

// ---------------------------------------------------------------------------
// Widget info collected from Test Engine hooks
// ---------------------------------------------------------------------------
struct WidgetInfo {
    ImGuiID id = 0;
    std::string label;
    std::string windowName;
    ImRect rect;
    ImGuiItemStatusFlags statusFlags = 0;
};

// ---------------------------------------------------------------------------
// Double-buffered widget registry
// ---------------------------------------------------------------------------
namespace detail {
    // Current frame (written during ImGui rendering)
    inline std::vector<WidgetInfo> currentFrame;
    inline std::unordered_map<ImGuiID, size_t> currentIdMap;

    // Last completed frame (read by MCP tools)
    inline std::vector<WidgetInfo> lastFrame;

    // Whether collection is active
    inline bool collecting = false;
}

// Begin frame: clear current buffer
inline void beginFrame() {
    if (!detail::collecting) return;
    detail::currentFrame.clear();
    detail::currentIdMap.clear();
}

// Swap frames: move current to last
inline void swapFrames() {
    if (!detail::collecting) return;
    detail::lastFrame.swap(detail::currentFrame);
}

// Enable/disable collection
inline void enableCollection() {
    auto* ctx = ImGui::GetCurrentContext();
    if (ctx) {
        ctx->TestEngineHookItems = true;
    }
    detail::collecting = true;
}

inline void disableCollection() {
    auto* ctx = ImGui::GetCurrentContext();
    if (ctx) {
        ctx->TestEngineHookItems = false;
    }
    detail::collecting = false;
}

} // namespace imgui_tools
} // namespace trussc

// =============================================================================
// Test Engine Hook implementations
// These extern functions are called by ImGui internally when
// IMGUI_ENABLE_TEST_ENGINE is defined.
// =============================================================================

inline void ImGuiTestEngineHook_ItemAdd(ImGuiContext* ctx, ImGuiID id, const ImRect& bb, const ImGuiLastItemData* item_data) {
    if (!trussc::imgui_tools::detail::collecting) return;

    std::string windowName;
    if (ctx->CurrentWindow) {
        windowName = ctx->CurrentWindow->Name;
    }

    trussc::imgui_tools::WidgetInfo info;
    info.id = id;
    info.rect = bb;
    info.windowName = std::move(windowName);
    if (item_data) {
        info.statusFlags = item_data->StatusFlags;
    }

    auto& cur = trussc::imgui_tools::detail::currentFrame;
    auto& idMap = trussc::imgui_tools::detail::currentIdMap;

    size_t idx = cur.size();
    cur.push_back(std::move(info));
    idMap[id] = idx;
}

inline void ImGuiTestEngineHook_ItemInfo(ImGuiContext* ctx, ImGuiID id, const char* label, ImGuiItemStatusFlags flags) {
    if (!trussc::imgui_tools::detail::collecting) return;
    (void)ctx;

    auto& idMap = trussc::imgui_tools::detail::currentIdMap;
    auto it = idMap.find(id);
    if (it == idMap.end()) return;

    auto& widget = trussc::imgui_tools::detail::currentFrame[it->second];
    if (label) widget.label = label;
    widget.statusFlags = flags;
}

inline void ImGuiTestEngineHook_Log(ImGuiContext* ctx, const char* fmt, ...) {
    (void)ctx;
    (void)fmt;
}

inline const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext* ctx, ImGuiID id) {
    (void)ctx;
    auto& idMap = trussc::imgui_tools::detail::currentIdMap;
    auto it = idMap.find(id);
    if (it != idMap.end()) {
        auto& w = trussc::imgui_tools::detail::currentFrame[it->second];
        if (!w.label.empty()) return w.label.c_str();
    }
    return nullptr;
}
