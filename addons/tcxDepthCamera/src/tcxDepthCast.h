#pragma once

// =============================================================================
// tcxDepthCast.h - Capability queries for DepthCamera
// =============================================================================
//
// A device advertises device-specific features by inheriting capability
// interfaces (tcxDepthCapabilities.h). The single source of truth for "does
// this camera have feature X" is whether it inherits interface X, so we query
// with a dynamic_cast rather than a hand-maintained bool flag.
//
// as<Cap>() does the check AND returns the usable pointer in one cast:
//
//   if (auto* s = as<IStereoRaw>(*cam)) { auto& left = s->getLeftInfrared(); }
//
// (Common streams - color, IR - are NOT capabilities; query them directly on
// the camera with hasColor() / hasInfrared().)
//
// =============================================================================

#include "tcxDepthCameraBase.h"
#include "tcxDepthCapabilities.h"
#include <memory>

namespace tcx::depthcamera {

using namespace tc;

// -- as<Cap>(cam) -> Cap* (nullptr if the camera lacks the capability) --------
template <class Cap>
Cap* as(DepthCamera& cam) { return dynamic_cast<Cap*>(&cam); }

template <class Cap>
const Cap* as(const DepthCamera& cam) { return dynamic_cast<const Cap*>(&cam); }

template <class Cap>
Cap* as(DepthCamera* cam) { return cam ? dynamic_cast<Cap*>(cam) : nullptr; }

template <class Cap>
Cap* as(const std::shared_ptr<DepthCamera>& cam) {
    return cam ? dynamic_cast<Cap*>(cam.get()) : nullptr;
}

// -- is<Cap>(cam) -> bool -----------------------------------------------------
template <class Cap>
bool is(const DepthCamera& cam) { return dynamic_cast<const Cap*>(&cam) != nullptr; }

template <class Cap>
bool is(const std::shared_ptr<DepthCamera>& cam) {
    return cam && dynamic_cast<const Cap*>(cam.get()) != nullptr;
}

// -- Readable named wrappers (free functions; base class stays untouched) -----
inline bool isStereoCam(const DepthCamera& c)   { return is<IStereoRaw>(c); }
inline bool hasConfidence(const DepthCamera& c) { return is<IConfidenceMap>(c); }

// Sensor kind is informational, not a capability - report it from the enum.
inline bool isToF(const DepthCamera& c) { return c.getSensorType() == DepthSensorType::ToF; }

} // namespace tcx::depthcamera

// Backward compatibility: canonical is now tcx::depthcamera. Flat tcx:: alias
// kept until v1.0.0.
namespace tcx { // deprecated: remove at v1.0.0
    using depthcamera::as;
    using depthcamera::is;
    using depthcamera::isStereoCam;
    using depthcamera::hasConfidence;
    using depthcamera::isToF;
}
