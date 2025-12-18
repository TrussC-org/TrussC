#pragma once

#include <cmath>
#include <algorithm>
#include <random>

// =============================================================================
// TrussC 数学ライブラリ
// =============================================================================

namespace trussc {

// 数学定数（TAU を基本とする）
constexpr float TAU = 6.28318530717958647693f;      // 円周率 τ = 2π
constexpr float HALF_TAU = TAU / 2.0f;              // τ/2 = π
constexpr float QUARTER_TAU = TAU / 4.0f;           // τ/4 = π/2
constexpr float PI = HALF_TAU;                      // π = τ/2（互換性のため）

// =============================================================================
// Vec2 - 2次元ベクトル
// =============================================================================

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    // コンストラクタ
    Vec2() = default;
    Vec2(float x_, float y_) : x(x_), y(y_) {}
    Vec2(float v) : x(v), y(v) {}
    Vec2(const Vec2&) = default;
    Vec2& operator=(const Vec2&) = default;

    // 配列アクセス
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    // 算術演算子
    Vec2 operator+(const Vec2& v) const { return Vec2(x + v.x, y + v.y); }
    Vec2 operator-(const Vec2& v) const { return Vec2(x - v.x, y - v.y); }
    Vec2 operator*(float s) const { return Vec2(x * s, y * s); }
    Vec2 operator/(float s) const { return Vec2(x / s, y / s); }
    Vec2 operator*(const Vec2& v) const { return Vec2(x * v.x, y * v.y); }
    Vec2 operator/(const Vec2& v) const { return Vec2(x / v.x, y / v.y); }
    Vec2 operator-() const { return Vec2(-x, -y); }

    // 複合代入演算子
    Vec2& operator+=(const Vec2& v) { x += v.x; y += v.y; return *this; }
    Vec2& operator-=(const Vec2& v) { x -= v.x; y -= v.y; return *this; }
    Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    Vec2& operator/=(float s) { x /= s; y /= s; return *this; }
    Vec2& operator*=(const Vec2& v) { x *= v.x; y *= v.y; return *this; }
    Vec2& operator/=(const Vec2& v) { x /= v.x; y /= v.y; return *this; }

    // 比較演算子
    bool operator==(const Vec2& v) const { return x == v.x && y == v.y; }
    bool operator!=(const Vec2& v) const { return !(*this == v); }

    // 長さ
    float length() const { return std::sqrt(x * x + y * y); }
    float lengthSquared() const { return x * x + y * y; }

    // 正規化
    Vec2 normalized() const {
        float len = length();
        if (len > 0) return *this / len;
        return Vec2();
    }
    Vec2& normalize() {
        float len = length();
        if (len > 0) { x /= len; y /= len; }
        return *this;
    }

    // 制限
    Vec2& limit(float max) {
        float lenSq = lengthSquared();
        if (lenSq > max * max) {
            *this = normalized() * max;
        }
        return *this;
    }

    // 内積
    float dot(const Vec2& v) const { return x * v.x + y * v.y; }

    // 外積（2Dでは z 成分のスカラー値を返す）
    float cross(const Vec2& v) const { return x * v.y - y * v.x; }

    // 距離
    float distance(const Vec2& v) const { return (*this - v).length(); }
    float distanceSquared(const Vec2& v) const { return (*this - v).lengthSquared(); }

    // 角度（ラジアン、x軸正方向から反時計回り）
    float angle() const { return std::atan2(y, x); }
    float angle(const Vec2& v) const { return std::atan2(cross(v), dot(v)); }

    // 回転
    Vec2 rotated(float radians) const {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Vec2(x * c - y * s, x * s + y * c);
    }
    Vec2& rotate(float radians) {
        *this = rotated(radians);
        return *this;
    }

    // 線形補間
    Vec2 lerp(const Vec2& v, float t) const {
        return *this + (v - *this) * t;
    }

    // 垂直ベクトル
    Vec2 perpendicular() const { return Vec2(-y, x); }

    // 反射
    Vec2 reflected(const Vec2& normal) const {
        return *this - normal * 2.0f * dot(normal);
    }

