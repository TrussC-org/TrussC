#pragma once

// =============================================================================
// TrussC Color Library
// =============================================================================
// Color space library based on OKLab/OKLCH
//
// Color spaces and conversion paths:
//
//        ColorHSB (sRGB-based)
//            ↕
//   Color (sRGB) ↔ ColorLinear ↔ ColorOKLab ↔ ColorOKLCH
//
// - Color:       sRGB (0-1), for display
// - ColorLinear: Linear RGB, for calculations/compositing/HDR
// - ColorHSB:    HSB (H: 0-TAU, S: 0-1, B: 0-1)
// - ColorOKLab:  OKLab (L: 0-1, a: ~-0.4-0.4, b: ~-0.4-0.4)
// - ColorOKLCH:  OKLCH (L: 0-1, C: 0-0.4, H: 0-TAU)
// =============================================================================

#include <cmath>
#include <algorithm>
#include "tcMath.h"  // TAU, HALF_TAU constants

namespace trussc {

// Forward declarations
struct Color;
struct ColorLinear;
struct ColorHSB;
struct ColorOKLab;
struct ColorOKLCH;

// =============================================================================
// Gamma conversion functions (common)
// =============================================================================

// sRGB -> Linear RGB (single channel)
inline float srgbToLinear(float x) {
    if (x <= 0.04045f) return x / 12.92f;
    return std::pow((x + 0.055f) / 1.055f, 2.4f);
}

// Linear RGB -> sRGB (single channel)
inline float linearToSrgb(float x) {
    if (x <= 0.0031308f) return 12.92f * x;
    return 1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f;
}

// =============================================================================
// Color (sRGB)
// =============================================================================
struct Color {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    // Constructor
    Color() = default;
    Color(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    // Grayscale
    explicit Color(float gray, float a_ = 1.0f)
        : r(gray), g(gray), b(gray), a(a_) {}

    // Create Color from 0-255 integer values
    static Color fromBytes(int r, int g, int b, int a = 255) {
        return Color(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
    }

    // From hex (0xRRGGBB or 0xRRGGBBAA)
    static Color fromHex(uint32_t hex, bool hasAlpha = false) {
        if (hasAlpha) {
            return Color(
                ((hex >> 24) & 0xFF) / 255.0f,
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f
            );
        } else {
            return Color(
                ((hex >> 16) & 0xFF) / 255.0f,
                ((hex >> 8) & 0xFF) / 255.0f,
                (hex & 0xFF) / 255.0f,
                1.0f
            );
        }
    }

    // To hex
    uint32_t toHex(bool includeAlpha = false) const {
        uint8_t ri = (uint8_t)(std::clamp(r, 0.0f, 1.0f) * 255);
        uint8_t gi = (uint8_t)(std::clamp(g, 0.0f, 1.0f) * 255);
        uint8_t bi = (uint8_t)(std::clamp(b, 0.0f, 1.0f) * 255);
        uint8_t ai = (uint8_t)(std::clamp(a, 0.0f, 1.0f) * 255);
        if (includeAlpha) {
            return (ri << 24) | (gi << 16) | (bi << 8) | ai;
        }
        return (ri << 16) | (gi << 8) | bi;
    }

    // Conversion (declaration only, implementation below)
    ColorLinear toLinear() const;
    ColorHSB toHSB() const;
    ColorOKLab toOKLab() const;
    ColorOKLCH toOKLCH() const;

    // Operators
    Color operator+(const Color& c) const { return Color(r + c.r, g + c.g, b + c.b, a + c.a); }
    Color operator-(const Color& c) const { return Color(r - c.r, g - c.g, b - c.b, a - c.a); }
    Color operator*(float s) const { return Color(r * s, g * s, b * s, a * s); }
    Color operator/(float s) const { return Color(r / s, g / s, b / s, a / s); }

    // Clamp
    Color clamped() const {
        return Color(
            std::clamp(r, 0.0f, 1.0f),
            std::clamp(g, 0.0f, 1.0f),
            std::clamp(b, 0.0f, 1.0f),
            std::clamp(a, 0.0f, 1.0f)
        );
    }

    // Linear interpolation in sRGB space (not recommended, perceptually non-uniform)
    Color lerpRGB(const Color& target, float t) const {
        return Color(
            r + (target.r - r) * t,
            g + (target.g - g) * t,
            b + (target.b - b) * t,
            a + (target.a - a) * t
        );
    }

    // Interpolation in each color space (declaration only)
    Color lerpLinear(const Color& target, float t) const;
    Color lerpHSB(const Color& target, float t) const;
    Color lerpOKLab(const Color& target, float t) const;
    Color lerpOKLCH(const Color& target, float t) const;

    // Default lerp uses OKLab (perceptually uniform)
    Color lerp(const Color& target, float t) const {
        return lerpOKLab(target, t);
    }

    // Comparison
    bool operator==(const Color& c) const {
        return r == c.r && g == c.g && b == c.b && a == c.a;
    }
    bool operator!=(const Color& c) const { return !(*this == c); }
};

// =============================================================================
// ColorLinear (Linear RGB)
// =============================================================================
struct ColorLinear {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 1.0f;

    // Constructor
    ColorLinear() = default;
    ColorLinear(float r_, float g_, float b_, float a_ = 1.0f)
        : r(r_), g(g_), b(b_), a(a_) {}

    // Grayscale
    explicit ColorLinear(float gray, float a_ = 1.0f)
        : r(gray), g(gray), b(gray), a(a_) {}

    // Conversion (declaration only)
    Color toSRGB() const;
    ColorHSB toHSB() const;
    ColorOKLab toOKLab() const;
    ColorOKLCH toOKLCH() const;

    // Operators (operations in linear space are physically correct)
    ColorLinear operator+(const ColorLinear& c) const { return ColorLinear(r + c.r, g + c.g, b + c.b, a + c.a); }
    ColorLinear operator-(const ColorLinear& c) const { return ColorLinear(r - c.r, g - c.g, b - c.b, a - c.a); }
    ColorLinear operator*(float s) const { return ColorLinear(r * s, g * s, b * s, a * s); }
    ColorLinear operator/(float s) const { return ColorLinear(r / s, g / s, b / s, a / s); }
    ColorLinear operator*(const ColorLinear& c) const { return ColorLinear(r * c.r, g * c.g, b * c.b, a * c.a); }

    // Clamp (unclamped version available for HDR)
    ColorLinear clamped() const {
        return ColorLinear(
            std::max(0.0f, r),
            std::max(0.0f, g),
            std::max(0.0f, b),
            std::clamp(a, 0.0f, 1.0f)
        );
    }

    ColorLinear clampedLDR() const {
        return ColorLinear(
            std::clamp(r, 0.0f, 1.0f),
            std::clamp(g, 0.0f, 1.0f),
            std::clamp(b, 0.0f, 1.0f),
            std::clamp(a, 0.0f, 1.0f)
        );
    }

    // Interpolation in linear space (physically correct)
    ColorLinear lerp(const ColorLinear& target, float t) const {
        return ColorLinear(
            r + (target.r - r) * t,
            g + (target.g - g) * t,
            b + (target.b - b) * t,
            a + (target.a - a) * t
        );
    }

    // Comparison
    bool operator==(const ColorLinear& c) const {
        return r == c.r && g == c.g && b == c.b && a == c.a;
    }
    bool operator!=(const ColorLinear& c) const { return !(*this == c); }
};

// =============================================================================
// ColorHSB
// =============================================================================
struct ColorHSB {
    float h = 0.0f;  // Hue (0 - TAU)
    float s = 0.0f;  // Saturation (0 - 1)
    float b = 1.0f;  // Brightness (0 - 1)
    float a = 1.0f;  // Alpha

    ColorHSB() = default;
    ColorHSB(float h_, float s_, float b_, float a_ = 1.0f)
        : h(h_), s(s_), b(b_), a(a_) {}

    // Conversion
    Color toRGB() const {
        // Normalize h (0-1)
        float hNorm = h / TAU;
        hNorm = hNorm - std::floor(hNorm);

        float r, g, bl;
        int i = (int)(hNorm * 6);
        float f = hNorm * 6 - i;
        float p = b * (1 - s);
        float q = b * (1 - f * s);
        float t = b * (1 - (1 - f) * s);

        switch (i % 6) {
            case 0: r = b; g = t; bl = p; break;
            case 1: r = q; g = b; bl = p; break;
            case 2: r = p; g = b; bl = t; break;
            case 3: r = p; g = q; bl = b; break;
            case 4: r = t; g = p; bl = b; break;
            case 5: r = b; g = p; bl = q; break;
            default: r = g = bl = 0; break;
        }
        return Color(r, g, bl, a);
    }

    ColorLinear toLinear() const;
    ColorOKLab toOKLab() const;
    ColorOKLCH toOKLCH() const;

    // Interpolation in HSB space
    ColorHSB lerp(const ColorHSB& target, float t, bool shortestPath = true) const {
        float newH;
        if (shortestPath) {
            // Interpolate hue via shortest path
            float diff = target.h - h;
            if (diff > HALF_TAU) diff -= TAU;
            if (diff < -HALF_TAU) diff += TAU;
            newH = h + diff * t;
            if (newH < 0) newH += TAU;
            if (newH >= TAU) newH -= TAU;
        } else {
            newH = h + (target.h - h) * t;
        }
        return ColorHSB(
            newH,
            s + (target.s - s) * t,
            b + (target.b - b) * t,
            a + (target.a - a) * t
        );
    }
};

// =============================================================================
// ColorOKLab
// =============================================================================
struct ColorOKLab {
    float L = 0.0f;  // Lightness (0 - 1)
    float a = 0.0f;  // Green-Red (~-0.4 - 0.4)
    float b = 0.0f;  // Blue-Yellow (~-0.4 - 0.4)
    float alpha = 1.0f;

    ColorOKLab() = default;
    ColorOKLab(float L_, float a_, float b_, float alpha_ = 1.0f)
        : L(L_), a(a_), b(b_), alpha(alpha_) {}

    // Convert to Linear RGB
    ColorLinear toLinear() const {
        float l_ = L + 0.3963377774f * a + 0.2158037573f * b;
        float m_ = L - 0.1055613458f * a - 0.0638541728f * b;
        float s_ = L - 0.0894841775f * a - 1.2914855480f * b;

        float l = l_ * l_ * l_;
        float m = m_ * m_ * m_;
        float s = s_ * s_ * s_;

        return ColorLinear(
            +4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s,
            -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
            -0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s,
            alpha
        );
    }

    Color toRGB() const;
    ColorHSB toHSB() const;
    ColorOKLCH toOKLCH() const;

    // Interpolation in OKLab space
    ColorOKLab lerp(const ColorOKLab& target, float t) const {
        return ColorOKLab(
            L + (target.L - L) * t,
            a + (target.a - a) * t,
            b + (target.b - b) * t,
            alpha + (target.alpha - alpha) * t
        );
    }
};

// =============================================================================
// ColorOKLCH
// =============================================================================
struct ColorOKLCH {
    float L = 0.0f;  // Lightness (0 - 1)
    float C = 0.0f;  // Chroma (0 - ~0.4)
    float H = 0.0f;  // Hue (0 - TAU)
    float alpha = 1.0f;

    ColorOKLCH() = default;
    ColorOKLCH(float L_, float C_, float H_, float alpha_ = 1.0f)
        : L(L_), C(C_), H(H_), alpha(alpha_) {}

    // Convert to OKLab
    ColorOKLab toOKLab() const {
        return ColorOKLab(
            L,
            C * std::cos(H),
            C * std::sin(H),
            alpha
        );
    }

    ColorLinear toLinear() const;
    Color toRGB() const;
    ColorHSB toHSB() const;

    // Interpolation in OKLCH space (hue via shortest path)
    ColorOKLCH lerp(const ColorOKLCH& target, float t, bool shortestPath = true) const {
        float newH;
        if (shortestPath) {
            float diff = target.H - H;
            if (diff > HALF_TAU) diff -= TAU;
            if (diff < -HALF_TAU) diff += TAU;
            newH = H + diff * t;
            if (newH < 0) newH += TAU;
            if (newH >= TAU) newH -= TAU;
        } else {
            newH = H + (target.H - H) * t;
        }

        // Handle case when chroma is near 0 (gray has undefined hue)
        float newC = C + (target.C - C) * t;
        if (C < 0.001f && target.C >= 0.001f) {
            newH = target.H;
        } else if (target.C < 0.001f && C >= 0.001f) {
            newH = H;
        }

        return ColorOKLCH(
            L + (target.L - L) * t,
            newC,
            newH,
            alpha + (target.alpha - alpha) * t
        );
    }
};

// =============================================================================
// Conversion method implementations
// =============================================================================

// --- Color ---
inline ColorLinear Color::toLinear() const {
    return ColorLinear(
        srgbToLinear(r),
        srgbToLinear(g),
        srgbToLinear(b),
        a
    );
}

inline ColorHSB Color::toHSB() const {
    float maxVal = std::max({r, g, b});
    float minVal = std::min({r, g, b});
    float delta = maxVal - minVal;

    float h = 0, s = 0, bri = maxVal;

    if (delta > 0) {
        s = delta / maxVal;
        if (maxVal == r) {
            h = (g - b) / delta;
            if (g < b) h += 6;
        } else if (maxVal == g) {
            h = (b - r) / delta + 2;
        } else {
            h = (r - g) / delta + 4;
        }
        h /= 6.0f;  // Normalize to 0-1
        h *= TAU;   // Scale to TAU
    }

    return ColorHSB(h, s, bri, a);
}

inline ColorOKLab Color::toOKLab() const {
    return toLinear().toOKLab();
}

inline ColorOKLCH Color::toOKLCH() const {
    return toLinear().toOKLab().toOKLCH();
}

inline Color Color::lerpLinear(const Color& target, float t) const {
    return toLinear().lerp(target.toLinear(), t).toSRGB();
}

inline Color Color::lerpHSB(const Color& target, float t) const {
    return toHSB().lerp(target.toHSB(), t).toRGB();
}

inline Color Color::lerpOKLab(const Color& target, float t) const {
    return toOKLab().lerp(target.toOKLab(), t).toRGB();
}

inline Color Color::lerpOKLCH(const Color& target, float t) const {
    return toOKLCH().lerp(target.toOKLCH(), t).toRGB();
}

// --- ColorLinear ---
inline Color ColorLinear::toSRGB() const {
    return Color(
        linearToSrgb(r),
        linearToSrgb(g),
        linearToSrgb(b),
        a
    );
}

inline ColorHSB ColorLinear::toHSB() const {
    return toSRGB().toHSB();
}

inline ColorOKLab ColorLinear::toOKLab() const {
    float l = 0.4122214708f * r + 0.5363325363f * g + 0.0514459929f * b;
    float m = 0.2119034982f * r + 0.6806995451f * g + 0.1073969566f * b;
    float s = 0.0883024619f * r + 0.2817188376f * g + 0.6299787005f * b;

    float l_ = std::cbrt(l);
    float m_ = std::cbrt(m);
    float s_ = std::cbrt(s);

    return ColorOKLab(
        0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_,
        1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
        0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_,
        a
    );
}

inline ColorOKLCH ColorLinear::toOKLCH() const {
    return toOKLab().toOKLCH();
}

// --- ColorHSB ---
inline ColorLinear ColorHSB::toLinear() const {
    return toRGB().toLinear();
}

inline ColorOKLab ColorHSB::toOKLab() const {
    return toRGB().toOKLab();
}

inline ColorOKLCH ColorHSB::toOKLCH() const {
    return toRGB().toOKLCH();
}

// --- ColorOKLab ---
inline Color ColorOKLab::toRGB() const {
    return toLinear().toSRGB();
}

inline ColorHSB ColorOKLab::toHSB() const {
    return toRGB().toHSB();
}

inline ColorOKLCH ColorOKLab::toOKLCH() const {
    float C = std::sqrt(a * a + b * b);
    float H = std::atan2(b, a);
    if (H < 0) H += TAU;
    return ColorOKLCH(L, C, H, alpha);
}

// --- ColorOKLCH ---
inline ColorLinear ColorOKLCH::toLinear() const {
    return toOKLab().toLinear();
}

inline Color ColorOKLCH::toRGB() const {
    return toLinear().toSRGB();
}

inline ColorHSB ColorOKLCH::toHSB() const {
    return toRGB().toHSB();
}

// =============================================================================
// Convenient factory functions
// =============================================================================

// Create Color from HSB
inline Color colorFromHSB(float h, float s, float b, float a = 1.0f) {
    return ColorHSB(h, s, b, a).toRGB();
}

// Create Color from OKLCH
inline Color colorFromOKLCH(float L, float C, float H, float a = 1.0f) {
    return ColorOKLCH(L, C, H, a).toRGB();
}

// Create Color from OKLab
inline Color colorFromOKLab(float L, float a_lab, float b_lab, float alpha = 1.0f) {
    return ColorOKLab(L, a_lab, b_lab, alpha).toRGB();
}

// Create Color from Linear RGB
inline Color colorFromLinear(float r, float g, float b, float a = 1.0f) {
    return ColorLinear(r, g, b, a).toSRGB();
}

// =============================================================================
// Predefined colors
// =============================================================================
namespace colors {
    // Basic colors
    inline const Color white(1.0f, 1.0f, 1.0f);
    inline const Color black(0.0f, 0.0f, 0.0f);
    inline const Color red(1.0f, 0.0f, 0.0f);
    inline const Color green(0.0f, 1.0f, 0.0f);
    inline const Color blue(0.0f, 0.0f, 1.0f);
    inline const Color yellow(1.0f, 1.0f, 0.0f);
    inline const Color cyan(0.0f, 1.0f, 1.0f);
    inline const Color magenta(1.0f, 0.0f, 1.0f);
    inline const Color transparent(0.0f, 0.0f, 0.0f, 0.0f);

    // Grays
    inline const Color gray(0.501961f, 0.501961f, 0.501961f);
    inline const Color grey(0.501961f, 0.501961f, 0.501961f);
    inline const Color darkGray(0.662745f, 0.662745f, 0.662745f);
    inline const Color darkGrey(0.662745f, 0.662745f, 0.662745f);
    inline const Color dimGray(0.411765f, 0.411765f, 0.411765f);
    inline const Color dimGrey(0.411765f, 0.411765f, 0.411765f);
    inline const Color lightGray(0.827451f, 0.827451f, 0.827451f);
    inline const Color lightGrey(0.827451f, 0.827451f, 0.827451f);
    inline const Color silver(0.752941f, 0.752941f, 0.752941f);
    inline const Color gainsboro(0.862745f, 0.862745f, 0.862745f);
    inline const Color whiteSmoke(0.960784f, 0.960784f, 0.960784f);

    // Reds
    inline const Color darkRed(0.545098f, 0.0f, 0.0f);
    inline const Color fireBrick(0.698039f, 0.133333f, 0.133333f);
    inline const Color crimson(0.862745f, 0.0784314f, 0.235294f);
    inline const Color indianRed(0.803922f, 0.360784f, 0.360784f);
    inline const Color lightCoral(0.941176f, 0.501961f, 0.501961f);
    inline const Color salmon(0.980392f, 0.501961f, 0.447059f);
    inline const Color darkSalmon(0.913725f, 0.588235f, 0.478431f);
    inline const Color lightSalmon(1.0f, 0.627451f, 0.478431f);

    // Oranges
    inline const Color orange(1.0f, 0.647059f, 0.0f);
    inline const Color darkOrange(1.0f, 0.54902f, 0.0f);
    inline const Color orangeRed(1.0f, 0.270588f, 0.0f);
    inline const Color tomato(1.0f, 0.388235f, 0.278431f);
    inline const Color coral(1.0f, 0.498039f, 0.313726f);

    // Yellows
    inline const Color gold(1.0f, 0.843137f, 0.0f);
    inline const Color goldenRod(0.854902f, 0.647059f, 0.12549f);
    inline const Color darkGoldenRod(0.721569f, 0.52549f, 0.0431373f);
    inline const Color paleGoldenRod(0.933333f, 0.909804f, 0.666667f);
    inline const Color lightGoldenRodYellow(0.980392f, 0.980392f, 0.823529f);
    inline const Color khaki(0.941176f, 0.901961f, 0.54902f);
    inline const Color darkKhaki(0.741176f, 0.717647f, 0.419608f);

    // Greens
    inline const Color lime(0.0f, 1.0f, 0.0f);
    inline const Color limeGreen(0.196078f, 0.803922f, 0.196078f);
    inline const Color lightGreen(0.564706f, 0.933333f, 0.564706f);
    inline const Color paleGreen(0.596078f, 0.984314f, 0.596078f);
    inline const Color darkGreen(0.0f, 0.392157f, 0.0f);
    inline const Color forestGreen(0.133333f, 0.545098f, 0.133333f);
    inline const Color seaGreen(0.180392f, 0.545098f, 0.341176f);
    inline const Color mediumSeaGreen(0.235294f, 0.701961f, 0.443137f);
    inline const Color darkSeaGreen(0.560784f, 0.737255f, 0.560784f);
    inline const Color lightSeaGreen(0.12549f, 0.698039f, 0.666667f);
    inline const Color springGreen(0.0f, 1.0f, 0.498039f);
    inline const Color mediumSpringGreen(0.0f, 0.980392f, 0.603922f);
    inline const Color greenYellow(0.678431f, 1.0f, 0.184314f);
    inline const Color yellowGreen(0.603922f, 0.803922f, 0.196078f);
    inline const Color chartreuse(0.498039f, 1.0f, 0.0f);
    inline const Color lawnGreen(0.486275f, 0.988235f, 0.0f);
    inline const Color olive(0.501961f, 0.501961f, 0.0f);
    inline const Color oliveDrab(0.419608f, 0.556863f, 0.137255f);
    inline const Color darkOliveGreen(0.333333f, 0.419608f, 0.184314f);

    // Cyans
    inline const Color aqua(0.0f, 1.0f, 1.0f);
    inline const Color aquamarine(0.498039f, 1.0f, 0.831373f);
    inline const Color mediumAquaMarine(0.4f, 0.803922f, 0.666667f);
    inline const Color darkCyan(0.0f, 0.545098f, 0.545098f);
    inline const Color teal(0.0f, 0.501961f, 0.501961f);
    inline const Color lightCyan(0.878431f, 1.0f, 1.0f);
    inline const Color turquoise(0.25098f, 0.878431f, 0.815686f);
    inline const Color mediumTurquoise(0.282353f, 0.819608f, 0.8f);
    inline const Color darkTurquoise(0.0f, 0.807843f, 0.819608f);
    inline const Color paleTurquoise(0.686275f, 0.933333f, 0.933333f);

    // Blues
    inline const Color navy(0.0f, 0.0f, 0.501961f);
    inline const Color darkBlue(0.0f, 0.0f, 0.545098f);
    inline const Color mediumBlue(0.0f, 0.0f, 0.803922f);
    inline const Color royalBlue(0.254902f, 0.411765f, 0.882353f);
    inline const Color steelBlue(0.27451f, 0.509804f, 0.705882f);
    inline const Color blueSteel(0.27451f, 0.509804f, 0.705882f);
    inline const Color lightSteelBlue(0.690196f, 0.768627f, 0.870588f);
    inline const Color dodgerBlue(0.117647f, 0.564706f, 1.0f);
    inline const Color deepSkyBlue(0.0f, 0.74902f, 1.0f);
    inline const Color skyBlue(0.529412f, 0.807843f, 0.921569f);
    inline const Color lightSkyBlue(0.529412f, 0.807843f, 0.980392f);
    inline const Color lightBlue(0.678431f, 0.847059f, 0.901961f);
    inline const Color powderBlue(0.690196f, 0.878431f, 0.901961f);
    inline const Color cornflowerBlue(0.392157f, 0.584314f, 0.929412f);
    inline const Color cadetBlue(0.372549f, 0.619608f, 0.627451f);
    inline const Color midnightBlue(0.0980392f, 0.0980392f, 0.439216f);
    inline const Color aliceBlue(0.941176f, 0.972549f, 1.0f);

    // Purples
    inline const Color purple(0.501961f, 0.0f, 0.501961f);
    inline const Color darkMagenta(0.545098f, 0.0f, 0.545098f);
    inline const Color darkViolet(0.580392f, 0.0f, 0.827451f);
    inline const Color blueViolet(0.541176f, 0.168627f, 0.886275f);
    inline const Color indigo(0.294118f, 0.0f, 0.509804f);
    inline const Color slateBlue(0.415686f, 0.352941f, 0.803922f);
    inline const Color darkSlateBlue(0.282353f, 0.239216f, 0.545098f);
    inline const Color mediumSlateBlue(0.482353f, 0.407843f, 0.933333f);
    inline const Color mediumPurple(0.576471f, 0.439216f, 0.858824f);
    inline const Color darkOrchid(0.6f, 0.196078f, 0.8f);
    inline const Color mediumOrchid(0.729412f, 0.333333f, 0.827451f);
    inline const Color orchid(0.854902f, 0.439216f, 0.839216f);
    inline const Color violet(0.933333f, 0.509804f, 0.933333f);
    inline const Color plum(0.866667f, 0.627451f, 0.866667f);
    inline const Color thistle(0.847059f, 0.74902f, 0.847059f);
    inline const Color lavender(0.901961f, 0.901961f, 0.980392f);
    inline const Color fuchsia(1.0f, 0.0f, 1.0f);

    // Pinks
    inline const Color pink(1.0f, 0.752941f, 0.796078f);
    inline const Color lightPink(1.0f, 0.713726f, 0.756863f);
    inline const Color hotPink(1.0f, 0.411765f, 0.705882f);
    inline const Color deepPink(1.0f, 0.0784314f, 0.576471f);
    inline const Color mediumVioletRed(0.780392f, 0.0823529f, 0.521569f);
    inline const Color paleVioletRed(0.858824f, 0.439216f, 0.576471f);

    // Browns
    inline const Color brown(0.647059f, 0.164706f, 0.164706f);
    inline const Color maroon(0.501961f, 0.0f, 0.0f);
    inline const Color saddleBrown(0.545098f, 0.270588f, 0.0745098f);
    inline const Color sienna(0.627451f, 0.321569f, 0.176471f);
    inline const Color chocolate(0.823529f, 0.411765f, 0.117647f);
    inline const Color peru(0.803922f, 0.521569f, 0.247059f);
    inline const Color sandyBrown(0.956863f, 0.643137f, 0.376471f);
    inline const Color burlyWood(0.870588f, 0.721569f, 0.529412f);
    inline const Color tan(0.823529f, 0.705882f, 0.54902f);
    inline const Color rosyBrown(0.737255f, 0.560784f, 0.560784f);

    // Whites
    inline const Color snow(1.0f, 0.980392f, 0.980392f);
    inline const Color honeyDew(0.941176f, 1.0f, 0.941176f);
    inline const Color mintCream(0.960784f, 1.0f, 0.980392f);
    inline const Color azure(0.941176f, 1.0f, 1.0f);
    inline const Color ghostWhite(0.972549f, 0.972549f, 1.0f);
    inline const Color floralWhite(1.0f, 0.980392f, 0.941176f);
    inline const Color ivory(1.0f, 1.0f, 0.941176f);
    inline const Color antiqueWhite(0.980392f, 0.921569f, 0.843137f);
    inline const Color linen(0.980392f, 0.941176f, 0.901961f);
    inline const Color lavenderBlush(1.0f, 0.941176f, 0.960784f);
    inline const Color mistyRose(1.0f, 0.894118f, 0.882353f);
    inline const Color oldLace(0.992157f, 0.960784f, 0.901961f);
    inline const Color seaShell(1.0f, 0.960784f, 0.933333f);
    inline const Color beige(0.960784f, 0.960784f, 0.862745f);
    inline const Color cornsilk(1.0f, 0.972549f, 0.862745f);
    inline const Color lemonChiffon(1.0f, 0.980392f, 0.803922f);
    inline const Color lightYellow(1.0f, 1.0f, 0.878431f);
    inline const Color wheat(0.960784f, 0.870588f, 0.701961f);
    inline const Color moccasin(1.0f, 0.894118f, 0.709804f);
    inline const Color peachPuff(1.0f, 0.854902f, 0.72549f);
    inline const Color papayaWhip(1.0f, 0.937255f, 0.835294f);
    inline const Color blanchedAlmond(1.0f, 0.921569f, 0.803922f);
    inline const Color bisque(1.0f, 0.894118f, 0.768627f);
    inline const Color navajoWhite(1.0f, 0.870588f, 0.678431f);

    // Slates
    inline const Color slateGray(0.439216f, 0.501961f, 0.564706f);
    inline const Color slateGrey(0.439216f, 0.501961f, 0.564706f);
    inline const Color lightSlateGray(0.466667f, 0.533333f, 0.6f);
    inline const Color lightSlateGrey(0.466667f, 0.533333f, 0.6f);
    inline const Color darkSlateGray(0.184314f, 0.309804f, 0.309804f);
    inline const Color darkSlateGrey(0.184314f, 0.309804f, 0.309804f);
}

} // namespace trussc

// Namespace alias
namespace tc = trussc;
