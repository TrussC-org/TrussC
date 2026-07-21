#pragma once

// =============================================================================
// tcCameraContext.h - camera snapshot for draw-time stamping & picking
//
// A CameraContext is a snapshot of the camera (view / projection / viewport
// size) that part of the scene was drawn under. One is registered ("interned")
// per camera scope — setupScreenFovWithSize, EasyCam::begin, Fbo::begin — NOT
// per node: during the tree draw each node just stamps a pointer to whichever
// context is current. Mouse picking then unprojects the cursor through the
// node's OWN context, so a node drawn under the default perspective screen, an
// EasyCam, or any future camera is picked exactly where it is rendered (the
// pick ray and the render share one camera). Contexts are immutable once
// registered — registration always allocates — so pointers stamped on nodes
// in earlier frames never see their matrices change underneath them.
// =============================================================================

#include <cmath>
#include <memory>
#include <vector>
#include "tc/math/tcRay.h"
#include "tc/graphics/tcClipSpace.h"

namespace trussc {

class CameraContext {
public:
    Mat4  view       = Mat4::identity();
    Mat4  projection = Mat4::identity();
    float viewW = 0.0f;
    float viewH = 0.0f;
    // FBO contexts are not pickable: a click on the main screen must not be
    // tested against geometry that was rendered into an offscreen target
    // (mapping a click on the composited texture back into the FBO scene is
    // an indirection the framework can't guess).
    bool  pickable = true;

    // Unproject a screen point (pixels, top-left origin) into a world-space
    // ray. Same unprojection scheme as screenToWorld(): near plane to mid
    // depth (mid instead of far avoids precision issues with a large far
    // clip). NDC z values come from the backend's clip-space convention —
    // `projection` is stored backend-native (#134).
    Ray screenPointToRay(float screenX, float screenY) const {
        if (viewW <= 0.0f || viewH <= 0.0f) {
            return Ray::fromScreenPoint2D(screenX, screenY);
        }

        // Screen -> NDC (flip Y: screen Y is down, NDC Y is up)
        float ndcX = (screenX / viewW) * 2.0f - 1.0f;
        float ndcY = 1.0f - (screenY / viewH) * 2.0f;

        Mat4 invMvp = (projection * view).inverted();
        Vec4 nearClip = invMvp * Vec4(ndcX, ndcY, internal::ndcNearZ(), 1.0f);
        Vec4 midClip  = invMvp * Vec4(ndcX, ndcY, internal::ndcMidZ(),  1.0f);
        if (std::abs(nearClip.w) < 1e-7f || std::abs(midClip.w) < 1e-7f) {
            return Ray::fromScreenPoint2D(screenX, screenY);
        }

        Vec3 nearPoint(nearClip.x / nearClip.w, nearClip.y / nearClip.w, nearClip.z / nearClip.w);
        Vec3 midPoint (midClip.x  / midClip.w,  midClip.y  / midClip.w,  midClip.z  / midClip.w);
        return Ray(nearPoint, midPoint - nearPoint);
    }

    // Project a world position to screen pixels through this context.
    // Returns (x, y, depth in [0,1]); depth < 0 means behind the camera.
    Vec3 worldToScreen(const Vec3& worldPos) const {
        Vec4 clip = (projection * view) * Vec4(worldPos.x, worldPos.y, worldPos.z, 1.0f);
        if (clip.w < 1e-4f) return Vec3(0.0f, 0.0f, -1.0f);   // behind / at camera
        Vec3 ndc(clip.x / clip.w, clip.y / clip.w, clip.z / clip.w);
        return Vec3((ndc.x + 1.0f) * 0.5f * viewW,
                    (1.0f - ndc.y) * 0.5f * viewH,
                    internal::depthFromNdcZ(ndc.z));
    }
};

namespace internal {

// The context the current drawing happens under (most recent registration)
// lives in WindowContext (tc/app/tcWindowContext.h): reach it via
// internal::currentWindowContext().currentCameraContext. Null until the first
// camera setup of the process (e.g. headless mode), in which case picking
// falls back to the plain vertical screen ray.

// Register a new camera scope. Always allocates a fresh context — see the
// immutability note in the header comment. Defined in tcWindowContext.h
// (needs the complete WindowContext).
inline void registerCameraContext(const Mat4& view, const Mat4& projection,
                                  float viewW, float viewH, bool pickable = true);

// Per-pick ray cache used by one hit-test traversal: the cursor position plus
// one lazily-built ray per camera context encountered (a frame typically has
// one or two). `fixedRay` preserves the raw findHitNode(Ray) API: when set,
// every context resolves to that ray.
struct PickRaySource {
    float screenX = 0.0f;
    float screenY = 0.0f;
    bool hasFixedRay = false;
    Ray fixedRay;

    const Ray& rayFor(const CameraContext* ctx) {
        if (hasFixedRay) return fixedRay;
        for (auto& entry : rays_) {
            if (entry.first == ctx) return entry.second;
        }
        Ray ray = ctx ? ctx->screenPointToRay(screenX, screenY)
                      : Ray::fromScreenPoint2D(screenX, screenY);
        rays_.emplace_back(ctx, ray);
        return rays_.back().second;
    }

private:
    std::vector<std::pair<const CameraContext*, Ray>> rays_;
};

} // namespace internal

} // namespace trussc
