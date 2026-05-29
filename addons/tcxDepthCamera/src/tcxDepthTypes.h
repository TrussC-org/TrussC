#pragma once

// =============================================================================
// tcxDepthTypes.h - Common value types for depth cameras
// =============================================================================
//
// Plain data structs shared by the DepthCamera base and all implementations.
// The centrepiece is DepthFrame: the canonical, device-independent
// representation of one frame. The whole point of a depth camera is to get at
// this frame; everything else (deprojection, meshing, recording) is built on
// top of it by the base, so implementations only have to FILL a DepthFrame.
//
// =============================================================================

#include <TrussC.h>
#include <cstdint>
#include <vector>

namespace tcx {

using namespace tc;

// What kind of sensor produced the depth. Informational only - never branch on
// this to decide which methods/data exist; use hasColor()/hasInfrared() on the
// frame and capability interfaces + as<>() for that. This is for UI / logging.
enum class DepthSensorType {
    Unknown,
    StructuredLight,  // e.g. Kinect v1, Orbbec Astra, Xtion
    Stereo,           // e.g. Orbbec Gemini, RealSense D4xx
    ToF,              // e.g. Azure Kinect, Orbbec Femto
    LiDAR,            // scanning / solid-state LiDAR
};

// Pinhole intrinsics of the depth stream, in pixel units. World coordinates of
// a depth sample d (meters) at pixel (x, y):
//   X = (x - cx) * d / fx,  Y = (y - cy) * d / fy,  Z = d
// Distortion (Brown-Conrady) is optional; most SDKs hand back undistorted depth.
struct DepthIntrinsics {
    int   width  = 0;
    int   height = 0;
    float fx = 0.0f, fy = 0.0f;
    float cx = 0.0f, cy = 0.0f;
    float k1 = 0.0f, k2 = 0.0f, k3 = 0.0f;  // radial
    float p1 = 0.0f, p2 = 0.0f;             // tangential
};

// Identifies a stream for per-stream freshness queries.
enum class Stream { Depth, Color, Infrared, Confidence };

// Which streams a single frame capture refreshed (streams arrive async).
struct StreamFreshness {
    bool depth      = false;
    bool color      = false;
    bool infrared   = false;
    bool confidence = false;

    void clear() { depth = color = infrared = confidence = false; }

    StreamFreshness& operator|=(const StreamFreshness& o) {
        depth |= o.depth; color |= o.color;
        infrared |= o.infrared; confidence |= o.confidence;
        return *this;
    }
    bool any() const { return depth || color || infrared || confidence; }
    bool get(Stream s) const {
        switch (s) {
            case Stream::Depth:      return depth;
            case Stream::Color:      return color;
            case Stream::Infrared:   return infrared;
            case Stream::Confidence: return confidence;
        }
        return false;
    }
};

// =============================================================================
// DepthFrame - the canonical, device-independent frame
// =============================================================================
//
// Owned and triple-buffered by the DepthCamera base. An implementation fills it
// in captureInto(); everything the base offers (distance, world coordinates,
// meshing, recording) reads from it. Color and IR live here too (registered to
// the depth resolution) since they are common to essentially every RGBD camera;
// genuinely device-specific data (raw stereo pair, confidence) stays on
// capability interfaces.
//
// Depth is stored as uint16 + depthScale (meters per unit). RGBD cameras are
// natively uint16 mm (depthScale = 0.001), which keeps frames compact (great
// for recording); long-range LiDAR can use a coarser scale (e.g. 0.01 = cm,
// reaching ~655 m) without changing the meters-based API.
struct DepthFrame {
    int   w = 0;
    int   h = 0;
    float depthScale = 0.001f;          // meters per depth unit (mm by default)

    std::vector<uint16_t> depth;        // w*h, 0 = invalid / no data
    std::vector<Vec3>     world;        // optional precomputed world (m); empty -> deproject
    Pixels color;                       // optional RGBA, registered to depth (w x h)
    Pixels ir;                          // optional single-channel active brightness

    DepthIntrinsics intrinsics;
    double timestamp = 0.0;             // seconds (capture time)

    bool hasColor()    const { return color.isAllocated(); }
    bool hasInfrared() const { return ir.isAllocated(); }
    bool hasWorld()    const { return !world.empty(); }
};

// Options for DepthCamera::toMesh(). Everything is built in a single pass over
// the depth image, so invalid points are skipped and the optional attributes
// stay aligned with the kept vertices. Color attributes require frame.hasColor().
struct DepthMeshOptions {
    bool texCoords = false;  // add UVs into the (depth-registered) color image
    bool colors    = false;  // bake per-vertex RGB (self-contained mesh)
    int  step      = 1;      // decimation: keep every Nth pixel per axis
};

} // namespace tcx
