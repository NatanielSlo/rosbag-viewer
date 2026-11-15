#include <gtest/gtest.h>

#include "model/frames.h"

using namespace viewer;

namespace {
void expectNear(Vec3 a, Vec3 b, double tol = 1e-9) {
    EXPECT_NEAR(a.x, b.x, tol);
    EXPECT_NEAR(a.y, b.y, tol);
    EXPECT_NEAR(a.z, b.z, tol);
}
Quat yawAboutDown(double rad) {
    return {std::cos(rad / 2), 0, 0, std::sin(rad / 2)};
}  // PX4: z is Down
}  // namespace

TEST(Frames, NedToEnuSwapsAxesAndFlipsDown) {
    // Onboarding PDF section 4: (x, y, z)_ENU = (y, x, -z)_NED.
    expectNear(nedToEnu({1, 2, 3}), {2, 1, -3});
    // An altitude of 2 m is z = -2 in NED and +2 in ENU.
    EXPECT_DOUBLE_EQ(nedToEnu({0, 0, -2}).z, 2.0);
}

TEST(Frames, FrdToFluFlipsRightAndDown) { expectNear(frdToFlu({1, 2, 3}), {1, -2, -3}); }

TEST(Frames, WrapAngle) {
    EXPECT_NEAR(wrapAngle(0), 0, 1e-12);
    EXPECT_NEAR(wrapAngle(3 * kPi / 2), -kPi / 2, 1e-12);
    EXPECT_NEAR(wrapAngle(-3 * kPi / 2), kPi / 2, 1e-12);
    EXPECT_NEAR(wrapAngle(kPi), kPi, 1e-12);
    EXPECT_NEAR(wrapAngle(-kPi), kPi, 1e-12);
    EXPECT_NEAR(wrapAngle(7 * kPi), kPi, 1e-9);
}

TEST(Frames, LevelAttitudeFacingNorthMapsBodyAxesToWorldAxes) {
    // PX4 identity quaternion: level, nose pointing North.
    const Quat q = attitudeFrdNedToFluEnu({1, 0, 0, 0});
    expectNear(rotate(q, {1, 0, 0}), {0, 1, 0});   // forward -> North
    expectNear(rotate(q, {0, 1, 0}), {-1, 0, 0});  // left    -> West
    expectNear(rotate(q, {0, 0, 1}), {0, 0, 1});   // up      -> Up
    EXPECT_NEAR(headingFromAttitude(q), 0.0, 1e-9);
    EXPECT_NEAR(yawEnu(q), kPi / 2, 1e-9);  // ENU yaw counts from East
}

TEST(Frames, PxYawIsHeadingClockwiseFromNorth) {
    // PX4 yaw +90 deg = nose to the East, 180 = South, -90 = West.
    const Quat east = attitudeFrdNedToFluEnu(yawAboutDown(kPi / 2));
    expectNear(rotate(east, {1, 0, 0}), {1, 0, 0});
    EXPECT_NEAR(headingFromAttitude(east), kPi / 2, 1e-9);

    const Quat south = attitudeFrdNedToFluEnu(yawAboutDown(kPi));
    expectNear(rotate(south, {1, 0, 0}), {0, -1, 0});
    EXPECT_NEAR(headingFromAttitude(south), kPi, 1e-9);

    const Quat west = attitudeFrdNedToFluEnu(yawAboutDown(-kPi / 2));
    expectNear(rotate(west, {1, 0, 0}), {-1, 0, 0});
    EXPECT_NEAR(headingFromAttitude(west), 3 * kPi / 2, 1e-9);
}

TEST(Frames, RollInPxAttitudeIsRollToTheRightInFlu) {
    // PX4 roll +30 deg (about Forward, right side down) -> in FLU a roll of -30 deg about x.
    const double r = 30 * kPi / 180;
    const Quat q = attitudeFrdNedToFluEnu({std::cos(r / 2), std::sin(r / 2), 0, 0});
    // The body's up axis tilts towards the East (right of a north-facing drone).
    const Vec3 up = rotate(q, {0, 0, 1});
    EXPECT_GT(up.x, 0.0);
    EXPECT_NEAR(up.x, std::sin(r), 1e-9);
}

TEST(Frames, GeodeticToEnuAroundGarching) {
    const Geodetic home{48.2655, 11.6710, 527.0};
    // 0.001 deg north is about 111.2 m, with the same height.
    const Vec3 n = geodeticToEnu({48.2665, 11.6710, 527.0}, home);
    EXPECT_NEAR(n.y, 111.2, 0.3);
    EXPECT_NEAR(n.x, 0.0, 1e-6);
    EXPECT_NEAR(n.z, 0.0, 0.01);
    // 0.001 deg east at 48.27 N is about 74.1 m.
    const Vec3 e = geodeticToEnu({48.2655, 11.6720, 527.0}, home);
    EXPECT_NEAR(e.x, 74.1, 0.3);
    EXPECT_NEAR(e.y, 0.0, 0.01);
    // Height is passed through as Up.
    EXPECT_NEAR(geodeticToEnu({48.2655, 11.6710, 547.0}, home).z, 20.0, 1e-6);
    // The origin maps to zero.
    expectNear(geodeticToEnu(home, home), {0, 0, 0}, 1e-6);
}

TEST(Frames, GeodeticEnuRoundTrip) {
    const Geodetic home{48.2655, 11.6710, 527.0};
    for (Vec3 p :
         {Vec3{0, 0, 0}, Vec3{120, -80, 35}, Vec3{-1500, 2300, 120}, Vec3{5000, 5000, 400}}) {
        const Geodetic g = enuToGeodetic(p, home);
        expectNear(geodeticToEnu(g, home), p, 1e-4);
    }
}
