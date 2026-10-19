// Aurora3D — math.h
// Portable vector / matrix / quaternion math. No platform dependencies, so this
// header (and its test) build and run on any host, including the Linux CI box.
//
// Conventions (kept identical to the HLSL side, see src/shaders/aurora_default.hlsl):
//   * right-handed coordinate system
//   * depth range [0, 1]
//   * column-vector convention: transform is  v' = M * v
//   * matrices are stored COLUMN-MAJOR: element (row, col) lives at m[col*4 + row]
//     This is what HLSL expects for a default `float4x4` in a cbuffer, so matrices
//     can be memcpy'd straight into a constant buffer.
#pragma once

#include <cmath>
#include <cstdint>

namespace a3d {

inline constexpr float PI  = 3.14159265358979323846f;
inline constexpr float TAU = 6.28318530717958647692f;
inline constexpr float DEG2RAD = PI / 180.0f;
inline constexpr float RAD2DEG = 180.0f / PI;

// ---------------------------------------------------------------- scalars ----
inline float Radians(float degrees) { return degrees * DEG2RAD; }
inline float Degrees(float radians) { return radians * RAD2DEG; }
inline float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float Lerp(float a, float b, float t) { return a + (b - a) * t; }
inline float Min(float a, float b) { return a < b ? a : b; }
inline float Max(float a, float b) { return a > b ? a : b; }
inline float Abs(float v) { return v < 0.0f ? -v : v; }

// ------------------------------------------------------------------ Vec2 -----
struct Vec2 {
    float x = 0.0f, y = 0.0f;
    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}
    constexpr explicit Vec2(float s) : x(s), y(s) {}
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
};

inline constexpr Vec2 operator+(Vec2 a, Vec2 b) { return {a.x + b.x, a.y + b.y}; }
inline constexpr Vec2 operator-(Vec2 a, Vec2 b) { return {a.x - b.x, a.y - b.y}; }
inline constexpr Vec2 operator-(Vec2 a) { return {-a.x, -a.y}; }
inline constexpr Vec2 operator*(Vec2 a, float s) { return {a.x * s, a.y * s}; }
inline constexpr Vec2 operator*(float s, Vec2 a) { return {a.x * s, a.y * s}; }
inline constexpr Vec2 operator/(Vec2 a, float s) { return {a.x / s, a.y / s}; }
inline Vec2& operator+=(Vec2& a, Vec2 b) { a = a + b; return a; }
inline Vec2& operator-=(Vec2& a, Vec2 b) { a = a - b; return a; }
inline Vec2& operator*=(Vec2& a, float s) { a = a * s; return a; }

inline float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
inline float Length(Vec2 v) { return std::sqrt(Dot(v, v)); }
inline float LengthSq(Vec2 v) { return Dot(v, v); }
inline Vec2 Normalize(Vec2 v) { float l = Length(v); return l > 1e-8f ? v / l : Vec2{}; }
inline Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
inline constexpr Vec2 Perp(Vec2 v) { return {-v.y, v.x}; }

// ------------------------------------------------------------------ Vec3 -----
struct Vec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}
    constexpr explicit Vec3(float s) : x(s), y(s), z(s) {}
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
};

