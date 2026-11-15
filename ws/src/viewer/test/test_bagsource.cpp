// End to end: synthetic bag -> BagSource -> flight model, compared with the known flight.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "bag/bagsource.h"
#include "bag/recordinginfo.h"
#include "model/frames.h"
#include "model/telemetry.h"
#include "testing/shapes.h"

using namespace viewer;
namespace fs = std::filesystem;

namespace {
fs::path makeBag(const std::string& name, const std::string& storage, const std::string& extra) {
    const fs::path dir = fs::temp_directory_path() / ("viewer_" + name + "_" + storage);
    fs::remove_all(dir);
    const std::string cmd = std::string(MAKE_TEST_BAG) + " --out " + dir.string() + " --storage " +
                            storage + " " + extra + " > /dev/null";
    EXPECT_EQ(std::system(cmd.c_str()), 0);
    return dir;
}
}  // namespace

class BagSourceTest : public ::testing::TestWithParam<std::string> {};

TEST_P(BagSourceTest, CircleFlightMatchesGroundTruth) {
    const auto dir = makeBag("circle", GetParam(), "--shape circle --duration 40");
    const FlightData f = loadFlight(dir.string());

    EXPECT_NEAR(f.durationSeconds, 40.0, 0.1);
    ASSERT_EQ(f.drones.size(), 1u);
    const DroneTrack& d = f.drones.at("");
    ASSERT_FALSE(d.empty());

    // During the shape (plan: from 6 s) the pose must match the shape, converted to ENU.
    const fixtures::FlightPlan plan;
    for (double t : {8.0, 12.5, 20.0, 30.0}) {
        const auto p = d.poseAt(t);
        ASSERT_TRUE(p) << "t=" << t;
        const auto truth = fixtures::shapeAt(fixtures::Shape::Circle, t - plan.shapeAt);
        EXPECT_NEAR(p->position.x, truth.east, 0.05) << "t=" << t;     // ENU x = East
        EXPECT_NEAR(p->position.y, truth.north, 0.05) << "t=" << t;    // ENU y = North
        EXPECT_NEAR(p->position.z, plan.altitude, 0.05) << "t=" << t;  // Up
        // heading = direction of travel, clockwise from North
        double dh = wrapAngle(headingFromAttitude(p->attitude) - truth.yaw);
        EXPECT_NEAR(dh, 0.0, 0.02) << "t=" << t;
    }
}

TEST_P(BagSourceTest, TelemetrySignalsAreRead) {
    const auto dir =
        makeBag("telemetry", GetParam(), "--shape circle --duration 40 --gps 48.2655 11.6710 480");
    const FlightData f = loadFlight(dir.string());
    const DroneTrack& d = f.drones.at("");

    const Telemetry v = telemetryAt(d, 20.0);
    EXPECT_NEAR(*v.groundSpeed, 3.0 * 0.4, 0.01);  // circle: R * omega = 1.2 m/s
    EXPECT_NEAR(*v.verticalSpeed, 0.0, 0.01);
    EXPECT_NEAR(*v.altitude, 2.0, 0.05);
    EXPECT_NEAR(*v.batteryFraction, 1.0 - 0.2 * 20.0 / 40.0, 0.01);
    EXPECT_NEAR(*v.batteryVoltage, 16.8 - 1.2 * 20.0 / 40.0, 0.05);
    EXPECT_TRUE(*v.armed);
    EXPECT_NEAR(*v.flightTime, 19.0, 0.6);  // armed at 1 s, 2 Hz status
    EXPECT_FALSE(*telemetryAt(d, 0.2).armed);

    // climbing at 1 m/s at t = 4 s (climb 2 s .. 4 s)
    EXPECT_NEAR(*telemetryAt(d, 3.0).verticalSpeed, 1.0, 0.01);

    // GPS: 10 Hz for 40 s, ellipsoid height = AMSL + 47 m, origin from the local position
    EXPECT_NEAR(double(d.gps().size()), 401.0, 2.0);
    EXPECT_NEAR(d.gps().back().position.heightM, 480.0 + 47.0, 1.0);  // landed
    ASSERT_TRUE(d.origin());
    EXPECT_NEAR(d.origin()->latDeg, 48.2655, 1e-9);
    EXPECT_NEAR(d.origin()->lonDeg, 11.6710, 1e-9);
}

