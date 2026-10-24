// =============================================================================
// imgui_impl.cpp - Dear ImGui + sokol_imgui implementation (Android)
// =============================================================================

#ifdef __ANDROID__

// sokol headers (must come before sokol_imgui.h)
#include "sokol/sokol_app_tc.h"
#include "sokol/sokol_gfx.h"
#include "sokol/sokol_log.h"

// ImGui core implementation
#include "imgui/imgui.cpp"
#include "imgui/imgui_draw.cpp"
#include "imgui/imgui_tables.cpp"
#include "imgui/imgui_widgets.cpp"
#include "imgui/imgui_demo.cpp"

// Keep ImGui's MSVC formatting aliases from renaming later std:: calls.
#undef snprintf
#undef vsnprintf

// sokol_imgui implementation
#include "tcImGuiTargetImpl.h"

// Test Engine hook implementations (provides ImGuiTestEngineHook_* functions)
#include "tcImGuiHooksImpl.h"

// Public addon API implementation (after the backend).
#include "tcxImGuiImpl.h"

#endif // __ANDROID__
