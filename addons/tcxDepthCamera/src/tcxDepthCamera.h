#pragma once

// =============================================================================
// tcxDepthCamera.h - Unified depth-camera interface for TrussC (umbrella header)
// =============================================================================
//
// tcxDepthCamera defines a common base for depth / point-cloud sensors (Orbbec,
// Kinect, Xtion, RealSense, LiDAR, ...) so an application can drive any of them
// through one type:
//
//   #include <tcxDepthCamera.h>
//   using namespace tcx;
//
//   shared_ptr<DepthCamera> cam = make_shared<Orbbec>(serial);  // from tcxOrbbec
//   cam->setThreaded(true);
//   cam->setup();
//   ...
//   cam->update();
//   if (cam->isFrameNew()) {
//       const DepthFrame& f = cam->currentFrame();  // the raw thing you want
//       Mesh cloud = cam->toMesh({.colors = true}); // ... and the convenience
//       cloud.draw();
//   }
//
// The DepthCamera base OWNS a canonical, triple-buffered DepthFrame (depth +
// optional color/IR/world + intrinsics) and provides everything built on it -
// distance, world coordinates, meshing, threading. A backend only fills a
// DepthFrame in captureInto(). Device-specific extras (raw stereo pair,
// confidence) are capability interfaces discovered with as<>() / is<>().
//
// This addon ships no device drivers (except the hardware-free
// SyntheticDepthCamera). Concrete cameras live in their own addons (tcxOrbbec,
// tcxAzureKinect, ...) that depend on this one.
//
// Headers:
//   tcxDepthTypes.h           - DepthSensorType, DepthIntrinsics, DepthFrame,
//                               StreamFreshness, DepthMeshOptions
//   tcxDepthImage.h           - colorToImage/depthToImage/irToImage converters
//   tcxDepthCameraBase.h      - DepthCamera (the concrete, thread-safe base)
//   tcxDepthCapabilities.h    - IStereoRaw, IConfidenceMap (device-specific)
//   tcxDepthCast.h            - as<>() / is<>() / isStereoCam() capability queries
//   tcxSyntheticDepthCamera.h - SyntheticDepthCamera (software-generated source)
//
// =============================================================================

#include "tcxDepthTypes.h"
#include "tcxDepthImage.h"
#include "tcxDepthCameraBase.h"
#include "tcxDepthCapabilities.h"
#include "tcxDepthCast.h"
#include "tcxSyntheticDepthCamera.h"
