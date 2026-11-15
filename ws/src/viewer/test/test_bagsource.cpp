// End to end: synthetic bag -> BagSource -> flight model, compared with the known flight.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#include "bag/bagsource.h"
#include "model/frames.h"
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

INSTANTIATE_TEST_SUITE_P(Storage, BagSourceTest, ::testing::Values("sqlite3", "mcap"));

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