TEST_P(BagSourceTest, FlightModesAreRead) {
    const auto dir = makeBag("modes", GetParam(), "--duration 40");
    const FlightData f = loadFlight(dir.string());  // keep it alive: d refers into it
    const DroneTrack& d = f.drones.at("");
    EXPECT_EQ(d.modeAt(0.2), "MANUAL");
    EXPECT_EQ(d.modeAt(10.0), "OFFBOARD");
    EXPECT_EQ(d.modeAt(39.0), "LAND");
    EXPECT_EQ(d.modeChanges().size(), 3u);
}

TEST_P(BagSourceTest, DronesAreDetectedByNamespace) {
    const auto dir = makeBag("ns", GetParam(), "--duration 10 --namespace /drone1");
    const FlightData f = loadFlight(dir.string());
    ASSERT_EQ(f.drones.size(), 1u);
    EXPECT_TRUE(f.drones.count("/drone1"));
}

TEST_P(BagSourceTest, CompressedBagGivesTheSameFlight) {
    const auto plain = loadFlight(makeBag("plain", GetParam(), "--duration 20").string());
    const auto zst = loadFlight(makeBag("zstd", GetParam(), "--duration 20 --compress").string());
    // metadata.yaml must say it is compressed, otherwise this test proves nothing
    std::ifstream meta(fs::temp_directory_path() / ("viewer_zstd_" + GetParam()) / "metadata.yaml");
    const std::string text((std::istreambuf_iterator<char>(meta)),
                           std::istreambuf_iterator<char>());
    EXPECT_NE(text.find("zstd"), std::string::npos);

    const DroneTrack& a = plain.drones.at("");
    const DroneTrack& b = zst.drones.at("");
    ASSERT_EQ(a.samples().size(), b.samples().size());
    EXPECT_NEAR(a.poseAt(12.0)->position.x, b.poseAt(12.0)->position.x, 1e-9);
}

TEST_P(BagSourceTest, SplitBagIsReadAcrossAllFiles) {
    const auto dir = makeBag("split", GetParam(), "--duration 150 --split-bytes 300000");
    int files = 0;
    for (const auto& e : fs::directory_iterator(dir))
        if (e.path().extension() == ".db3" || e.path().extension() == ".mcap") ++files;
    EXPECT_GT(files, 1) << "the bag was not split, the test proves nothing";

    const auto whole = loadFlight(makeBag("whole", GetParam(), "--duration 150").string());
    const auto split = loadFlight(dir.string());
    EXPECT_EQ(whole.drones.at("").samples().size(), split.drones.at("").samples().size());
    EXPECT_NEAR(split.durationSeconds, 150.0, 0.1);
}

INSTANTIATE_TEST_SUITE_P(Storage, BagSourceTest, ::testing::Values("sqlite3", "mcap"));

// The same flight recorded the MAVROS way (ENU, standard ROS messages) must give the same result.
class MavrosBagTest : public ::testing::TestWithParam<std::string> {};

TEST_P(MavrosBagTest, CircleFlightMatchesGroundTruth) {
    const auto dir =
        makeBag("mav_circle", GetParam(), "--flavor mavros --shape circle --duration 40");
    const FlightData f = loadFlight(dir.string());
    ASSERT_EQ(f.drones.size(), 1u);
    const DroneTrack& d = f.drones.at("");
    const fixtures::FlightPlan plan;
    for (double tt : {8.0, 12.5, 20.0, 30.0}) {
        const auto p = d.poseAt(tt);
        ASSERT_TRUE(p) << "t=" << tt;
        const auto truth = fixtures::shapeAt(fixtures::Shape::Circle, tt - plan.shapeAt);
        EXPECT_NEAR(p->position.x, truth.east, 0.05) << "t=" << tt;
        EXPECT_NEAR(p->position.y, truth.north, 0.05) << "t=" << tt;
        EXPECT_NEAR(p->position.z, plan.altitude, 0.05) << "t=" << tt;
        EXPECT_NEAR(wrapAngle(headingFromAttitude(p->attitude) - truth.yaw), 0.0, 0.02)
            << "t=" << tt;
    }
}