inline constexpr Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline constexpr Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline constexpr Vec3 operator-(Vec3 a) { return {-a.x, -a.y, -a.z}; }
inline constexpr Vec3 operator*(Vec3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline constexpr Vec3 operator*(float s, Vec3 a) { return {a.x * s, a.y * s, a.z * s}; }
inline constexpr Vec3 operator*(Vec3 a, Vec3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline constexpr Vec3 operator/(Vec3 a, float s) { return {a.x / s, a.y / s, a.z / s}; }
inline Vec3& operator+=(Vec3& a, Vec3 b) { a = a + b; return a; }
inline Vec3& operator-=(Vec3& a, Vec3 b) { a = a - b; return a; }
inline Vec3& operator*=(Vec3& a, float s) { a = a * s; return a; }

inline float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline constexpr Vec3 Cross(Vec3 a, Vec3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Length(Vec3 v) { return std::sqrt(Dot(v, v)); }
inline float LengthSq(Vec3 v) { return Dot(v, v); }
inline float Distance(Vec3 a, Vec3 b) { return Length(b - a); }
inline Vec3 Normalize(Vec3 v) { float l = Length(v); return l > 1e-8f ? v / l : Vec3{}; }
inline Vec3 Lerp(Vec3 a, Vec3 b, float t) { return a + (b - a) * t; }
inline Vec3 Reflect(Vec3 v, Vec3 n) { return v - n * (2.0f * Dot(v, n)); }
inline Vec3 Min(Vec3 a, Vec3 b) { return {Min(a.x, b.x), Min(a.y, b.y), Min(a.z, b.z)}; }
inline Vec3 Max(Vec3 a, Vec3 b) { return {Max(a.x, b.x), Max(a.y, b.y), Max(a.z, b.z)}; }

inline constexpr Vec3 kAxisX{1, 0, 0};
inline constexpr Vec3 kAxisY{0, 1, 0};
inline constexpr Vec3 kAxisZ{0, 0, 1};

// ------------------------------------------------------------------ Vec4 -----
struct Vec4 {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 0.0f;
    constexpr Vec4() = default;
    constexpr Vec4(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
    constexpr Vec4(Vec3 v, float w_) : x(v.x), y(v.y), z(v.z), w(w_) {}
    constexpr explicit Vec4(float s) : x(s), y(s), z(s), w(s) {}
    constexpr Vec3 xyz() const { return {x, y, z}; }
    float& operator[](int i) { return (&x)[i]; }
    const float& operator[](int i) const { return (&x)[i]; }
};

inline constexpr Vec4 operator+(Vec4 a, Vec4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
inline constexpr Vec4 operator-(Vec4 a, Vec4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
inline constexpr Vec4 operator*(Vec4 a, float s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
inline float Dot(Vec4 a, Vec4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Vec4 Lerp(Vec4 a, Vec4 b, float t) { return a + (b - a) * t; }

// ------------------------------------------------------------------ Mat4 -----
// Column-major storage: m[col*4 + row].
struct Mat4 {
    float m[16] = {1, 0, 0, 0,
                   0, 1, 0, 0,
                   0, 0, 1, 0,
                   0, 0, 0, 1};

    float& At(int row, int col) { return m[col * 4 + row]; }
    float At(int row, int col) const { return m[col * 4 + row]; }

    Vec4 Column(int c) const { return {m[c * 4], m[c * 4 + 1], m[c * 4 + 2], m[c * 4 + 3]}; }
    Vec3 Row3(int r) const { return {m[r], m[4 + r], m[8 + r]}; }

    static Mat4 Identity() { return Mat4{}; }

    static Mat4 Zero() {
        Mat4 o;
        for (int i = 0; i < 16; ++i) o.m[i] = 0.0f;
        return o;
    }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    // Column-vector convention: result = a * b (b is applied first).
    Mat4 o = Mat4::Zero();
    for (int c = 0; c < 4; ++c) {
        Vec4 bc = b.Column(c);
        for (int r = 0; r < 4; ++r) {
            o.At(r, c) = a.At(r, 0) * bc.x + a.At(r, 1) * bc.y +
                         a.At(r, 2) * bc.z + a.At(r, 3) * bc.w;
        }
    }
    return o;
}

inline Vec4 operator*(const Mat4& a, Vec4 v) {
    return {a.At(0, 0) * v.x + a.At(0, 1) * v.y + a.At(0, 2) * v.z + a.At(0, 3) * v.w,
            a.At(1, 0) * v.x + a.At(1, 1) * v.y + a.At(1, 2) * v.z + a.At(1, 3) * v.w,
            a.At(2, 0) * v.x + a.At(2, 1) * v.y + a.At(2, 2) * v.z + a.At(2, 3) * v.w,
            a.At(3, 0) * v.x + a.At(3, 1) * v.y + a.At(3, 2) * v.z + a.At(3, 3) * v.w};
}

inline Vec3 TransformPoint(const Mat4& a, Vec3 p) {
    Vec4 r = a * Vec4(p, 1.0f);
    return {r.x, r.y, r.z};
}

inline Vec3 TransformDirection(const Mat4& a, Vec3 d) {
    Vec4 r = a * Vec4(d, 0.0f);
    return {r.x, r.y, r.z};
}

inline Mat4 Translation(Vec3 t) {
    Mat4 o;
    o.At(0, 3) = t.x;
    o.At(1, 3) = t.y;
    o.At(2, 3) = t.z;
    return o;
}

inline Mat4 Scale(Vec3 s) {
    Mat4 o;
    o.At(0, 0) = s.x;
    o.At(1, 1) = s.y;
    o.At(2, 2) = s.z;
    return o;
}

inline Mat4 Scale(float s) { return Scale(Vec3(s, s, s)); }

inline Mat4 RotationX(float radians) {
    float c = std::cos(radians), s = std::sin(radians);
    Mat4 o;
    o.At(1, 1) = c;  o.At(1, 2) = -s;
    o.At(2, 1) = s;  o.At(2, 2) = c;
    return o;
}

inline Mat4 RotationY(float radians) {
    float c = std::cos(radians), s = std::sin(radians);
    Mat4 o;
    o.At(0, 0) = c;  o.At(0, 2) = s;
    o.At(2, 0) = -s; o.At(2, 2) = c;
    return o;
}

inline Mat4 RotationZ(float radians) {
    float c = std::cos(radians), s = std::sin(radians);
    Mat4 o;
    o.At(0, 0) = c;  o.At(0, 1) = -s;
    o.At(1, 0) = s;  o.At(1, 1) = c;
    return o;
}

// Rotation about an arbitrary (assumed non-degenerate) axis — Rodrigues' formula.
inline Mat4 RotationAxis(Vec3 axis, float radians) {
    Vec3 a = Normalize(axis);
    float c = std::cos(radians), s = std::sin(radians), t = 1.0f - c;
    Mat4 o;
    o.At(0, 0) = t * a.x * a.x + c;
    o.At(0, 1) = t * a.x * a.y - s * a.z;
    o.At(0, 2) = t * a.x * a.z + s * a.y;
    o.At(1, 0) = t * a.x * a.y + s * a.z;
    o.At(1, 1) = t * a.y * a.y + c;
    o.At(1, 2) = t * a.y * a.z - s * a.x;
    o.At(2, 0) = t * a.x * a.z - s * a.y;
    o.At(2, 1) = t * a.y * a.z + s * a.x;
    o.At(2, 2) = t * a.z * a.z + c;
    return o;
}

// Euler angles applied as Y * X * Z (yaw, then pitch, then roll).
inline Mat4 RotationEuler(Vec3 radiansYXZ) {
    return RotationY(radiansYXZ.y) * RotationX(radiansYXZ.x) * RotationZ(radiansYXZ.z);
}

// Model matrix from position / euler / scale — the common gameplay case.
inline Mat4 Compose(Vec3 position, Vec3 eulerRadians, Vec3 scale) {
    return Translation(position) * RotationEuler(eulerRadians) * Scale(scale);
}

inline Mat4 Transpose(const Mat4& a) {
    Mat4 o;
    for (int r = 0; r < 4; ++r)
        for (int c = 0; c < 4; ++c) o.At(r, c) = a.At(c, r);
    return o;
}

// Right-handed look-at; forward is -Z.
inline Mat4 LookAt(Vec3 eye, Vec3 target, Vec3 up) {
    Vec3 f = Normalize(target - eye);
    Vec3 s = Normalize(Cross(f, up));
    Vec3 u = Cross(s, f);
    Mat4 o;
    o.At(0, 0) = s.x;  o.At(0, 1) = s.y;  o.At(0, 2) = s.z;  o.At(0, 3) = -Dot(s, eye);
    o.At(1, 0) = u.x;  o.At(1, 1) = u.y;  o.At(1, 2) = u.z;  o.At(1, 3) = -Dot(u, eye);
    o.At(2, 0) = -f.x; o.At(2, 1) = -f.y; o.At(2, 2) = -f.z; o.At(2, 3) = Dot(f, eye);
    return o;
}

// Right-handed perspective, depth mapped to [0, 1] (native D3D range).
inline Mat4 Perspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    float tanHalf = std::tan(fovYRadians * 0.5f);
    Mat4 o = Mat4::Zero();
    o.At(0, 0) = 1.0f / (aspect * tanHalf);
    o.At(1, 1) = 1.0f / tanHalf;
    o.At(2, 2) = farZ / (nearZ - farZ);
    o.At(2, 3) = (farZ * nearZ) / (nearZ - farZ);
    o.At(3, 2) = -1.0f;
    return o;
}

inline Mat4 Ortho(float left, float right, float bottom, float top, float nearZ, float farZ) {
    Mat4 o;
    o.At(0, 0) = 2.0f / (right - left);
    o.At(1, 1) = 2.0f / (top - bottom);
    o.At(2, 2) = 1.0f / (nearZ - farZ);
    o.At(0, 3) = (left + right) / (left - right);
    o.At(1, 3) = (top + bottom) / (bottom - top);
    o.At(2, 3) = nearZ / (nearZ - farZ);
    return o;
}

// General 4x4 inverse (cofactor expansion). Returns identity if singular.
inline Mat4 Inverse(const Mat4& a) {
    const float* mm = a.m;
    float inv[16];

    inv[0]  =  mm[5]*mm[10]*mm[15] - mm[5]*mm[11]*mm[14] - mm[9]*mm[6]*mm[15] + mm[9]*mm[7]*mm[14] + mm[13]*mm[6]*mm[11] - mm[13]*mm[7]*mm[10];
    inv[4]  = -mm[4]*mm[10]*mm[15] + mm[4]*mm[11]*mm[14] + mm[8]*mm[6]*mm[15] - mm[8]*mm[7]*mm[14] - mm[12]*mm[6]*mm[11] + mm[12]*mm[7]*mm[10];
    inv[8]  =  mm[4]*mm[9]*mm[15]  - mm[4]*mm[11]*mm[13] - mm[8]*mm[5]*mm[15] + mm[8]*mm[7]*mm[13] + mm[12]*mm[5]*mm[11] - mm[12]*mm[7]*mm[9];
    inv[12] = -mm[4]*mm[9]*mm[14]  + mm[4]*mm[10]*mm[13] + mm[8]*mm[5]*mm[14] - mm[8]*mm[6]*mm[13] - mm[12]*mm[5]*mm[10] + mm[12]*mm[6]*mm[9];
    inv[1]  = -mm[1]*mm[10]*mm[15] + mm[1]*mm[11]*mm[14] + mm[9]*mm[2]*mm[15] - mm[9]*mm[3]*mm[14] - mm[13]*mm[2]*mm[11] + mm[13]*mm[3]*mm[10];
    inv[5]  =  mm[0]*mm[10]*mm[15] - mm[0]*mm[11]*mm[14] - mm[8]*mm[2]*mm[15] + mm[8]*mm[3]*mm[14] + mm[12]*mm[2]*mm[11] - mm[12]*mm[3]*mm[10];
    inv[9]  = -mm[0]*mm[9]*mm[15]  + mm[0]*mm[11]*mm[13] + mm[8]*mm[1]*mm[15] - mm[8]*mm[3]*mm[13] - mm[12]*mm[1]*mm[11] + mm[12]*mm[3]*mm[9];
    inv[13] =  mm[0]*mm[9]*mm[14]  - mm[0]*mm[10]*mm[13] - mm[8]*mm[1]*mm[14] + mm[8]*mm[2]*mm[13] + mm[12]*mm[1]*mm[10] - mm[12]*mm[2]*mm[9];
    inv[2]  =  mm[1]*mm[6]*mm[15]  - mm[1]*mm[7]*mm[14]  - mm[5]*mm[2]*mm[15] + mm[5]*mm[3]*mm[14] + mm[13]*mm[2]*mm[7]  - mm[13]*mm[3]*mm[6];
    inv[6]  = -mm[0]*mm[6]*mm[15]  + mm[0]*mm[7]*mm[14]  + mm[4]*mm[2]*mm[15] - mm[4]*mm[3]*mm[14] - mm[12]*mm[2]*mm[7]  + mm[12]*mm[3]*mm[6];
    inv[10] =  mm[0]*mm[5]*mm[15]  - mm[0]*mm[7]*mm[13]  - mm[4]*mm[1]*mm[15] + mm[4]*mm[3]*mm[13] + mm[12]*mm[1]*mm[7]  - mm[12]*mm[3]*mm[5];
    inv[14] = -mm[0]*mm[5]*mm[14]  + mm[0]*mm[6]*mm[13]  + mm[4]*mm[1]*mm[14] - mm[4]*mm[2]*mm[13] - mm[12]*mm[1]*mm[6]  + mm[12]*mm[2]*mm[5];
    inv[3]  = -mm[1]*mm[6]*mm[11]  + mm[1]*mm[7]*mm[10]  + mm[5]*mm[2]*mm[11] - mm[5]*mm[3]*mm[10] - mm[9]*mm[2]*mm[7]   + mm[9]*mm[3]*mm[6];
    inv[7]  =  mm[0]*mm[6]*mm[11]  - mm[0]*mm[7]*mm[10]  - mm[4]*mm[2]*mm[11] + mm[4]*mm[3]*mm[10] + mm[8]*mm[2]*mm[7]   - mm[8]*mm[3]*mm[6];
    inv[11] = -mm[0]*mm[5]*mm[11]  + mm[0]*mm[7]*mm[9]   + mm[4]*mm[1]*mm[11] - mm[4]*mm[3]*mm[9]  - mm[8]*mm[1]*mm[7]   + mm[8]*mm[3]*mm[5];
    inv[15] =  mm[0]*mm[5]*mm[10]  - mm[0]*mm[6]*mm[9]   - mm[4]*mm[1]*mm[10] + mm[4]*mm[2]*mm[9]  + mm[8]*mm[1]*mm[6]   - mm[8]*mm[2]*mm[5];

    float det = mm[0] * inv[0] + mm[1] * inv[4] + mm[2] * inv[8] + mm[3] * inv[12];
    if (Abs(det) < 1e-12f) return Mat4::Identity();
    det = 1.0f / det;

    Mat4 o;
    for (int i = 0; i < 16; ++i) o.m[i] = inv[i] * det;
    return o;
}

// ---------------------------------------------------------------- Quaternion -
struct Quat {
    float x = 0.0f, y = 0.0f, z = 0.0f, w = 1.0f;
    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}
};

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

inline Quat Normalize(Quat q) {
    float l = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    if (l < 1e-8f) return Quat{};
    return {q.x / l, q.y / l, q.z / l, q.w / l};
}

inline Quat QuatFromAxisAngle(Vec3 axis, float radians) {
    Vec3 a = Normalize(axis);
    float h = radians * 0.5f, s = std::sin(h);
    return {a.x * s, a.y * s, a.z * s, std::cos(h)};
}

inline Quat QuatFromEuler(Vec3 radiansYXZ) {
    return Normalize(QuatFromAxisAngle(kAxisY, radiansYXZ.y) *
                     QuatFromAxisAngle(kAxisX, radiansYXZ.x) *
                     QuatFromAxisAngle(kAxisZ, radiansYXZ.z));
}

inline Quat Slerp(Quat a, Quat b, float t) {
    float d = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
    if (d < 0.0f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) return Normalize(Quat{Lerp(a.x, b.x, t), Lerp(a.y, b.y, t), Lerp(a.z, b.z, t), Lerp(a.w, b.w, t)});
    float theta0 = std::acos(Clamp(d, -1.0f, 1.0f));
    float theta = theta0 * t;
    float sinTheta = std::sin(theta), sinTheta0 = std::sin(theta0);
    float s0 = std::cos(theta) - d * sinTheta / sinTheta0;
    float s1 = sinTheta / sinTheta0;
    return Normalize(Quat{a.x * s0 + b.x * s1, a.y * s0 + b.y * s1,
                          a.z * s0 + b.z * s1, a.w * s0 + b.w * s1});
}

// -------------------------------------------------------------- AABB / Ray ---
struct Bounds {
    Vec3 min{ 1e30f,  1e30f,  1e30f};
    Vec3 max{-1e30f, -1e30f, -1e30f};

    void Grow(Vec3 p) { min = a3d::Min(min, p); max = a3d::Max(max, p); }
    Vec3 Center() const { return (min + max) * 0.5f; }
    Vec3 Extents() const { return (max - min) * 0.5f; }
    float Radius() const { return Length(Extents()); }
    bool Valid() const { return min.x <= max.x; }
};

struct Ray {
    Vec3 origin{};
    Vec3 direction{0, 0, -1};
};

// ------------------------------------------------------------ game helpers ---
// Everything below is plain math with no platform dependency, so it is available
// to any game built on the library and unit-tested on the host.

// Steps `current` toward `target` by at most `maxDelta`. The go-to for "move at a
// fixed speed without overshooting".
inline float MoveTowards(float current, float target, float maxDelta) {
    const float diff = target - current;
    if (Abs(diff) <= maxDelta) return target;
    return current + (diff > 0.0f ? maxDelta : -maxDelta);
}

inline Vec2 MoveTowards(Vec2 current, Vec2 target, float maxDelta) {
    const Vec2 diff = target - current;
    const float dist = Length(diff);
    if (dist <= maxDelta || dist < 1e-8f) return target;
    return current + diff / dist * maxDelta;
}

inline Vec3 MoveTowards(Vec3 current, Vec3 target, float maxDelta) {
    const Vec3 diff = target - current;
    const float dist = Length(diff);
    if (dist <= maxDelta || dist < 1e-8f) return target;
    return current + diff / dist * maxDelta;
}

// Exponential smoothing that is independent of the frame rate: `t` is the
// fraction of the remaining gap to close per second.
inline float Damp(float current, float target, float rate, float dt) {
    return Lerp(current, target, 1.0f - std::exp(-rate * dt));
}
inline Vec3 Damp(Vec3 current, Vec3 target, float rate, float dt) {
    return Lerp(current, target, 1.0f - std::exp(-rate * dt));
}

// Critically-damped spring (Game Programming Gems 4). Writes the smoothed value
// and velocity through the pointers and returns the new value. Use it for camera
// follow and UI motion that must not overshoot.
inline float SmoothDamp(float current, float target, float* velocity, float smoothTime,
                        float dt, float maxSpeed = 1e30f) {
    smoothTime = Max(0.0001f, smoothTime);
    const float omega = 2.0f / smoothTime;
    const float x = omega * dt;
    const float expo = 1.0f / (1.0f + x + 0.48f * x * x + 0.235f * x * x * x);
    float change = current - target;
    const float maxChange = maxSpeed * smoothTime;
    change = Clamp(change, -maxChange, maxChange);
    const float temp = (*velocity + omega * change) * dt;
    *velocity = (*velocity - omega * temp) * expo;
    return target + (change + temp) * expo;
}

inline Vec3 SmoothDamp(Vec3 current, Vec3 target, Vec3* velocity, float smoothTime,
                       float dt, float maxSpeed = 1e30f) {
    return Vec3{SmoothDamp(current.x, target.x, &velocity->x, smoothTime, dt, maxSpeed),
                SmoothDamp(current.y, target.y, &velocity->y, smoothTime, dt, maxSpeed),
                SmoothDamp(current.z, target.z, &velocity->z, smoothTime, dt, maxSpeed)};
}

// Angle helpers. `Radians` is already declared above.
inline float LerpAngle(float a, float b, float t) {
    float delta = std::fmod(b - a, TAU);
    if (delta > PI) delta -= TAU;
    if (delta < -PI) delta += TAU;
    return a + delta * t;
}

// Reflects `v` off a surface with normal `n`. Same as Reflect(), named for the
// physics case so call sites read clearly.
inline Vec3 Bounce(Vec3 v, Vec3 n) { return Reflect(v, n); }

// Rotate a vector around an axis by `radians` (Rodrigues), useful for steering.
inline Vec3 RotateAround(Vec3 v, Vec3 axis, float radians) {
    const Vec3 a = Normalize(axis);
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    return v * c + Cross(a, v) * s + a * Dot(a, v) * (1.0f - c);
}

// Projects `v` onto `onto` (the component of v along onto).
inline Vec3 Project(Vec3 v, Vec3 onto) {
    const float lenSq = LengthSq(onto);
    if (lenSq < 1e-8f) return Vec3{};
    return onto * (Dot(v, onto) / lenSq);
}

// Removes the component of `v` along `onto` (the component perpendicular to it).
inline Vec3 Reject(Vec3 v, Vec3 onto) { return v - Project(v, onto); }

// Rotate `v` by a quaternion.
inline Vec3 Rotate(Quat q, Vec3 v) {
    const Quat p{v.x, v.y, v.z, 0.0f};
    const Quat qc{-q.x, -q.y, -q.z, q.w};
    const Quat r = q * p * qc;
    return Vec3{r.x, r.y, r.z};
}

inline Mat4 QuatToMat4(Quat q) {
    q = Normalize(q);
    const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    Mat4 o;
    o.At(0, 0) = 1.0f - 2.0f * (yy + zz); o.At(0, 1) = 2.0f * (xy - wz);        o.At(0, 2) = 2.0f * (xz + wy);
    o.At(1, 0) = 2.0f * (xy + wz);        o.At(1, 1) = 1.0f - 2.0f * (xx + zz); o.At(1, 2) = 2.0f * (yz - wx);
    o.At(2, 0) = 2.0f * (xz - wy);        o.At(2, 1) = 2.0f * (yz + wx);        o.At(2, 2) = 1.0f - 2.0f * (xx + yy);
    return o;
}

// Euler angles (radians, YXZ) from a rotation matrix.
inline Vec3 ToEuler(const Mat4& m) {
    const float sy = std::sqrt(m.At(0, 0) * m.At(0, 0) + m.At(1, 0) * m.At(1, 0));
    const bool singular = sy < 1e-6f;
    Vec3 e;
    if (!singular) {
        e.x = std::atan2(m.At(2, 1), m.At(2, 2));
        e.y = std::atan2(-m.At(2, 0), sy);
        e.z = std::atan2(m.At(1, 0), m.At(0, 0));
    } else {
        e.x = std::atan2(-m.At(1, 2), m.At(1, 1));
        e.y = std::atan2(-m.At(2, 0), sy);
        e.z = 0.0f;
    }
    return e;
}

// ---------------------------------------------------------------- easing -----
// The usual easing curves. Each takes a normalized t in [0, 1] and returns a
// shaped value, so `Lerp(a, b, EaseOutCubic(t))` is the whole pattern.
inline float EaseInQuad(float t)   { return t * t; }
inline float EaseOutQuad(float t)  { return 1.0f - (1.0f - t) * (1.0f - t); }
inline float EaseInOutQuad(float t){ return t < 0.5f ? 2.0f * t * t : 1.0f - 2.0f * (1.0f - t) * (1.0f - t); }
inline float EaseInCubic(float t)  { return t * t * t; }
inline float EaseOutCubic(float t) { const float u = 1.0f - t; return 1.0f - u * u * u; }
inline float EaseInOutCubic(float t){ return t < 0.5f ? 4.0f * t * t * t : 1.0f - 4.0f * (1.0f - t) * (1.0f - t) * (1.0f - t); }
inline float EaseInSine(float t)   { return 1.0f - std::cos(t * PI * 0.5f); }
inline float EaseOutSine(float t)  { return std::sin(t * PI * 0.5f); }
inline float EaseInOutSine(float t){ return -(std::cos(PI * t) - 1.0f) * 0.5f; }
inline float EaseOutBack(float t) {
    const float c1 = 1.70158f, c3 = c1 + 1.0f, u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}
inline float EaseOutBounce(float t) {
    const float n1 = 7.5625f, d1 = 2.75f;
    if (t < 1.0f / d1) return n1 * t * t;
    if (t < 2.0f / d1) { t -= 1.5f / d1; return n1 * t * t + 0.75f; }
    if (t < 2.5f / d1) { t -= 2.25f / d1; return n1 * t * t + 0.9375f; }
    t -= 2.625f / d1;
    return n1 * t * t + 0.984375f;
}

// A normalized 0->1 ramp that is 0 before `start`, 1 after `start + duration`,
// and eased in between. Great for one-shot animations driven by a clock.
inline float TimedLerp(float time, float start, float duration) {
    if (duration <= 0.0f) return time >= start ? 1.0f : 0.0f;
    return Clamp((time - start) / duration, 0.0f, 1.0f);
}

// -------------------------------------------------------------- rectangle ----
struct Rect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;

    constexpr Rect() = default;
    constexpr Rect(float x_, float y_, float w_, float h_) : x(x_), y(y_), width(w_), height(h_) {}

    Vec2 Position() const { return {x, y}; }
    Vec2 Size() const { return {width, height}; }
    Vec2 Center() const { return {x + width * 0.5f, y + height * 0.5f}; }
    float Left() const { return x; }
    float Right() const { return x + width; }
    float Top() const { return y; }
    float Bottom() const { return y + height; }

    bool Contains(Vec2 p) const {
        return p.x >= x && p.x <= x + width && p.y >= y && p.y <= y + height;
    }
    bool Overlaps(Rect o) const {
        return x < o.x + o.width && x + width > o.x && y < o.y + o.height && y + height > o.y;
    }
};

// Axis-aligned bounding-box test against a ray. `outDistance` receives the
// distance along the ray to the entry point when it hits.
inline bool RayHitsBounds(Ray ray, Bounds bounds, float* outDistance = nullptr) {
    if (!bounds.Valid()) return false;

    float tmin = 0.0f;
    float tmax = 1e30f;
    for (int axis = 0; axis < 3; ++axis) {
        const float origin = ray.origin[axis];
        const float dir = ray.direction[axis];
        const float lo = bounds.min[axis];
        const float hi = bounds.max[axis];
        if (Abs(dir) < 1e-8f) {
            if (origin < lo || origin > hi) return false;
            continue;
        }
        float t1 = (lo - origin) / dir;
        float t2 = (hi - origin) / dir;
        if (t1 > t2) { const float tmp = t1; t1 = t2; t2 = tmp; }
        tmin = Max(tmin, t1);
        tmax = Min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (outDistance) *outDistance = tmin;
    return true;
}

// Closest points between two segments; handy for swept collision. Writes the
// parametric positions along each segment and returns the squared distance.
inline float ClosestPointsBetweenSegments(Vec3 p1, Vec3 q1, Vec3 p2, Vec3 q2,
                                          float* s, float* t) {
    const Vec3 d1 = q1 - p1;
    const Vec3 d2 = q2 - p2;
    const Vec3 r = p1 - p2;
    const float a = Dot(d1, d1);
    const float e = Dot(d2, d2);
    const float f = Dot(d2, r);

    float ss = 0.0f, tt = 0.0f;
    if (a <= 1e-8f && e <= 1e-8f) {
        ss = tt = 0.0f;
    } else if (a <= 1e-8f) {
        tt = Clamp(f / e, 0.0f, 1.0f);
    } else {
        const float c = Dot(d1, r);
        if (e <= 1e-8f) {
            ss = Clamp(-c / a, 0.0f, 1.0f);
        } else {
            const float b = Dot(d1, d2);
            const float denom = a * e - b * b;
            ss = denom > 1e-8f ? Clamp((b * f - c * e) / denom, 0.0f, 1.0f) : 0.0f;
            tt = (b * ss + f) / e;
            if (tt < 0.0f) { tt = 0.0f; ss = Clamp(-c / a, 0.0f, 1.0f); }
            else if (tt > 1.0f) { tt = 1.0f; ss = Clamp((b - c) / a, 0.0f, 1.0f); }
        }
    }

    if (s) *s = ss;
    if (t) *t = tt;
    return LengthSq((p1 + d1 * ss) - (p2 + d2 * tt));
}

// ---------------------------------------------------------------- transform --
// A position / rotation / scale triple with the derived axes a game needs. It is
// the piece that makes "move and rotate a thing" read like English:
//
//   Transform t;
//   t.MoveLocal({0, 0, speed * dt});        // walk forward
//   t.RotateY(45.0f * dt);                  // turn
//   a3d::DrawMeshEx(mesh, t.Matrix(), tint);
struct Transform {
    Vec3 position{};
    Vec3 rotation{};            // euler degrees, applied Y * X * Z
    Vec3 scale{1.0f, 1.0f, 1.0f};

    Mat4 Matrix() const {
        return Compose(position, Vec3{Radians(rotation.x), Radians(rotation.y),
                                      Radians(rotation.z)},
                       scale);
    }

    // Local axes after the rotation is applied. Forward is -Z, matching the
    // engine's right-handed convention and Camera3D.
    Vec3 Forward() const {
        const Mat4 r = RotationEuler(Vec3{Radians(rotation.x), Radians(rotation.y),
                                          Radians(rotation.z)});
        // Column 2 of a rotation matrix is the local +Z axis, so forward is its
        // negation.
        return Normalize(Vec3{-r.At(0, 2), -r.At(1, 2), -r.At(2, 2)});
    }
    Vec3 Right() const {
        const Mat4 r = RotationEuler(Vec3{Radians(rotation.x), Radians(rotation.y),
                                          Radians(rotation.z)});
        return Normalize(Vec3{r.At(0, 0), r.At(1, 0), r.At(2, 0)});
    }
    Vec3 Up() const {
        const Mat4 r = RotationEuler(Vec3{Radians(rotation.x), Radians(rotation.y),
                                          Radians(rotation.z)});
        return Normalize(Vec3{r.At(0, 1), r.At(1, 1), r.At(2, 1)});
    }

    // Movement in world space.
    void Translate(Vec3 delta) { position = position + delta; }
    // Movement along the object's own axes. A positive z moves forward (the
    // direction the object faces).
    void MoveLocal(Vec3 delta) {
        position = position + Right() * delta.x + Up() * delta.y + Forward() * delta.z;
    }
    // Movement along the world axes, ignoring rotation. The usual "walk on the
    // ground" case where turning must not tilt the motion.
    void MoveFlat(Vec3 delta) {
        const Vec3 forward = Normalize(Vec3{-std::sin(Radians(rotation.y)), 0.0f,
                                            -std::cos(Radians(rotation.y))});
        const Vec3 right{std::cos(Radians(rotation.y)), 0.0f, -std::sin(Radians(rotation.y))};
        position = position + right * delta.x + forward * delta.z + Vec3{0.0f, delta.y, 0.0f};
    }

    // Rotation in degrees.
    void Rotate(Vec3 degrees) {
        rotation = rotation + degrees;
        rotation.x = Clamp(rotation.x, -89.999f, 89.999f);
    }
    void RotateX(float degrees) { rotation.x = Clamp(rotation.x + degrees, -89.999f, 89.999f); }
    void RotateY(float degrees) { rotation.y += degrees; }
    void RotateZ(float degrees) { rotation.z += degrees; }

    // Point the object's forward axis at a world position (Y-only yaw), which is
    // what "look at the player" usually means.
    void LookAt(Vec3 target) {
        const Vec3 dir = Normalize(target - position);
        rotation.y = Degrees(std::atan2(-dir.x, -dir.z));
        rotation.x = Degrees(std::asin(Clamp(dir.y, -1.0f, 1.0f)));
    }

    void ScaleBy(Vec3 factor) { scale = scale * factor; }
    void ScaleBy(float factor) { scale = scale * factor; }

    Vec3 ToWorld(Vec3 localPoint) const { return TransformPoint(Matrix(), localPoint); }
    Vec3 DirectionToWorld(Vec3 localDir) const { return TransformDirection(Matrix(), localDir); }
};

// ----------------------------------------------------------------- random ----
// A small deterministic PRNG. Seeded explicitly so a game can reproduce a run,
// and never touching global state, so systems do not interfere with each other.
struct Random {
    uint32_t state = 0x9E3779B9u;

    Random() = default;
    explicit Random(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}

    void Seed(uint32_t seed) { state = seed ? seed : 0x9E3779B9u; }

    // xorshift32 — fast, decent distribution, no library dependency.
    uint32_t NextUint() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    // Uniform in [0, 1).
    float NextFloat() { return static_cast<float>(NextUint() >> 8) * (1.0f / 16777216.0f); }
    // Uniform in [min, max).
    float Range(float minValue, float maxValue) { return minValue + NextFloat() * (maxValue - minValue); }
    // Uniform integer in [min, max] inclusive.
    int RangeInt(int minValue, int maxValue) {
        if (maxValue <= minValue) return minValue;
        return minValue + static_cast<int>(NextUint() % static_cast<uint32_t>(maxValue - minValue + 1));
    }
    // True with probability `chance` in [0, 1].
    bool Chance(float probability) { return NextFloat() < probability; }
    // A random sign, -1 or +1.
    float Sign() { return (NextUint() & 1u) ? 1.0f : -1.0f; }
    Vec2 InCircle(float radius) {
        const float angle = Range(0.0f, TAU);
        const float r = radius * std::sqrt(NextFloat());
        return Vec2{std::cos(angle) * r, std::sin(angle) * r};
    }
    Vec3 InSphere(float radius) {
        const float z = Range(-1.0f, 1.0f);
        const float angle = Range(0.0f, TAU);
        const float r = std::sqrt(Max(0.0f, 1.0f - z * z));
        return Vec3{std::cos(angle) * r, std::sin(angle) * r, z} * radius;
    }
    Vec3 OnSphere(float radius) {
        const float z = Range(-1.0f, 1.0f);
        const float angle = Range(0.0f, TAU);
        const float r = std::sqrt(Max(0.0f, 1.0f - z * z));
        return Normalize(Vec3{std::cos(angle) * r, std::sin(angle) * r, z}) * radius;
    }
};

// Process-wide convenience RNG, seeded from the clock the first time it is used.
// Handy for quick effects; use your own Random for anything reproducible.
Random& GlobalRandom();
void SetRandomSeed(uint32_t seed);
float RandomFloat(float minValue, float maxValue);
int RandomInt(int minValue, int maxValue);
bool RandomChance(float probability);

// ------------------------------------------------------------------ timer ----
// A countdown that is updated once per frame. `Tick` returns true exactly on the
// frame it fires, which keeps the "cooldown / spawn interval" pattern to one line.
struct Timer {
    float duration = 1.0f;
    float remaining = 0.0f;
    bool repeating = false;

    Timer() = default;
    explicit Timer(float seconds, bool repeat = false)
        : duration(seconds), remaining(seconds), repeating(repeat) {}

    void Restart() { remaining = duration; }
    void Stop() { remaining = 0.0f; }
    bool Running() const { return remaining > 0.0f; }
    // Fraction of the way through, 0 at the start and 1 when it fires.
    float Progress() const { return duration > 0.0f ? 1.0f - Clamp(remaining / duration, 0.0f, 1.0f) : 1.0f; }

    bool Tick(float dt) {
        if (remaining <= 0.0f) return false;
        remaining -= dt;
        if (remaining > 0.0f) return false;
        if (repeating) remaining += duration;
        else remaining = 0.0f;
        return true;
    }
};

} // namespace a3d