    // 静的メソッド
    static Vec2 fromAngle(float radians, float length = 1.0f) {
        return Vec2(std::cos(radians) * length, std::sin(radians) * length);
    }
};

// スカラー * ベクトル
inline Vec2 operator*(float s, const Vec2& v) { return v * s; }

// =============================================================================
// Vec3 - 3次元ベクトル
// =============================================================================

struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    // コンストラクタ
    Vec3() = default;
    Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    Vec3(float v) : x(v), y(v), z(v) {}
    Vec3(const Vec2& v, float z_ = 0.0f) : x(v.x), y(v.y), z(z_) {}
    Vec3(const Vec3&) = default;
    Vec3& operator=(const Vec3&) = default;

    // 配列アクセス
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    // 算術演算子
    Vec3 operator+(const Vec3& v) const { return Vec3(x + v.x, y + v.y, z + v.z); }
    Vec3 operator-(const Vec3& v) const { return Vec3(x - v.x, y - v.y, z - v.z); }
    Vec3 operator*(float s) const { return Vec3(x * s, y * s, z * s); }
    Vec3 operator/(float s) const { return Vec3(x / s, y / s, z / s); }
    Vec3 operator*(const Vec3& v) const { return Vec3(x * v.x, y * v.y, z * v.z); }
    Vec3 operator/(const Vec3& v) const { return Vec3(x / v.x, y / v.y, z / v.z); }
    Vec3 operator-() const { return Vec3(-x, -y, -z); }

    // 複合代入演算子
    Vec3& operator+=(const Vec3& v) { x += v.x; y += v.y; z += v.z; return *this; }
    Vec3& operator-=(const Vec3& v) { x -= v.x; y -= v.y; z -= v.z; return *this; }
    Vec3& operator*=(float s) { x *= s; y *= s; z *= s; return *this; }
    Vec3& operator/=(float s) { x /= s; y /= s; z /= s; return *this; }
    Vec3& operator*=(const Vec3& v) { x *= v.x; y *= v.y; z *= v.z; return *this; }
    Vec3& operator/=(const Vec3& v) { x /= v.x; y /= v.y; z /= v.z; return *this; }

    // 比較演算子
    bool operator==(const Vec3& v) const { return x == v.x && y == v.y && z == v.z; }
    bool operator!=(const Vec3& v) const { return !(*this == v); }

    // 長さ
    float length() const { return std::sqrt(x * x + y * y + z * z); }
    float lengthSquared() const { return x * x + y * y + z * z; }

    // 正規化
    Vec3 normalized() const {
        float len = length();
        if (len > 0) return *this / len;
        return Vec3();
    }
    Vec3& normalize() {
        float len = length();
        if (len > 0) { x /= len; y /= len; z /= len; }
        return *this;
    }

    // 制限
    Vec3& limit(float max) {
        float lenSq = lengthSquared();
        if (lenSq > max * max) {
            *this = normalized() * max;
        }
        return *this;
    }

    // 内積
    float dot(const Vec3& v) const { return x * v.x + y * v.y + z * v.z; }

    // 外積
    Vec3 cross(const Vec3& v) const {
        return Vec3(
            y * v.z - z * v.y,
            z * v.x - x * v.z,
            x * v.y - y * v.x
        );
    }

    // 距離
    float distance(const Vec3& v) const { return (*this - v).length(); }
    float distanceSquared(const Vec3& v) const { return (*this - v).lengthSquared(); }

    // 線形補間
    Vec3 lerp(const Vec3& v, float t) const {
        return *this + (v - *this) * t;
    }

    // 反射
    Vec3 reflected(const Vec3& normal) const {
        return *this - normal * 2.0f * dot(normal);
    }

    // Vec2 への変換
    Vec2 xy() const { return Vec2(x, y); }
};

// スカラー * ベクトル
inline Vec3 operator*(float s, const Vec3& v) { return v * s; }

// =============================================================================
// Vec4 - 4次元ベクトル（同次座標、色など）
// =============================================================================

