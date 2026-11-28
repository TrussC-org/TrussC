#pragma once
#include "Theme.h"
#include "Strings.h"

namespace gui2 {
inline bool beginCard(const char* id, float height = 0, ImGuiChildFlags extra = 0, const ImVec4* bg = nullptr, float border = -1, const ImVec4* borderColor = nullptr) {
    const auto& t = theme();
    if (t.cardShadow) {
        // Child decorations may share the parent draw list; keep shadows behind them.
        ImGui::GetWindowDrawList()->ChannelsSplit(2);
        ImGui::GetWindowDrawList()->ChannelsSetCurrent(1);
    }
    ImGui::PushStyleColor(ImGuiCol_ChildBg, bg ? *bg : t.card);
    ImGui::PushStyleColor(ImGuiCol_Border, borderColor ? *borderColor : t.cardBorder);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, t.childRounding);
    ImGui::PushStyleVar(ImGuiStyleVar_ChildBorderSize, border < 0 ? t.childBorder : border);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, t.cardPadding);
    auto flags = ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | extra;
    if (height == 0) flags |= ImGuiChildFlags_AutoResizeY;
    ImGui::BeginChild(id, ImVec2(0, height), flags);
    return true;
}
inline void endCard(bool trailingGap = true) {
    ImGui::EndChild();
    ImGui::PopStyleVar(3);
    ImGui::PopStyleColor(2);
    if (theme().cardShadow) {
        ImGui::GetWindowDrawList()->ChannelsSetCurrent(0);
        const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
        ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(a.x+4, a.y+4), ImVec2(b.x+4, b.y+4),
            ImGui::GetColorU32(theme().cardBorder), theme().childRounding);
        ImGui::GetWindowDrawList()->ChannelsMerge();
    }
    if (trailingGap) ImGui::Dummy(ImVec2(0, theme().sectionGap - ImGui::GetStyle().ItemSpacing.y));
}
inline void sectionHeader(const char* id) {
    const auto& t = theme();
    ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * t.headerScale);
    if (t.light) {
        const auto p = ImGui::GetCursorScreenPos();
        ImGui::GetWindowDrawList()->AddCircleFilled(ImVec2(p.x+5, p.y+ImGui::GetTextLineHeight()*.5f), 5, ImGui::GetColorU32(t.accent));
        ImGui::SetCursorScreenPos(ImVec2(p.x+16, p.y));
        ImGui::TextUnformatted(text(id));
    } else if (t.headerScale == candy.headerScale) {
        std::string value = text(id);
        for (auto& c : value) if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        const auto p = ImGui::GetCursorScreenPos(), size = ImGui::CalcTextSize(value.c_str());
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x+size.x+16, p.y+size.y+6),
            ImGui::GetColorU32(t.accentSoft), (size.y+6)*.5f);
        ImGui::SetCursorScreenPos(ImVec2(p.x+8, p.y+3));
        ImGui::TextColored(t.accent, "%s", value.c_str());
        ImGui::Dummy(ImVec2(0, 3));
    } else ImGui::TextColored(t.textDim, "%s", text(id));
    ImGui::PopFont();
}
inline void warningBadge(const char* value) {
    const auto& t = theme();
    const auto p = ImGui::GetCursorScreenPos(), size = ImGui::CalcTextSize(value);
    ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x+size.x+16, p.y+size.y+6),
        ImGui::GetColorU32(t.warningSoft), t.frameRounding);
    ImGui::SetCursorScreenPos(ImVec2(p.x+8, p.y+3));
    ImGui::TextColored(t.light ? t.text : t.warning, "%s", value);
    ImGui::Dummy(ImVec2(0, 3));
}
}
