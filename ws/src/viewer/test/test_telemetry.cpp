#include <gtest/gtest.h>

#include "model/frames.h"
#include "model/telemetry.h"

using namespace viewer;

namespace {
// 10 s flight east at 5 m/s, climbing 1 m/s, heading east (PX4 yaw 90 deg), armed from t = 2.
DroneTrack makeTrack() {
    DroneTrack t;
    const Quat east = attitudeFrdNedToFluEnu({std::cos(kPi / 4), 0, 0, std::sin(kPi / 4)});
    for (int i = 0; i <= 10; ++i) {
        t.addSample({double(i), {5.0 * i, 0, double(i)}, east});
        t.signal("ground_speed").add(i, 5.0);
        t.signal("vertical_speed").add(i, 1.0);
        t.signal("battery_remaining").add(i, 1.0 - 0.05 * i);
        t.signal("battery_voltage").add(i, 16.0 - 0.1 * i);
        t.signal("armed").add(i, i >= 2 ? 1.0 : 0.0);
    }
    return t;
}
}  // namespace

TEST(Telemetry, ValuesAtATimeInsideTheFlight) {
    const Telemetry v = telemetryAt(makeTrack(), 5.0);
    EXPECT_NEAR(*v.altitude, 5.0, 1e-9);
    EXPECT_NEAR(*v.distanceFromHome, 25.0, 1e-9);
    EXPECT_NEAR(*v.groundSpeed, 5.0, 1e-9);
    EXPECT_NEAR(*v.verticalSpeed, 1.0, 1e-9);
    EXPECT_NEAR(*v.headingRad, kPi / 2, 1e-9);  // east
    EXPECT_NEAR(*v.batteryFraction, 0.75, 1e-9);
    EXPECT_NEAR(*v.batteryVoltage, 15.5, 1e-9);
    EXPECT_TRUE(*v.armed);
}

TEST(Telemetry, FlightTimeCountsFromTheFirstArming) {
    const DroneTrack t = makeTrack();
    EXPECT_FALSE(telemetryAt(t, 1.0).flightTime);  // not armed yet
    EXPECT_NEAR(*telemetryAt(t, 7.5).flightTime, 5.5, 1e-9);
    EXPECT_FALSE(*telemetryAt(t, 1.0).armed);
}

TEST(Telemetry, ArmedIsAStepNotAnInterpolation) {
    DroneTrack t;
    t.signal("armed").add(0.0, 0.0);
    t.signal("armed").add(1.0, 1.0);
    EXPECT_FALSE(*telemetryAt(t, 0.9)
                      .armed);  // an interpolation would give 0.9; a flag stays at its last value
    EXPECT_TRUE(*telemetryAt(t, 1.0).armed);
}

TEST(Telemetry, MissingSignalsAreEmptyNotZero) {
    DroneTrack t;
    t.addSample({0, {}, {}});
    const Telemetry v = telemetryAt(t, 0.0);
    EXPECT_TRUE(v.altitude);
    EXPECT_FALSE(v.groundSpeed);
    EXPECT_FALSE(v.batteryFraction);
    EXPECT_FALSE(v.armed);
    EXPECT_FALSE(v.flightTime);
}

TEST(Telemetry, NoDataAfterADropoutOfASingleSignal) {
    DroneTrack t = makeTrack();
    t.signal("ground_speed") = ScalarSeries{};  // reset, then add a gap
    t.signal("ground_speed").add(0.0, 1.0);
    t.signal("ground_speed").add(10.0, 9.0);
    EXPECT_FALSE(telemetryAt(t, 5.0).groundSpeed);  // 10 s gap: no invented value
    EXPECT_TRUE(telemetryAt(t, 5.0).altitude);      // other signals are unaffected
}

TEST(Telemetry, EverythingEmptyOutsideTheRecording) {
    const Telemetry v = telemetryAt(makeTrack(), 100.0);
    EXPECT_FALSE(v.altitude);
    EXPECT_FALSE(v.groundSpeed);
    EXPECT_FALSE(v.headingRad);
}
