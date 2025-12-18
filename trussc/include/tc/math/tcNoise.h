#pragma once

// tc::noise - パーリンノイズ関数
// oFのofNoise()互換API

#include "stb/stb_perlin.h"

namespace trussc {

// ---------------------------------------------------------------------------
// 基本ノイズ関数（-1.0 〜 1.0 を返す）
// ---------------------------------------------------------------------------

// 1Dノイズ
inline float signedNoise(float x) {
    return stb_perlin_noise3(x, 0.0f, 0.0f, 0, 0, 0);
}

// 2Dノイズ
inline float signedNoise(float x, float y) {
    return stb_perlin_noise3(x, y, 0.0f, 0, 0, 0);
}

// 3Dノイズ
inline float signedNoise(float x, float y, float z) {
    return stb_perlin_noise3(x, y, z, 0, 0, 0);
}

// 4Dノイズ（zとwを組み合わせ）
inline float signedNoise(float x, float y, float z, float w) {
    // 4Dノイズは3Dノイズを組み合わせて近似
    float n1 = stb_perlin_noise3(x, y, z, 0, 0, 0);
    float n2 = stb_perlin_noise3(x + w, y + w, z + w, 0, 0, 0);
    return (n1 + n2) * 0.5f;
}

// ---------------------------------------------------------------------------
// 正規化ノイズ関数（0.0 〜 1.0 を返す）- oF互換
// ---------------------------------------------------------------------------

// 1Dノイズ
inline float noise(float x) {
    return signedNoise(x) * 0.5f + 0.5f;
}

// 2Dノイズ
inline float noise(float x, float y) {
    return signedNoise(x, y) * 0.5f + 0.5f;
}

// 3Dノイズ
inline float noise(float x, float y, float z) {
    return signedNoise(x, y, z) * 0.5f + 0.5f;
}

// 4Dノイズ
inline float noise(float x, float y, float z, float w) {
    return signedNoise(x, y, z, w) * 0.5f + 0.5f;
}

// ---------------------------------------------------------------------------
// Vec版オーバーロード
// ---------------------------------------------------------------------------

inline float signedNoise(const Vec2& v) {
    return signedNoise(v.x, v.y);
}

inline float signedNoise(const Vec3& v) {
    return signedNoise(v.x, v.y, v.z);
}

inline float noise(const Vec2& v) {
    return noise(v.x, v.y);
}

inline float noise(const Vec3& v) {
    return noise(v.x, v.y, v.z);
}

// ---------------------------------------------------------------------------
// フラクタルノイズ（fbm: Fractal Brownian Motion）
// ---------------------------------------------------------------------------

// 2D fbm
inline float fbm(float x, float y, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        sum += signedNoise(x * frequency, y * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return (sum / maxValue) * 0.5f + 0.5f;  // 0〜1に正規化
}

// 3D fbm
inline float fbm(float x, float y, float z, int octaves = 4, float lacunarity = 2.0f, float gain = 0.5f) {
    float sum = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxValue = 0.0f;

    for (int i = 0; i < octaves; i++) {
        sum += signedNoise(x * frequency, y * frequency, z * frequency) * amplitude;
        maxValue += amplitude;
        amplitude *= gain;
        frequency *= lacunarity;
    }

    return (sum / maxValue) * 0.5f + 0.5f;
}

// ---------------------------------------------------------------------------
// シード付きノイズ
// ---------------------------------------------------------------------------

inline float signedNoise(float x, float y, float z, int seed) {
    return stb_perlin_noise3_seed(x, y, z, 0, 0, 0, seed);
}

inline float noise(float x, float y, float z, int seed) {
    return signedNoise(x, y, z, seed) * 0.5f + 0.5f;
}

} // namespace trussc
