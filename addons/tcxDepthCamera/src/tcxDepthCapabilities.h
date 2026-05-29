#pragma once

// =============================================================================
// tcxDepthCapabilities.h - Optional capability interfaces for depth cameras
// =============================================================================
//
// The common streams (depth, color, IR) live in the canonical DepthFrame on the
// DepthCamera base. Capability interfaces are only for genuinely device-specific
// data that does NOT fit that uniform representation. A device composes the ones
// it supports via multiple inheritance:
//
//   class Gemini : public DepthCamera, public IStereoRaw { ... };
//
// These interfaces do NOT inherit DepthCamera, so there is no diamond - it
// behaves like Java's "implements" of several interfaces. Query at runtime with
// as<>() / is<>() (see tcxDepthCast.h):
//
//   if (auto* s = as<IStereoRaw>(*cam)) { auto& left = s->getLeftInfrared(); }
//
// =============================================================================

#include "tcxDepthTypes.h"

namespace tcx {

using namespace tc;

// -----------------------------------------------------------------------------
// IStereoRaw - device is a stereo pair and can hand back both raw IR views
// (the single registered IR image lives on the frame; this is the L/R pair).
// -----------------------------------------------------------------------------
class IStereoRaw {
public:
    virtual ~IStereoRaw() = default;

    virtual const Pixels& getLeftInfrared()  const = 0;
    virtual const Pixels& getRightInfrared() const = 0;
    virtual float getBaseline() const = 0;  // meters between the two sensors
};

// -----------------------------------------------------------------------------
// IConfidenceMap - per-pixel confidence / amplitude (typical of ToF)
// Useful for masking out flying pixels / low-confidence depth.
// -----------------------------------------------------------------------------
class IConfidenceMap {
public:
    virtual ~IConfidenceMap() = default;

    virtual const Pixels& getConfidencePixels() const = 0;
    virtual float getConfidenceAt(int x, int y) const = 0;  // normalized 0-1
};

} // namespace tcx
