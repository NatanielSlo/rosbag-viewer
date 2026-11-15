#include <gtest/gtest.h>

#include "model/flightmodel.h"
#include "model/frames.h"

using namespace viewer;

namespace {
Quat yawEnuQuat(double rad) { return {std::cos(rad / 2), 0, 0, std::sin(rad / 2)}; }

DroneTrack straightLine() {
    DroneTrack t;
    for (int i = 0; i <= 10; ++i) t.addSample({double(i), {10.0 * i, 0, 5}, yawEnuQuat(0)});
    return t;
}
}  // namespace

TEST(DroneTrack, InterpolatesPosition) {
    const auto p = straightLine().poseAt(2.5);
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->position.x, 25.0, 1e-9);
    EXPECT_NEAR(p->position.z, 5.0, 1e-9);
}

TEST(DroneTrack, ExactSampleTimesReturnTheSample) {
    const auto p = straightLine().poseAt(4.0);
    ASSERT_TRUE(p);
    EXPECT_NEAR(p->position.x, 40.0, 1e-9);
}

TEST(DroneTrack, InterpolatesAttitudeTheShortWayAcrossTheWrap) {
    DroneTrack t;
    t.addSample({0, {}, yawEnuQuat(170 * kPi / 180)});
    t.addSample({1, {}, yawEnuQuat(-170 * kPi / 180)});
    const auto p = t.poseAt(0.5);
    ASSERT_TRUE(p);
    // Halfway between +170 and -170 degrees is 180 (not 0).
    EXPECT_NEAR(std::abs(yawEnu(p->attitude)), kPi, 1e-6);
}

TEST(DroneTrack, NoDataFarOutsideTheRecordedRange) {
    const auto t = straightLine();
    EXPECT_FALSE(t.poseAt(-5.0));
    EXPECT_FALSE(t.poseAt(30.0));
}

TEST(DroneTrack, HoldsTheEndsWithinTheGapLimit) {
    const auto t = straightLine();
    ASSERT_TRUE(t.poseAt(-1.0));
    EXPECT_NEAR(t.poseAt(-1.0)->position.x, 0.0, 1e-9);
    ASSERT_TRUE(t.poseAt(11.5));
    EXPECT_NEAR(t.poseAt(11.5)->position.x, 100.0, 1e-9);
}

TEST(DroneTrack, DoesNotDrawALineAcrossADropout) {
    DroneTrack t;
    t.addSample({0, {0, 0, 0}, {}});
    t.addSample({1, {10, 0, 0}, {}});
    t.addSample({20, {500, 0, 0}, {}});  // 19 s without data
    t.addSample({21, {510, 0, 0}, {}});
    // Close to the first side: hold the last good sample.
    ASSERT_TRUE(t.poseAt(2.0));
    EXPECT_NEAR(t.poseAt(2.0)->position.x, 10.0, 1e-9);
    // In the middle of the dropout: no data, shown as "—".
    EXPECT_FALSE(t.poseAt(10.0));
    // Close to the second side: hold the first sample after the gap.
    ASSERT_TRUE(t.poseAt(19.0));
    EXPECT_NEAR(t.poseAt(19.0)->position.x, 500.0, 1e-9);
}

TEST(DroneTrack, EmptyTrackHasNoData) {
    DroneTrack t;
    EXPECT_TRUE(t.empty());
    EXPECT_FALSE(t.poseAt(0));
    EXPECT_FALSE(t.modeAt(0));
}

TEST(DroneTrack, ModeAtReturnsTheLastChangeAtOrBeforeT) {
    DroneTrack t;
    t.addModeChange(0.0, "MANUAL");
    t.addModeChange(10.0, "OFFBOARD");
    t.addModeChange(30.0, "LAND");
    EXPECT_EQ(t.modeAt(0.0), "MANUAL");
    EXPECT_EQ(t.modeAt(9.99), "MANUAL");
    EXPECT_EQ(t.modeAt(10.0), "OFFBOARD");
    EXPECT_EQ(t.modeAt(100.0), "LAND");
}

TEST(DroneTrack, NoModeBeforeTheFirstChange) {
    DroneTrack t;
    t.addModeChange(5.0, "MANUAL");
    EXPECT_FALSE(t.modeAt(1.0));
}
