// =============================================================================
// imgui_impl.mm - Dear ImGui + sokol_imgui implementation (iOS)
// =============================================================================

// sokol headers (required before sokol_imgui.h)
#include "sokol/sokol_app.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"

// ImGui core implementation
#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/imgui_demo.cpp"

// sokol_imgui implementation
#define SOKOL_IMGUI_IMPL
#include "sokol/sokol_imgui.h"
