// =============================================================================
// imgui_impl.cpp - Dear ImGui + sokol_imgui implementation (Android)
// =============================================================================

#ifdef __ANDROID__

// sokol headers (must come before sokol_imgui.h)
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
#include "sokol/util/sokol_imgui.h"

// Test Engine hook implementations (provides ImGuiTestEngineHook_* functions)
#include "tc/gui/tcImGuiHooks.h"

#endif // __ANDROID__