struct Vec4 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 0.0f;

    // コンストラクタ
    Vec4() = default;
    Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    Vec4(float v) : x(v), y(v), z(v), w(v) {}
    Vec4(const Vec3& v, float w_ = 1.0f) : x(v.x), y(v.y), z(v.z), w(w_) {}
    Vec4(const Vec2& v, float z_ = 0.0f, float w_ = 1.0f) : x(v.x), y(v.y), z(z_), w(w_) {}
    Vec4(const Vec4&) = default;
    Vec4& operator=(const Vec4&) = default;

    // 配列アクセス
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }

    // 算術演算子
    Vec4 operator+(const Vec4& v) const { return Vec4(x + v.x, y + v.y, z + v.z, w + v.w); }
    Vec4 operator-(const Vec4& v) const { return Vec4(x - v.x, y - v.y, z - v.z, w - v.w); }
    Vec4 operator*(float s) const { return Vec4(x * s, y * s, z * s, w * s); }
    Vec4 operator/(float s) const { return Vec4(x / s, y / s, z / s, w / s); }
    Vec4 operator-() const { return Vec4(-x, -y, -z, -w); }

    // 複合代入演算子
    Vec4& operator+=(const Vec4& v) { x += v.x; y += v.y; z += v.z; w += v.w; return *this; }
    Vec4& operator-=(const Vec4& v) { x -= v.x; y -= v.y; z -= v.z; w -= v.w; return *this; }
    Vec4& operator*=(float s) { x *= s; y *= s; z *= s; w *= s; return *this; }
    Vec4& operator/=(float s) { x /= s; y /= s; z /= s; w /= s; return *this; }

    // 比較演算子
    bool operator==(const Vec4& v) const { return x == v.x && y == v.y && z == v.z && w == v.w; }
    bool operator!=(const Vec4& v) const { return !(*this == v); }

    // 長さ
    float length() const { return std::sqrt(x * x + y * y + z * z + w * w); }
    float lengthSquared() const { return x * x + y * y + z * z + w * w; }

    // 正規化
    Vec4 normalized() const {
        float len = length();
        if (len > 0) return *this / len;
        return Vec4();
    }
    Vec4& normalize() {
        float len = length();
        if (len > 0) { x /= len; y /= len; z /= len; w /= len; }
        return *this;
    }

    // 内積
    float dot(const Vec4& v) const { return x * v.x + y * v.y + z * v.z + w * v.w; }

    // 線形補間
    Vec4 lerp(const Vec4& v, float t) const {
        return *this + (v - *this) * t;
    }

    // 部分ベクトルへの変換
    Vec2 xy() const { return Vec2(x, y); }
    Vec3 xyz() const { return Vec3(x, y, z); }
};

// スカラー * ベクトル
inline Vec4 operator*(float s, const Vec4& v) { return v * s; }

// =============================================================================
// Mat3 - 3x3 行列（2D変換用）
// =============================================================================

struct Mat3 {
    float m[9] = {
        1, 0, 0,
        0, 1, 0,
        0, 0, 1
    };

    // コンストラクタ
    Mat3() = default;
    Mat3(float m00, float m01, float m02,
         float m10, float m11, float m12,
         float m20, float m21, float m22) {
        m[0] = m00; m[1] = m01; m[2] = m02;
        m[3] = m10; m[4] = m11; m[5] = m12;
        m[6] = m20; m[7] = m21; m[8] = m22;
    }
    Mat3(const Mat3&) = default;
    Mat3& operator=(const Mat3&) = default;

    // 配列アクセス (row, col)
    float& at(int row, int col) { return m[row * 3 + col]; }
    const float& at(int row, int col) const { return m[row * 3 + col]; }

    // 単位行列
    static Mat3 identity() { return Mat3(); }

    // 平行移動
    static Mat3 translate(float tx, float ty) {
        return Mat3(
            1, 0, tx,
            0, 1, ty,
            0, 0, 1
        );
    }
    static Mat3 translate(const Vec2& t) { return translate(t.x, t.y); }

    // 回転（ラジアン）
    static Mat3 rotate(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Mat3(
            c, -s, 0,
            s,  c, 0,
            0,  0, 1
        );
    }

