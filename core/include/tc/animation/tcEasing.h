#pragma once

#include <cmath>
#include <functional>
#include "../../tcMath.h"
#include "../utils/tcReflect.h"

namespace trussc {

// Easing type - defines the curve shape
enum class EaseType {
    Linear,   // No easing
    Quad,     // Quadratic (t^2)
    Cubic,    // Cubic (t^3)
    Quart,    // Quartic (t^4)
    Quint,    // Quintic (t^5)
    Sine,     // Sinusoidal
    Expo,     // Exponential
    Circ,     // Circular
    Back,     // Overshoot
    Elastic,  // Elastic spring
    Bounce,   // Bouncing
    Custom    // User-supplied curve function (falls back to Linear when none is set)
};
TC_ENUM_LABELS(EaseType, "Linear", "Quad", "Cubic", "Quart", "Quint", "Sine",
               "Expo", "Circ", "Back", "Elastic", "Bounce", "Custom")

// User-defined easing curve: maps normalized time t (0-1) to progress.
// May capture state (lambdas, functors) — e.g. a live-editable curve object.
// Values outside 0-1 are allowed (overshoot, Back/Elastic-style).
using EaseFunction = std::function<float(float)>;

// Easing mode - defines acceleration/deceleration
enum class EaseMode {
    In,      // Accelerate
    Out,     // Decelerate
    InOut    // Accelerate then decelerate
};
TC_ENUM_LABELS(EaseMode, "In", "Out", "InOut")

namespace internal {

// Base easing curves (ease-in form)
// These are the fundamental curves that get transformed for Out/InOut

inline float easeLinear(float t) {
    return t;
}

inline float easeQuad(float t) {
    return t * t;
}

inline float easeCubic(float t) {
    return t * t * t;
}

inline float easeQuart(float t) {
    return t * t * t * t;
}

inline float easeQuint(float t) {
    return t * t * t * t * t;
}

inline float easeSine(float t) {
    return 1.0f - std::cos(t * QUARTER_TAU);
}

inline float easeExpo(float t) {
    return t == 0.0f ? 0.0f : std::pow(2.0f, 10.0f * (t - 1.0f));
}

inline float easeCirc(float t) {
    return 1.0f - std::sqrt(1.0f - t * t);
}

inline float easeBack(float t) {
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    return c3 * t * t * t - c1 * t * t;
}

inline float easeElastic(float t) {
    if (t == 0.0f || t == 1.0f) return t;
    constexpr float c4 = TAU / 3.0f;
    return -std::pow(2.0f, 10.0f * t - 10.0f) * std::sin((t * 10.0f - 10.75f) * c4);
}

inline float easeBounce(float t) {
    // Bounce out, then we'll flip it for ease-in
    float t1 = 1.0f - t;
    constexpr float n1 = 7.5625f;
    constexpr float d1 = 2.75f;
    float result;
    if (t1 < 1.0f / d1) {
        result = n1 * t1 * t1;
    } else if (t1 < 2.0f / d1) {
        t1 -= 1.5f / d1;
        result = n1 * t1 * t1 + 0.75f;
    } else if (t1 < 2.5f / d1) {
        t1 -= 2.25f / d1;
        result = n1 * t1 * t1 + 0.9375f;
    } else {
        t1 -= 2.625f / d1;
        result = n1 * t1 * t1 + 0.984375f;
    }
    return 1.0f - result;  // Flip to get ease-in
}

// Get the base ease-in curve for a given type
inline float getBaseCurve(float t, EaseType type) {
    switch (type) {
        case EaseType::Linear:  return easeLinear(t);
        case EaseType::Quad:    return easeQuad(t);
        case EaseType::Cubic:   return easeCubic(t);
        case EaseType::Quart:   return easeQuart(t);
        case EaseType::Quint:   return easeQuint(t);
        case EaseType::Sine:    return easeSine(t);
        case EaseType::Expo:    return easeExpo(t);
        case EaseType::Circ:    return easeCirc(t);
        case EaseType::Back:    return easeBack(t);
        case EaseType::Elastic: return easeElastic(t);
        case EaseType::Bounce:  return easeBounce(t);
        default:                return t;
    }
}

} // namespace internal

// ----- Public API -----

// Ease-in: accelerate from zero velocity
inline float easeIn(float t, EaseType type) {
    return internal::getBaseCurve(t, type);
}

// Ease-out: decelerate to zero velocity
inline float easeOut(float t, EaseType type) {
    // Flip the curve: f_out(t) = 1 - f_in(1 - t)
    return 1.0f - internal::getBaseCurve(1.0f - t, type);
}

// Ease-in-out: accelerate then decelerate (symmetric)
inline float easeInOut(float t, EaseType type) {
    if (t < 0.5f) {
        // First half: ease-in scaled to [0, 0.5]
        return internal::getBaseCurve(t * 2.0f, type) * 0.5f;
    } else {
        // Second half: ease-out scaled to [0.5, 1]
        return 1.0f - internal::getBaseCurve((1.0f - t) * 2.0f, type) * 0.5f;
    }
}

// Ease-in-out with different in/out types (asymmetric)
inline float easeInOut(float t, EaseType inType, EaseType outType) {
    if (t < 0.5f) {
        // First half: ease-in with inType
        return internal::getBaseCurve(t * 2.0f, inType) * 0.5f;
    } else {
        // Second half: ease-out with outType
        return 1.0f - internal::getBaseCurve((1.0f - t) * 2.0f, outType) * 0.5f;
    }
}

// Convenience: apply easing with mode
inline float ease(float t, EaseType type, EaseMode mode) {
    switch (mode) {
        case EaseMode::In:    return easeIn(t, type);
        case EaseMode::Out:   return easeOut(t, type);
        case EaseMode::InOut: return easeInOut(t, type);
        default:              return t;
    }
}

// ----- Custom-curve variants -----
// The supplied function is treated as the ease-in base curve, exactly like
// the built-in types: Out/InOut are derived by the standard reflection.
// A null function falls back to linear.

// Ease-in: the curve as authored
inline float easeIn(float t, const EaseFunction& fn) {
    return fn ? fn(t) : t;
}

// Ease-out: time-and-value flipped curve
inline float easeOut(float t, const EaseFunction& fn) {
    return fn ? 1.0f - fn(1.0f - t) : t;
}

// Ease-in-out: first half ease-in, second half ease-out (both halved)
inline float easeInOut(float t, const EaseFunction& fn) {
    if (!fn) return t;
    if (t < 0.5f) {
        return fn(t * 2.0f) * 0.5f;
    } else {
        return 1.0f - fn((1.0f - t) * 2.0f) * 0.5f;
    }
}

// Convenience: apply a custom curve with mode
inline float ease(float t, const EaseFunction& fn, EaseMode mode) {
    switch (mode) {
        case EaseMode::In:    return easeIn(t, fn);
        case EaseMode::Out:   return easeOut(t, fn);
        case EaseMode::InOut: return easeInOut(t, fn);
        default:              return t;
    }
}

} // namespace trussc