TEST_P(MavrosBagTest, ModesTelemetryGpsAndInfo) {
    const auto dir =
        makeBag("mav_all", GetParam(),
                "--flavor mavros --shape circle --duration 40 --gps 48.2655 11.6710 480 "
                "--namespace /drone2");
    const FlightData f = loadFlight(dir.string());
    ASSERT_EQ(f.drones.count("/drone2"), 1u);  // namespace in front of /mavros
    const DroneTrack& d = f.drones.at("/drone2");
    // MAVROS strings are shown with the same names as PX4 modes
    EXPECT_EQ(d.modeAt(0.2), "MANUAL");
    EXPECT_EQ(d.modeAt(10.0), "OFFBOARD");
    EXPECT_EQ(d.modeAt(39.0), "LAND");

    const Telemetry v = telemetryAt(d, 20.0);
    EXPECT_NEAR(*v.groundSpeed, 1.2, 0.01);
    EXPECT_NEAR(*v.batteryFraction, 0.9, 0.01);
    EXPECT_NEAR(*v.batteryVoltage, 16.2, 0.05);
    EXPECT_TRUE(*v.armed);
    EXPECT_NEAR(double(d.gps().size()), 401.0, 2.0);
    EXPECT_NEAR(d.gps().back().position.heightM, 480.0 + 47.0, 1.0);

    // and the Recording info names the MAVROS topics
    const auto info = describeRecording(f);
    EXPECT_NE(info.drones[0].lines[0].detail.find("/drone2/mavros/local_position/pose"),
              std::string::npos);
}

TEST_P(MavrosBagTest, MavrosIsPreferredWhenBothFlavorsAreInOneBag) {
    const auto dir =
        makeBag("both", GetParam(), "--flavor both --duration 20 --gps 48.2655 11.6710 480");
    const FlightData f = loadFlight(dir.string());
    ASSERT_EQ(f.drones.size(), 1u);  // one drone, not two
    const DroneSources& src = f.sources.at("");
    EXPECT_EQ(src.position, "/mavros/local_position/pose");
    EXPECT_EQ(src.status, "/mavros/state");
    EXPECT_EQ(src.gps, "/mavros/global_position/global");
    // the PX4 topics are in the bag but not used for what MAVROS already provides
    const auto info = describeRecording(f);
    bool px4Unused = false;
    for (const auto& u : info.unusedTopics)
        px4Unused |= u.name == "/fmu/out/vehicle_local_position_v1";
    EXPECT_TRUE(px4Unused);
    // 20 s at 50 Hz: both flavors have 1001 position samples, the result must not be doubled
    EXPECT_EQ(f.drones.at("").samples().size(), 1001u);
}

INSTANTIATE_TEST_SUITE_P(Storage, MavrosBagTest, ::testing::Values("sqlite3", "mcap"));

TEST(BagSource, MissingBagGivesAClearError) {
    try {
        loadFlight("/nonexistent/bag");
        FAIL() << "expected BagError";
    } catch (const BagError& e) {
        EXPECT_NE(std::string(e.what()).find("not found"), std::string::npos);
    }
}

TEST(BagSource, FolderThatIsNotABagGivesAClearError) {
    const fs::path dir = fs::temp_directory_path() / "viewer_not_a_bag";
    fs::remove_all(dir);
    fs::create_directories(dir);
    EXPECT_THROW(loadFlight(dir.string()), BagError);
}

TEST(BagSource, ModeNames) {
    EXPECT_EQ(px4ModeName(14), "OFFBOARD");
    EXPECT_EQ(px4ModeName(999), "NAV_STATE_999");
}