    // スケール
    static Mat3 scale(float sx, float sy) {
        return Mat3(
            sx, 0,  0,
            0,  sy, 0,
            0,  0,  1
        );
    }
    static Mat3 scale(float s) { return scale(s, s); }
    static Mat3 scale(const Vec2& s) { return scale(s.x, s.y); }

    // 行列の乗算
    Mat3 operator*(const Mat3& other) const {
        Mat3 result;
        for (int row = 0; row < 3; row++) {
            for (int col = 0; col < 3; col++) {
                result.at(row, col) = 0;
                for (int k = 0; k < 3; k++) {
                    result.at(row, col) += at(row, k) * other.at(k, col);
                }
            }
        }
        return result;
    }

    // ベクトルの変換
    Vec2 operator*(const Vec2& v) const {
        float w = m[6] * v.x + m[7] * v.y + m[8];
        return Vec2(
            (m[0] * v.x + m[1] * v.y + m[2]) / w,
            (m[3] * v.x + m[4] * v.y + m[5]) / w
        );
    }

    Vec3 operator*(const Vec3& v) const {
        return Vec3(
            m[0] * v.x + m[1] * v.y + m[2] * v.z,
            m[3] * v.x + m[4] * v.y + m[5] * v.z,
            m[6] * v.x + m[7] * v.y + m[8] * v.z
        );
    }

    // 転置
    Mat3 transposed() const {
        return Mat3(
            m[0], m[3], m[6],
            m[1], m[4], m[7],
            m[2], m[5], m[8]
        );
    }

    // 行列式
    float determinant() const {
        return m[0] * (m[4] * m[8] - m[5] * m[7])
             - m[1] * (m[3] * m[8] - m[5] * m[6])
             + m[2] * (m[3] * m[7] - m[4] * m[6]);
    }

    // 逆行列
    Mat3 inverted() const {
        float det = determinant();
        if (std::abs(det) < 1e-10f) return Mat3();

        float invDet = 1.0f / det;
        return Mat3(
            (m[4] * m[8] - m[5] * m[7]) * invDet,
            (m[2] * m[7] - m[1] * m[8]) * invDet,
            (m[1] * m[5] - m[2] * m[4]) * invDet,
            (m[5] * m[6] - m[3] * m[8]) * invDet,
            (m[0] * m[8] - m[2] * m[6]) * invDet,
            (m[2] * m[3] - m[0] * m[5]) * invDet,
            (m[3] * m[7] - m[4] * m[6]) * invDet,
            (m[1] * m[6] - m[0] * m[7]) * invDet,
            (m[0] * m[4] - m[1] * m[3]) * invDet
        );
    }
};

// =============================================================================
// Mat4 - 4x4 行列（3D変換用）
// =============================================================================

struct Mat4 {
    float m[16] = {
        1, 0, 0, 0,
        0, 1, 0, 0,
        0, 0, 1, 0,
        0, 0, 0, 1
    };

    // コンストラクタ
    Mat4() = default;
    Mat4(float m00, float m01, float m02, float m03,
         float m10, float m11, float m12, float m13,
         float m20, float m21, float m22, float m23,
         float m30, float m31, float m32, float m33) {
        m[0]  = m00; m[1]  = m01; m[2]  = m02; m[3]  = m03;
        m[4]  = m10; m[5]  = m11; m[6]  = m12; m[7]  = m13;
        m[8]  = m20; m[9]  = m21; m[10] = m22; m[11] = m23;
        m[12] = m30; m[13] = m31; m[14] = m32; m[15] = m33;
    }
    Mat4(const Mat4&) = default;
    Mat4& operator=(const Mat4&) = default;

    // 配列アクセス (row, col)
    float& at(int row, int col) { return m[row * 4 + col]; }
    const float& at(int row, int col) const { return m[row * 4 + col]; }

    // 単位行列
    static Mat4 identity() { return Mat4(); }

    // 平行移動
    static Mat4 translate(float tx, float ty, float tz) {
        return Mat4(
            1, 0, 0, tx,
            0, 1, 0, ty,
            0, 0, 1, tz,
            0, 0, 0, 1
        );
    }
    static Mat4 translate(const Vec3& t) { return translate(t.x, t.y, t.z); }

