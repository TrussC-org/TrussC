// =============================================================================
// imgui_impl.cpp - Dear ImGui + sokol_imgui 実装 (Windows/Linux)
// =============================================================================

// sokol ヘッダー（sokol_imgui.h より先に必要）
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"

// ImGui コア実装
#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/imgui_demo.cpp"

// sokol_imgui 実装
#define SOKOL_IMGUI_IMPL
#include "sokol/sokol_imgui.h"
