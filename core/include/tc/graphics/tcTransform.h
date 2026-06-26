#pragma once

// =============================================================================
// tcTransform.h - global transform / matrix / style stack free functions
// =============================================================================
// Thin free-function wrappers over the default RenderContext's matrix and style
// stack (pushMatrix / translate / rotate / scale / ...). Extracted from TrussC.h
// so lower-level headers (e.g. tcNode.h) can depend on these directly, in
// dependency order, instead of pulling the whole umbrella. Apps include
// <TrussC.h>; this is an internal piece of it.
// =============================================================================

#include "tcMath.h"                        // Vec3, Quaternion, Mat4
#include "tc/graphics/tcRenderContext.h"   // RenderContext + getDefaultContext()

namespace trussc {

// ---------------------------------------------------------------------------
// Transformations (delegated to RenderContext)
// ---------------------------------------------------------------------------

// Save matrix to stack
inline void pushMatrix() {
    getDefaultContext().pushMatrix();
}

// Restore matrix from stack
inline void popMatrix() {
    getDefaultContext().popMatrix();
}

// Save style to stack (color, fill/stroke, textAlign, etc.)
inline void pushStyle() {
    getDefaultContext().pushStyle();
}

// Restore style from stack
inline void popStyle() {
    getDefaultContext().popStyle();
}

// Reset style to default values (white color, fill enabled, etc.)
inline void resetStyle() {
    getDefaultContext().resetStyle();
}

// Translation
inline void translate(Vec3 pos) {
    getDefaultContext().translate(pos);
}

inline void translate(float x, float y, float z) {
    getDefaultContext().translate(x, y, z);
}

inline void translate(float x, float y) {
    getDefaultContext().translate(x, y);
}

// Z-axis rotation (radians)
inline void rotate(float radians) {
    getDefaultContext().rotate(radians);
}

// X-axis rotation (radians)
inline void rotateX(float radians) {
    getDefaultContext().rotateX(radians);
}

// Y-axis rotation (radians)
inline void rotateY(float radians) {
    getDefaultContext().rotateY(radians);
}

// Z-axis rotation (radians) - explicit
inline void rotateZ(float radians) {
    getDefaultContext().rotateZ(radians);
}

// Rotation in degrees
inline void rotateDeg(float degrees) {
    getDefaultContext().rotateDeg(degrees);
}

inline void rotateXDeg(float degrees) {
    getDefaultContext().rotateXDeg(degrees);
}

inline void rotateYDeg(float degrees) {
    getDefaultContext().rotateYDeg(degrees);
}

inline void rotateZDeg(float degrees) {
    getDefaultContext().rotateZDeg(degrees);
}

// Euler rotation (x, y, z in radians)
inline void rotate(float x, float y, float z) {
    rotateX(x);
    rotateY(y);
    rotateZ(z);
}

inline void rotate(const Vec3& euler) {
    rotate(euler.x, euler.y, euler.z);
}

inline void rotate(const Quaternion& quat) {
    getDefaultContext().rotate(quat);
}

// Euler rotation in degrees
inline void rotateDeg(float x, float y, float z) {
    rotateXDeg(x);
    rotateYDeg(y);
    rotateZDeg(z);
}

inline void rotateDeg(const Vec3& euler) {
    rotateDeg(euler.x, euler.y, euler.z);
}

// Scale (uniform)
inline void scale(float s) {
    getDefaultContext().scale(s);
}

// Scale (non-uniform 2D)
inline void scale(float sx, float sy) {
    getDefaultContext().scale(sx, sy);
}

// Scale (non-uniform 3D)
inline void scale(float sx, float sy, float sz) {
    getDefaultContext().scale(sx, sy, sz);
}

// Get the current transformation matrix
inline Mat4 getMatrix() {
    return getDefaultContext().getMatrix();
}
[[deprecated("Use getMatrix() instead. Will be removed in v1.0.0")]]
inline Mat4 getCurrentMatrix() {
    return getMatrix();
}

// Effective uniform scale of the current matrix (max of x/y basis lengths)
inline float getScale() {
    return getDefaultContext().getScale();
}

// Reset transformation matrix
inline void resetMatrix() {
    getDefaultContext().resetMatrix();
}

// Multiply the current matrix by `mat` (relative transform, like translate/rotate)
inline void multMatrix(const Mat4& mat) {
    getDefaultContext().multMatrix(mat);
}

// Replace the current matrix with `mat` (absolute - use with caution, may break camera setup)
inline void setMatrix(const Mat4& mat) {
    getDefaultContext().setMatrix(mat);
}
[[deprecated("Renamed to setMatrix() (it replaces the current matrix). Will be removed in v1.0.0")]]
inline void loadMatrix(const Mat4& mat) {
    getDefaultContext().setMatrix(mat);
}
[[deprecated("Use getScale() instead. Will be removed in v1.0.0")]]
inline float getCurrentScale() {
    return getDefaultContext().getScale();
}

} // namespace trussc
