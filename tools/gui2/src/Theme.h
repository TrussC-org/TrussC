#pragma once
#include <cstdlib>
#include <string>

namespace gui2 {
struct Theme {
    ImVec4 window, card, cardBorder, inset, insetHovered, console, consoleText;
    ImVec4 text, textDim, textOnAccent;
    ImVec4 accent, accentHovered, accentActive, accentSoft, selection;
    ImVec4 button, buttonHovered, buttonActive, buttonBorder;
    ImVec4 success, successSoft, failure, failureStrong, failureSoft, warning, warningSoft, progress;
    float windowRounding, childRounding, frameRounding, grabRounding, popupRounding;
    float childBorder, frameBorder, popupBorder;
    ImVec2 windowPadding, cardPadding, framePadding, itemSpacing;
    float sectionGap, headerScale, captionScale, titleScale;
    bool light, cardShadow;
};
inline const Theme soda = {
    {0.11f, 0.12f, 0.14f, 1}, // window
    {0.15f, 0.16f, 0.20f, 1}, // card
    {0.23f, 0.25f, 0.30f, 1}, // cardBorder
    {0.12f, 0.13f, 0.16f, 1}, // inset
    {0.16f, 0.18f, 0.23f, 1}, // insetHovered
    {0.08f, 0.09f, 0.11f, 1}, // console
    {0.84f, 0.86f, 0.89f, 1}, // consoleText
    {0.93f, 0.93f, 0.95f, 1}, // text
    {0.60f, 0.64f, 0.70f, 1}, // textDim
    {0.05f, 0.09f, 0.14f, 1}, // textOnAccent
    {0.31f, 0.70f, 1.00f, 1}, // accent
    {0.42f, 0.76f, 1.00f, 1}, // accentHovered
    {0.18f, 0.61f, 0.91f, 1}, // accentActive
    {0.31f, 0.70f, 1.00f, 0.18f}, // accentSoft
    {0.31f, 0.70f, 1.00f, 0.35f}, // selection
    {0.20f, 0.22f, 0.28f, 1}, // button
    {0.25f, 0.27f, 0.33f, 1}, // buttonHovered
    {0.16f, 0.18f, 0.23f, 1}, // buttonActive
    {0.29f, 0.31f, 0.39f, 1}, // buttonBorder
    {0.24f, 0.84f, 0.55f, 1}, // success
    {0.24f, 0.84f, 0.55f, 0.16f}, // successSoft
    {1.00f, 0.42f, 0.40f, 1}, // failure
    {0.79f, 0.23f, 0.23f, 1}, // failureStrong
    {0.24f, 0.16f, 0.18f, 1}, // failureSoft
    {1.00f, 0.78f, 0.29f, 1}, // warning
    {1.00f, 0.78f, 0.29f, 0.16f}, // warningSoft
    {0.31f, 0.70f, 1.00f, 1}, // progress
    0, 10, 6, 6, 8, 1, 1, 1, {16,14}, {14,12}, {12,7}, {10,8}, 12, .88f, .88f, 1.3f, false, false
};
inline const Theme candy = {
    {0.10f, 0.09f, 0.13f, 1}, // window
    {0.15f, 0.14f, 0.18f, 1}, // card
    {0.29f, 0.27f, 0.39f, 1}, // cardBorder
    {0.12f, 0.10f, 0.16f, 1}, // inset
    {0.18f, 0.15f, 0.22f, 1}, // insetHovered
    {0.07f, 0.05f, 0.09f, 1}, // console
    {0.85f, 0.84f, 0.91f, 1}, // consoleText
    {0.96f, 0.95f, 0.98f, 1}, // text
    {0.67f, 0.64f, 0.76f, 1}, // textDim
    {0.09f, 0.06f, 0.17f, 1}, // textOnAccent
    {0.65f, 0.55f, 0.98f, 1}, // accent
    {0.73f, 0.65f, 1.00f, 1}, // accentHovered
    {0.56f, 0.44f, 0.95f, 1}, // accentActive
    {0.65f, 0.55f, 0.98f, 0.20f}, // accentSoft
    {0.65f, 0.55f, 0.98f, 0.35f}, // selection
    {0.22f, 0.19f, 0.28f, 1}, // button
    {0.27f, 0.24f, 0.35f, 1}, // buttonHovered
    {0.18f, 0.15f, 0.22f, 1}, // buttonActive
    {0, 0, 0, 0}, // buttonBorder
    {0.29f, 0.87f, 0.50f, 1}, // success
    {0.29f, 0.87f, 0.50f, 0.16f}, // successSoft
    {1.00f, 0.36f, 0.36f, 1}, // failure
    {0.82f, 0.23f, 0.23f, 1}, // failureStrong
    {0.29f, 0.15f, 0.19f, 1}, // failureSoft
    {1.00f, 0.82f, 0.25f, 1}, // warning
    {1.00f, 0.82f, 0.25f, 0.16f}, // warningSoft
    {0.65f, 0.55f, 0.98f, 1}, // progress
    0, 14, 10, 10, 12, 2, 0, 2, {16,14}, {16,14}, {14,8}, {10,8}, 14, .8f, .88f, 1.3f, false, false
};
inline const Theme paper = {
    {0.96f, 0.94f, 0.90f, 1}, // window
    {1, 1, 1, 1}, // card
    {0.12f, 0.12f, 0.16f, 1}, // cardBorder
    {0.98f, 0.97f, 0.95f, 1}, // inset
    {0.96f, 0.95f, 0.93f, 1}, // insetHovered
    {0.12f, 0.12f, 0.16f, 1}, // console
    {0.93f, 0.92f, 0.96f, 1}, // consoleText
    {0.12f, 0.12f, 0.16f, 1}, // text
    {0.42f, 0.41f, 0.47f, 1}, // textDim
    {1, 1, 1, 1}, // textOnAccent
    {0.18f, 0.42f, 1.00f, 1}, // accent
    {0.29f, 0.50f, 1.00f, 1}, // accentHovered
    {0.12f, 0.33f, 0.88f, 1}, // accentActive
    {0.18f, 0.42f, 1.00f, 0.14f}, // accentSoft
    {0.18f, 0.42f, 1.00f, 0.25f}, // selection
    {1, 1, 1, 1}, // button
    {0.91f, 0.94f, 1.00f, 1}, // buttonHovered
    {0.93f, 0.91f, 0.86f, 1}, // buttonActive
    {0.12f, 0.12f, 0.16f, 1}, // buttonBorder
    {0.08f, 0.50f, 0.24f, 1}, // success
    {0.86f, 0.99f, 0.91f, 1}, // successSoft
    {0.79f, 0.16f, 0.16f, 1}, // failure
    {0.85f, 0.21f, 0.21f, 1}, // failureStrong
    {0.99f, 0.89f, 0.89f, 1}, // failureSoft
    {0.54f, 0.38f, 0.00f, 1}, // warning
    {1.00f, 0.88f, 0.40f, 1}, // warningSoft
    {0.18f, 0.42f, 1.00f, 1}, // progress
    0, 12, 8, 8, 10, 2, 2, 2, {16,14}, {14,12}, {12,7}, {10,10}, 16, 1, .88f, 1.3f, true, true
};
inline const Theme& theme() {
    // Temporary environment selection for the theme comparison.
    static const Theme* selected = [] {
        const char* value = std::getenv("TRUSSC_GUI2_THEME");
        if (value && std::string(value) == "soda") return &soda;
        if (value && std::string(value) == "paper") return &paper;
        return &candy;
    }();
    return *selected;
}
inline ImVec4 over(const ImVec4& tint, const ImVec4& surface) {
    return {tint.x * tint.w + surface.x * (1 - tint.w),
        tint.y * tint.w + surface.y * (1 - tint.w),
        tint.z * tint.w + surface.z * (1 - tint.w), 1};
}
inline const ImVec4 dangerText{1, 1, 1, 1};
inline ImVec4 darken(ImVec4 color, float factor) {
    color.x *= factor;
    color.y *= factor;
    color.z *= factor;
    return color;
}
inline ImVec4 alpha(ImVec4 color, float factor) { color.w *= factor; return color; }
inline void applyTheme(ImGuiStyle& s, const Theme& t) {
    s.WindowRounding = t.windowRounding;
    s.ChildRounding = t.childRounding;
    s.FrameRounding = t.frameRounding;
    s.GrabRounding = t.grabRounding;
    s.PopupRounding = t.popupRounding;
    s.ChildBorderSize = t.childBorder;
    s.FrameBorderSize = t.frameBorder;
    s.PopupBorderSize = t.popupBorder;
    s.WindowPadding = t.windowPadding;
    s.FramePadding = t.framePadding;
    s.ItemSpacing = t.itemSpacing;
    s.ScrollbarSize = 10;
    s.ScrollbarRounding = t.grabRounding;
    s.DisabledAlpha = .5f;
    s.SeparatorTextBorderSize = 0;
    auto* c = s.Colors;
    c[ImGuiCol_WindowBg] = t.window;
    c[ImGuiCol_ChildBg] = c[ImGuiCol_ScrollbarBg] = ImVec4(0,0,0,0);
    c[ImGuiCol_Border] = c[ImGuiCol_Separator] = t.cardBorder;
    c[ImGuiCol_FrameBg] = t.inset;
    c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_FrameBgActive] = t.insetHovered;
    c[ImGuiCol_Button] = t.button;
    c[ImGuiCol_ButtonHovered] = t.buttonHovered;
    c[ImGuiCol_ButtonActive] = t.buttonActive;
    c[ImGuiCol_CheckMark] = c[ImGuiCol_SliderGrab] = c[ImGuiCol_SliderGrabActive] =
        c[ImGuiCol_NavCursor] = c[ImGuiCol_PlotLines] = t.accent;
    c[ImGuiCol_Header] = t.accentSoft;
    c[ImGuiCol_HeaderHovered] = alpha(t.accentSoft, .7f);
    c[ImGuiCol_HeaderActive] = alpha(t.accentSoft, 1.3f);
    c[ImGuiCol_TextSelectedBg] = t.selection;
    c[ImGuiCol_PlotHistogram] = t.progress;
    c[ImGuiCol_Text] = t.text;
    c[ImGuiCol_TextDisabled] = t.textDim;
    c[ImGuiCol_PopupBg] = c[ImGuiCol_TitleBg] = c[ImGuiCol_TitleBgActive] =
        c[ImGuiCol_TitleBgCollapsed] = c[ImGuiCol_MenuBarBg] = t.card;
    c[ImGuiCol_ScrollbarGrab] = t.light ? ImVec4(.84f,.82f,.77f,1) : t.cardBorder;
    c[ImGuiCol_ScrollbarGrabHovered] = c[ImGuiCol_ScrollbarGrabActive] = t.textDim;
    c[ImGuiCol_ModalWindowDimBg] = t.light ? ImVec4(.12f,.12f,.16f,.35f) : ImVec4(0,0,0,.55f);
}
}
