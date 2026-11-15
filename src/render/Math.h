#pragma once

#include <cmath>
#include <cstdint>

namespace df {

struct Vec3 {
    float x = 0, y = 0, z = 0;
    Vec3 operator+(Vec3 o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(Vec3 o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
};
inline float dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline Vec3 cross(Vec3 a, Vec3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline Vec3 normalize(Vec3 v) { float l = std::sqrt(dot(v, v)); return l > 0 ? v * (1.f / l) : v; }

// The EXEs' integer square root (GOG Df.exe 0x477620, GOG Df2.exe 0x41e830,
// the same code): n itself while (int)n < 2; else the floor root r of a
// 16-step digit loop, plus 1 when n - r*r >= r - 1 (unsigned compare), so it
// rounds: 2 -> 2, 5 -> 3, 11 -> 4, 110 -> 11.
inline uint32_t dfIsqrt(uint32_t n) {
    if (int32_t(n) < 2) return n;
    uint32_t r = uint32_t(std::sqrt(double(n)));
    while (uint64_t(r) * r > n) --r;
    while (uint64_t(r + 1) * (r + 1) <= n) ++r;
    return n - r * r >= r - 1 ? r + 1 : r;
}
// dfIsqrt of the sum of the squared whole-metre differences (16.16 >> 16,
// imul / add with the 32-bit wrap), as a flat range in metres.
inline float dfFlatRange(float dx, float dy) {
    const uint32_t a = uint32_t(int32_t(std::floor(dx))), b = uint32_t(int32_t(std::floor(dy)));
    return float(dfIsqrt(a * a + b * b));
}

// Column-major 4x4 matrix (OpenGL convention).
struct Mat4 {
    float m[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
    float& at(int row, int col) { return m[col * 4 + row]; }
    float at(int row, int col) const { return m[col * 4 + row]; }
};

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r;
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j) {
            float s = 0;
            for (int k = 0; k < 4; ++k) s += a.at(i, k) * b.at(k, j);
            r.at(i, j) = s;
        }
    return r;
}

inline Mat4 perspective(float fovyRad, float aspect, float zn, float zf) {
    Mat4 r;
    float f = 1.f / std::tan(fovyRad * 0.5f);
    r.m[0] = f / aspect; r.m[5] = f;
    r.m[10] = (zf + zn) / (zn - zf); r.m[11] = -1;
    r.m[14] = 2 * zf * zn / (zn - zf); r.m[15] = 0;
    return r;
}

inline Mat4 lookDir(Vec3 eye, Vec3 fwd, Vec3 up) {
    Vec3 f = normalize(fwd), s = normalize(cross(f, up)), u = cross(s, f);
    Mat4 r;
    r.at(0, 0) = s.x; r.at(0, 1) = s.y; r.at(0, 2) = s.z;
    r.at(1, 0) = u.x; r.at(1, 1) = u.y; r.at(1, 2) = u.z;
    r.at(2, 0) = -f.x; r.at(2, 1) = -f.y; r.at(2, 2) = -f.z;
    r.at(0, 3) = -dot(s, eye); r.at(1, 3) = -dot(u, eye); r.at(2, 3) = dot(f, eye);
    return r;
}

inline Mat4 translate(Vec3 t) {
    Mat4 r;
    r.m[12] = t.x; r.m[13] = t.y; r.m[14] = t.z;
    return r;
}

inline Mat4 rotateX(float a) {
    Mat4 r;
    float c = std::cos(a), s = std::sin(a);
    r.at(1, 1) = c; r.at(1, 2) = -s;
    r.at(2, 1) = s; r.at(2, 2) = c;
    return r;
}

inline Mat4 rotateY(float a) {
    Mat4 r;
    float c = std::cos(a), s = std::sin(a);
    r.at(0, 0) = c; r.at(0, 2) = s;
    r.at(2, 0) = -s; r.at(2, 2) = c;
    return r;
}

inline Mat4 inverseRotationOnly(const Mat4& v) {
    // Transpose of the upper 3x3 (view rotation is orthonormal).
    Mat4 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) r.at(i, j) = v.at(j, i);
    return r;
}

} // namespace df
