#pragma once

#include "tcMath.h"

namespace trussc {

// =============================================================================
// Ray - Ray (origin + direction)
// Basic struct for unified hit testing
// =============================================================================

struct Ray {
    Vec3 origin;      // Origin
    Vec3 direction;   // Direction (assumed normalized)

    // Constructor
    Ray() : origin(0, 0, 0), direction(0, 0, -1) {}
    Ray(const Vec3& o, const Vec3& d) : origin(o), direction(d.normalized()) {}

    // Get position at t: P(t) = origin + direction * t
    Vec3 at(float t) const {
        return origin + direction * t;
    }

    // Transform with inverse matrix (transform to node's local space)
    // origin: transform as point
    // direction: transform as direction (not affected by translation)
    Ray transformed(const Mat4& inverseMatrix) const {
        // Transform origin as point
        Vec3 newOrigin = inverseMatrix * origin;

        // Transform direction as direction (treat w=0)
        // Only use 3x3 part of matrix to exclude translation effect
        Vec3 newDir;
        newDir.x = inverseMatrix.m[0] * direction.x + inverseMatrix.m[1] * direction.y + inverseMatrix.m[2] * direction.z;
        newDir.y = inverseMatrix.m[4] * direction.x + inverseMatrix.m[5] * direction.y + inverseMatrix.m[6] * direction.z;
        newDir.z = inverseMatrix.m[8] * direction.x + inverseMatrix.m[9] * direction.y + inverseMatrix.m[10] * direction.z;

        return Ray(newOrigin, newDir);  // Normalized in constructor
    }

    // ==========================================================================
    // 2D helper: Generate ray from mouse coordinates (orthographic)
    // ==========================================================================

    // 2D mode (orthographic): Generate ray parallel to Z axis from mouse position
    // Assumes camera looks from Z+ toward Z-
    static Ray fromScreenPoint2D(float screenX, float screenY, float startZ = 1000.0f) {
        return Ray(
            Vec3(screenX, screenY, startZ),
            Vec3(0, 0, -1)
        );
    }

    // ==========================================================================
    // Hit test result
    // ==========================================================================

    // Result of an intersect* call, returned by value the way Node::HitResult
    // is. The out-param overloads below force an uninitialized local at every
    // call site, cannot be used in an `if` initializer, and cannot be bound to
    // Lua at all (the binding generator skips out-params), so they only remain
    // as thin adapters until v1.0.0.
    struct Hit {
        bool  hit = false;   // true if the ray hit
        float t = 0.0f;      // distance along the ray -- only meaningful when hit
        Vec3  point;         // intersection point -- only meaningful when hit

        explicit operator bool() const { return hit; }
    };

    // ==========================================================================
    // Plane intersection
    // ==========================================================================

    // Intersection with the Z=0 plane (for 2D UI)
    Hit intersectZPlane() const {
        // If direction.z is near 0, parallel (no intersection)
        if (std::abs(direction.z) < 1e-6f) {
            return {};
        }

        // Intersection with Z=0 plane: origin.z + t * direction.z = 0
        const float t = -origin.z / direction.z;

        // t < 0 is behind the ray (no intersection)
        if (t < 0) {
            return {};
        }

        return { true, t, at(t) };
    }

    [[deprecated("Use the Hit-returning intersectZPlane() instead. Will be removed in v1.0.0")]]
    bool intersectZPlane(float& outT, Vec3& outPoint) const {
        const Hit h = intersectZPlane();
        if (h.hit) {
            outT = h.t;
            outPoint = h.point;
        }
        return h.hit;
    }

    // Intersection with an arbitrary plane
    // planeNormal: plane normal (normalized)
    // planeD: plane distance (signed distance from origin)
    Hit intersectPlane(const Vec3& planeNormal, float planeD) const {
        const float denom = direction.dot(planeNormal);

        // Check if parallel
        if (std::abs(denom) < 1e-6f) {
            return {};
        }

        const float t = -(origin.dot(planeNormal) + planeD) / denom;

        if (t < 0) {
            return {};
        }

        return { true, t, at(t) };
    }

    [[deprecated("Use the Hit-returning intersectPlane() instead. Will be removed in v1.0.0")]]
    bool intersectPlane(const Vec3& planeNormal, float planeD, float& outT, Vec3& outPoint) const {
        const Hit h = intersectPlane(planeNormal, planeD);
        if (h.hit) {
            outT = h.t;
            outPoint = h.point;
        }
        return h.hit;
    }

    // ==========================================================================
    // Sphere intersection
    // ==========================================================================

    // Intersection with a sphere centered at the origin
    Hit intersectSphere(float radius) const {
        // |origin + t * direction|^2 = radius^2
        // a*t^2 + b*t + c = 0
        const float a = direction.dot(direction);  // 1 if normalized
        const float b = 2.0f * origin.dot(direction);
        const float c = origin.dot(origin) - radius * radius;

        const float discriminant = b * b - 4 * a * c;
        if (discriminant < 0) {
            return {};
        }

        const float sqrtD = std::sqrt(discriminant);
        const float t1 = (-b - sqrtD) / (2 * a);
        const float t2 = (-b + sqrtD) / (2 * a);

        // Select the closest positive t
        if (t1 > 0) {
            return { true, t1, at(t1) };
        }
        if (t2 > 0) {
            return { true, t2, at(t2) };
        }

        return {};
    }

    [[deprecated("Use the Hit-returning intersectSphere() instead. Will be removed in v1.0.0")]]
    bool intersectSphere(float radius, float& outT) const {
        const Hit h = intersectSphere(radius);
        if (h.hit) {
            outT = h.t;
        }
        return h.hit;
    }

    // ==========================================================================
    // AABB (Axis-Aligned Bounding Box) intersection
    // ==========================================================================

    // Intersection with an AABB defined by min/max
    Hit intersectAABB(const Vec3& boxMin, const Vec3& boxMax) const {
        float tmin = 0.0f;
        float tmax = std::numeric_limits<float>::max();

        for (int i = 0; i < 3; i++) {
            const float invD = 1.0f / direction[i];
            float t0 = (boxMin[i] - origin[i]) * invD;
            float t1 = (boxMax[i] - origin[i]) * invD;

            if (invD < 0.0f) {
                std::swap(t0, t1);
            }

            tmin = std::max(tmin, t0);
            tmax = std::min(tmax, t1);

            if (tmax < tmin) {
                return {};
            }
        }

        return { true, tmin, at(tmin) };
    }

    [[deprecated("Use the Hit-returning intersectAABB() instead. Will be removed in v1.0.0")]]
    bool intersectAABB(const Vec3& boxMin, const Vec3& boxMax, float& outT) const {
        const Hit h = intersectAABB(boxMin, boxMax);
        if (h.hit) {
            outT = h.t;
        }
        return h.hit;
    }
};

} // namespace trussc