    // X軸回転
    static Mat4 rotateX(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Mat4(
            1, 0,  0, 0,
            0, c, -s, 0,
            0, s,  c, 0,
            0, 0,  0, 1
        );
    }

    // Y軸回転
    static Mat4 rotateY(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Mat4(
             c, 0, s, 0,
             0, 1, 0, 0,
            -s, 0, c, 0,
             0, 0, 0, 1
        );
    }

    // Z軸回転
    static Mat4 rotateZ(float radians) {
        float c = std::cos(radians);
        float s = std::sin(radians);
        return Mat4(
            c, -s, 0, 0,
            s,  c, 0, 0,
            0,  0, 1, 0,
            0,  0, 0, 1
        );
    }

    // 任意軸回転
    static Mat4 rotate(float radians, const Vec3& axis) {
        Vec3 a = axis.normalized();
        float c = std::cos(radians);
        float s = std::sin(radians);
        float t = 1.0f - c;
        return Mat4(
            t * a.x * a.x + c,       t * a.x * a.y - s * a.z, t * a.x * a.z + s * a.y, 0,
            t * a.x * a.y + s * a.z, t * a.y * a.y + c,       t * a.y * a.z - s * a.x, 0,
            t * a.x * a.z - s * a.y, t * a.y * a.z + s * a.x, t * a.z * a.z + c,       0,
            0, 0, 0, 1
        );
    }

    // スケール
    static Mat4 scale(float sx, float sy, float sz) {
        return Mat4(
            sx, 0,  0,  0,
            0,  sy, 0,  0,
            0,  0,  sz, 0,
            0,  0,  0,  1
        );
    }
    static Mat4 scale(float s) { return scale(s, s, s); }
    static Mat4 scale(const Vec3& s) { return scale(s.x, s.y, s.z); }

    // 行列の乗算
    Mat4 operator*(const Mat4& other) const {
        Mat4 result;
        for (int row = 0; row < 4; row++) {
            for (int col = 0; col < 4; col++) {
                result.at(row, col) = 0;
                for (int k = 0; k < 4; k++) {
                    result.at(row, col) += at(row, k) * other.at(k, col);
                }
            }
        }
        return result;
    }

