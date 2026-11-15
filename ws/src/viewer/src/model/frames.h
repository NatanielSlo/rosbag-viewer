#pragma once
// Coordinate frames. Inside the viewer everything is ENU (East, North, Up) for the world and
// FLU (Forward, Left, Up) for the body, in metres and radians. PX4 uses NED / FRD.
#include "model/geometry.h"

namespace viewer {

constexpr double kPi = 3.14159265358979323846;

// PX4 world vector (North, East, Down) -> ENU. (x, y, z)_ENU = (y, x, -z)_NED.
inline Vec3 nedToEnu(Vec3 v) { return {v.y, v.x, -v.z}; }
// PX4 body vector (Forward, Right, Down) -> FLU. (x, y, z)_FLU = (x, -y, -z)_FRD.
inline Vec3 frdToFlu(Vec3 v) { return {v.x, -v.y, -v.z}; }

// Wraps an angle to (-pi, pi].
double wrapAngle(double radians);

// PX4 attitude (rotation FRD body -> NED world) -> viewer attitude (FLU body -> ENU world).
Quat attitudeFrdNedToFluEnu(Quat q_frd_ned);

// Heading as seen from above: radians clockwise from North, in [0, 2pi). (ENU yaw is counter-
// clockwise from East, so heading = pi/2 - yaw.)
double headingFromAttitude(Quat q_flu_enu);
// Yaw in ENU (counter-clockwise from East) of the body's forward axis.
double yawEnu(Quat q_flu_enu);

// WGS84 geodetic position. Height is above the ellipsoid (not AMSL, see docs/prototype-lessons.md
// L8).
struct Geodetic {
    double latDeg = 0, lonDeg = 0, heightM = 0;
};

Vec3 geodeticToEcef(Geodetic p);
// Local ENU metres of `p` relative to `origin` (e.g. the home point).
Vec3 geodeticToEnu(Geodetic p, Geodetic origin);
Geodetic enuToGeodetic(Vec3 enu, Geodetic origin);

}  // namespace viewer
