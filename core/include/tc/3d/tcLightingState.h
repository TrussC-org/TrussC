#pragma once

// =============================================================================
// tcLightingState.h - Lighting state (breadcrumb; per-window)
// =============================================================================
//
// The lighting state (activeLights, currentMaterial, cameraPosition,
// pbrExposure, currentEnvironment) used to be process globals here. It is now
// PER-WINDOW: it lives in internal::WindowContext (tc/app/tcWindowContext.h)
// and is reached through internal::currentWindowContext(). Single-window apps
// are unchanged (the main window is the only context).
//
// The limits internal::maxLights / internal::maxShadowLights also moved to
// tcWindowContext.h (the shadow-slot arrays need maxShadowLights at definition
// time). This file is kept as the documented home of the lighting state and to
// preserve the include-order contract (included before tc3DGraphics.h).
//
// =============================================================================

#include <vector>

namespace trussc {

// Forward declarations
class Light;
class Material;

} // namespace trussc
