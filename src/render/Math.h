#pragma once

#include <cmath>

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