    // ベクトルの変換
    Vec3 operator*(const Vec3& v) const {
        float w = m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15];
        return Vec3(
            (m[0] * v.x + m[1] * v.y + m[2]  * v.z + m[3])  / w,
            (m[4] * v.x + m[5] * v.y + m[6]  * v.z + m[7])  / w,
            (m[8] * v.x + m[9] * v.y + m[10] * v.z + m[11]) / w
        );
    }

    Vec4 operator*(const Vec4& v) const {
        return Vec4(
            m[0]  * v.x + m[1]  * v.y + m[2]  * v.z + m[3]  * v.w,
            m[4]  * v.x + m[5]  * v.y + m[6]  * v.z + m[7]  * v.w,
            m[8]  * v.x + m[9]  * v.y + m[10] * v.z + m[11] * v.w,
            m[12] * v.x + m[13] * v.y + m[14] * v.z + m[15] * v.w
        );
    }

    // 転置
    Mat4 transposed() const {
        return Mat4(
            m[0], m[4], m[8],  m[12],
            m[1], m[5], m[9],  m[13],
            m[2], m[6], m[10], m[14],
            m[3], m[7], m[11], m[15]
        );
    }

    // 逆行列（簡易版、アフィン変換用）
    Mat4 inverted() const {
        Mat4 inv;

        inv.m[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15]
                 + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv.m[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15]
                 - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv.m[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15]
                 + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv.m[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11]
                 - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];

        inv.m[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15]
                 - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv.m[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15]
                 + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv.m[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15]
                 - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv.m[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11]
                 + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];

        inv.m[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15]
                 + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv.m[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15]
                 - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv.m[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15]
                  + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv.m[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11]
                  - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];

        inv.m[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14]
                  - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv.m[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14]
                  + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv.m[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14]
                  - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv.m[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10]
                  + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        float det = m[0] * inv.m[0] + m[1] * inv.m[4] + m[2] * inv.m[8] + m[3] * inv.m[12];
        if (std::abs(det) < 1e-10f) return Mat4();

        float invDet = 1.0f / det;
        for (int i = 0; i < 16; i++) {
            inv.m[i] *= invDet;
        }
        return inv;
    }

    // 視点変換（lookAt）
    static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
        Vec3 f = (target - eye).normalized();
        Vec3 s = f.cross(up).normalized();
        Vec3 u = s.cross(f);
        return Mat4(
            s.x, s.y, s.z, -s.dot(eye),
            u.x, u.y, u.z, -u.dot(eye),
            -f.x, -f.y, -f.z, f.dot(eye),
            0, 0, 0, 1
        );
    }

    // 正射影
    static Mat4 ortho(float left, float right, float bottom, float top, float nearPlane, float farPlane) {
        return Mat4(
            2.0f / (right - left), 0, 0, -(right + left) / (right - left),
            0, 2.0f / (top - bottom), 0, -(top + bottom) / (top - bottom),
            0, 0, -2.0f / (farPlane - nearPlane), -(farPlane + nearPlane) / (farPlane - nearPlane),
            0, 0, 0, 1
        );
    }

    // 透視投影
    static Mat4 perspective(float fovY, float aspect, float nearPlane, float farPlane) {
        float tanHalfFov = std::tan(fovY / 2.0f);
        return Mat4(
            1.0f / (aspect * tanHalfFov), 0, 0, 0,
            0, 1.0f / tanHalfFov, 0, 0,
            0, 0, -(farPlane + nearPlane) / (farPlane - nearPlane), -2.0f * farPlane * nearPlane / (farPlane - nearPlane),
            0, 0, -1, 0
        );
    }
};

// =============================================================================
// ユーティリティ関数
// =============================================================================

// 度数法 → ラジアン
inline float radians(float degrees) { return degrees * TAU / 360.0f; }

// ラジアン → 度数法
inline float degrees(float radians) { return radians * 360.0f / TAU; }

// 線形補間
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// クランプ
inline float clamp(float value, float min, float max) {
    return std::max(min, std::min(max, value));
}

// マップ
inline float map(float value, float inMin, float inMax, float outMin, float outMax) {
    return outMin + (outMax - outMin) * ((value - inMin) / (inMax - inMin));
}

// 符号
inline float sign(float value) { return (value > 0) - (value < 0); }

// 小数部分
inline float fract(float value) { return value - std::floor(value); }

// 最小/最大
template<typename T>
T min(T a, T b) { return (a < b) ? a : b; }

template<typename T>
T max(T a, T b) { return (a > b) ? a : b; }

// 絶対値
inline float abs(float value) { return std::abs(value); }

// =============================================================================
// 乱数
// =============================================================================

namespace internal {
    // スレッドローカルな乱数生成器
    inline std::mt19937& getRandomEngine() {
        static thread_local std::mt19937 engine(std::random_device{}());
        return engine;
    }
}

// 0.0 ~ 1.0 の乱数
inline float random() {
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    return dist(internal::getRandomEngine());
}

// 0.0 ~ max の乱数
inline float random(float max) {
    std::uniform_real_distribution<float> dist(0.0f, max);
    return dist(internal::getRandomEngine());
}

// min ~ max の乱数
inline float random(float min, float max) {
    std::uniform_real_distribution<float> dist(min, max);
    return dist(internal::getRandomEngine());
}

// 整数版: 0 ~ max-1 の乱数
inline int randomInt(int max) {
    std::uniform_int_distribution<int> dist(0, max - 1);
    return dist(internal::getRandomEngine());
}

// 整数版: min ~ max の乱数（両端含む）
inline int randomInt(int min, int max) {
    std::uniform_int_distribution<int> dist(min, max);
    return dist(internal::getRandomEngine());
}

// シードを設定
inline void randomSeed(unsigned int seed) {
    internal::getRandomEngine().seed(seed);
}

} // namespace trussc
