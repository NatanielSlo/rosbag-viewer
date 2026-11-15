#include "model/frames.h"

namespace viewer {
namespace {
constexpr double kA = 6378137.0;            // WGS84 semi-major axis
constexpr double kF = 1.0 / 298.257223563;  // flattening
constexpr double kE2 = kF * (2.0 - kF);     // first eccentricity squared
constexpr double kDeg = kPi / 180.0;
}  // namespace

double wrapAngle(double a) {
    a = std::fmod(a + kPi, 2.0 * kPi);
    if (a <= 0) a += 2.0 * kPi;
    return a - kPi;
}

Quat attitudeFrdNedToFluEnu(Quat q) {
    // q_flu_enu = q_enu_ned * q_ned_frd * q_frd_flu
    const double s = std::sqrt(0.5);
    const Quat enuFromNed{0, s, s, 0};  // 180 deg about (1,1,0): swaps x/y, negates z
    const Quat fruFromFlu{0, 1, 0, 0};  // 180 deg about x:      negates y/z
    return normalized(enuFromNed * q * fruFromFlu);
}

double yawEnu(Quat q) {
    return std::atan2(2.0 * (q.w * q.z + q.x * q.y), 1.0 - 2.0 * (q.y * q.y + q.z * q.z));
}

double headingFromAttitude(Quat q) {
    double h = kPi / 2.0 - yawEnu(q);
    h = std::fmod(h, 2.0 * kPi);
    if (h < 0) h += 2.0 * kPi;
    // A tiny negative value (-1e-16) becomes exactly 2pi after the shift: report North as 0, not
    // 360.
    return h >= 2.0 * kPi ? 0.0 : h;
}

Vec3 geodeticToEcef(Geodetic p) {
    const double lat = p.latDeg * kDeg, lon = p.lonDeg * kDeg;
    const double sl = std::sin(lat), cl = std::cos(lat);
    const double n = kA / std::sqrt(1.0 - kE2 * sl * sl);
    return {(n + p.heightM) * cl * std::cos(lon), (n + p.heightM) * cl * std::sin(lon),
            (n * (1.0 - kE2) + p.heightM) * sl};
}

Vec3 geodeticToEnu(Geodetic p, Geodetic o) {
    const Vec3 d = geodeticToEcef(p) - geodeticToEcef(o);
    const double lat = o.latDeg * kDeg, lon = o.lonDeg * kDeg;
    const double sl = std::sin(lat), cl = std::cos(lat), so = std::sin(lon), co = std::cos(lon);
    return {-so * d.x + co * d.y, -sl * co * d.x - sl * so * d.y + cl * d.z,
            cl * co * d.x + cl * so * d.y + sl * d.z};
}

Geodetic enuToGeodetic(Vec3 enu, Geodetic o) {
    const double lat = o.latDeg * kDeg, lon = o.lonDeg * kDeg;
    const double sl = std::sin(lat), cl = std::cos(lat), so = std::sin(lon), co = std::cos(lon);
    const Vec3 d{-so * enu.x - sl * co * enu.y + cl * co * enu.z,
                 co * enu.x - sl * so * enu.y + cl * so * enu.z, cl * enu.y + sl * enu.z};
    const Vec3 e = geodeticToEcef(o) + d;
    // Iterative ECEF -> geodetic (converges in a few steps for heights near the surface).
    const double p = std::hypot(e.x, e.y);
    double latI = std::atan2(e.z, p * (1.0 - kE2)), h = 0;
    for (int i = 0; i < 6; ++i) {
        const double s = std::sin(latI);
        const double n = kA / std::sqrt(1.0 - kE2 * s * s);
        h = p / std::cos(latI) - n;
        latI = std::atan2(e.z, p * (1.0 - kE2 * n / (n + h)));
    }
    return {latI / kDeg, std::atan2(e.y, e.x) / kDeg, h};
}

}  // namespace viewer
