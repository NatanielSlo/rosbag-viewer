#pragma once
// Small vector and quaternion types for the flight model (no dependencies, header only).
#include <cmath>

namespace viewer {

struct Vec3 {
    double x = 0, y = 0, z = 0;
};
inline Vec3 operator+(Vec3 a, Vec3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline Vec3 operator*(Vec3 a, double s) { return {a.x * s, a.y * s, a.z * s}; }
inline double length(Vec3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
inline Vec3 lerp(Vec3 a, Vec3 b, double t) { return a + (b - a) * t; }

// Unit quaternion, Hamilton convention (w, x, y, z), as used by ROS and PX4.
struct Quat {
    double w = 1, x = 0, y = 0, z = 0;
};

inline Quat operator*(Quat a, Quat b) {
    return {a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
            a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
            a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
            a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w};
}
inline Quat normalized(Quat q) {
    const double n = std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z);
    return n > 0 ? Quat{q.w / n, q.x / n, q.y / n, q.z / n} : Quat{};
}
inline double dot(Quat a, Quat b) { return a.w * b.w + a.x * b.x + a.y * b.y + a.z * b.z; }

// Rotates v by q.
inline Vec3 rotate(Quat q, Vec3 v) {
    const Quat p = q * Quat{0, v.x, v.y, v.z} * Quat{q.w, -q.x, -q.y, -q.z};
    return {p.x, p.y, p.z};
}

// Shortest-path spherical interpolation.
inline Quat slerp(Quat a, Quat b, double t) {
    double d = dot(a, b);
    if (d < 0) {  // q and -q are the same rotation: take the short way round
        b = {-b.w, -b.x, -b.y, -b.z};
        d = -d;
    }
    if (d > 0.9995) {  // nearly parallel: linear interpolation is accurate and avoids 0/0
        return normalized({a.w + (b.w - a.w) * t, a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t,
                           a.z + (b.z - a.z) * t});
    }
    const double theta = std::acos(d);
    const double sa = std::sin((1 - t) * theta) / std::sin(theta);
    const double sb = std::sin(t * theta) / std::sin(theta);
    return {a.w * sa + b.w * sb, a.x * sa + b.x * sb, a.y * sa + b.y * sb, a.z * sa + b.z * sb};
}

}  // namespace viewer
