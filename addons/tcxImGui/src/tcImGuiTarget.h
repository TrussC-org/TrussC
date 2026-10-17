#pragma once
#include <TrussC.h>
#include "sokol_imgui.h"

namespace tcx::imgui::detail {
// Implemented beside the backend, so all callers (including reload guests)
// share the reservation belonging to the actual sokol_imgui context.
bool beginTargetFrame(simgui_tc_context context, const simgui_frame_desc_t& desc);
bool validateTargetFrame(simgui_tc_context context);
void renderTargetFrame(simgui_tc_context context);
void destroyTargetContext(simgui_tc_context context);
}
