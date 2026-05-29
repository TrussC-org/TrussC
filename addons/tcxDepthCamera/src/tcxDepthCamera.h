#pragma once

// =============================================================================
// tcxDepthCamera.h - Unified depth-camera interface for TrussC (umbrella header)
// =============================================================================
//
// tcxDepthCamera is an INTERFACE-ONLY addon: it defines the common contract for
// depth / point-cloud sensors (Orbbec, Kinect, Xtion, RealSense, LiDAR, ...) so
// that an application can drive any of them through a single base type:
//
//   #include <tcxDepthCamera.h>
//   using namespace tcx;
//
//   shared_ptr<DepthCamera> cam = make_shared<Orbbec>(serial);  // from tcxOrbbec
//   cam->setup();
//   ...
//   cam->update();
//   if (cam->isFrameNew()) {
//       Mesh cloud = cam->toMesh({.colors = true});
//       cloud.draw();
//   }
//
//   // Optional features are discovered at runtime, not baked into the type:
//   if (auto* s = as<IStereoRaw>(*cam)) { auto& left = s->getLeftInfrared(); }
//   if (isStereoCam(*cam)) { ... }
//
// This addon contains no device drivers. Concrete cameras live in their own
// addons (e.g. tcxOrbbec) that depend on this one and inherit DepthCamera plus
// whatever capability interfaces they support.
//
// Headers:
//   tcxDepthTypes.h        - DepthSensorType, DepthIntrinsics, DepthMeshOptions
//   tcxDepthCapabilities.h - IColorStream, IInfraredStream, IStereoRaw, ...
//   tcxDepthCameraBase.h   - DepthCamera (the shared contract)
//   tcxThreadedDepthCamera.h - ThreadedDepthCameraBase<Frame> (thread-safe base)
//   tcxSyntheticDepthCamera.h - SyntheticDepthCamera (software-generated source)
//   tcxDepthCast.h         - as<>() / is<>() / isStereoCam() capability queries
//
// =============================================================================

#include "tcxDepthTypes.h"
#include "tcxDepthCapabilities.h"
#include "tcxDepthCameraBase.h"
#include "tcxThreadedDepthCamera.h"
#include "tcxSyntheticDepthCamera.h"
#include "tcxDepthCast.h"
