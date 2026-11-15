#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>

#include "bag/recordinginfo.h"

using namespace viewer;
namespace fs = std::filesystem;

namespace {
fs::path makeBag(const std::string& name, const std::string& extra) {
    const fs::path dir = fs::temp_directory_path() / ("viewer_info_" + name);
    fs::remove_all(dir);
    EXPECT_EQ(std::system((std::string(MAKE_TEST_BAG) + " --out " + dir.string() + " " + extra +
                           " > /dev/null")
                              .c_str()),
              0);
    return dir;
}
const InfoLine& find(const DroneInfo& d, const std::string& label) {
    for (const auto& l : d.lines)
        if (l.label == label) return l;
    throw std::runtime_error("no line " + label);
}
}  // namespace

TEST(RecordingInfo, EverythingFoundForACompleteBag) {
    const auto info = describeRecording(
        loadFlight(makeBag("full", "--duration 20 --gps 48.2 11.6 480").string()));
    ASSERT_EQ(info.drones.size(), 1u);
    for (const auto& l : info.drones[0].lines)
        EXPECT_EQ(l.status, Availability::Found) << l.label << ": " << l.detail;
    EXPECT_TRUE(info.warnings.empty());
    EXPECT_TRUE(info.unusedTopics.empty());
    EXPECT_NE(find(info.drones[0], "Position").detail.find("/fmu/out/vehicle_local_position_v1"),
              std::string::npos);
}

TEST(RecordingInfo, MissingGpsIsReportedWithItsConsequence) {
    const auto info = describeRecording(loadFlight(makeBag("nogps", "--duration 20").string()));
    const InfoLine& gps = find(info.drones[0], "GPS");
    EXPECT_EQ(gps.status, Availability::NotInBag);
    EXPECT_NE(gps.detail.find("local 3D scene"), std::string::npos);
    EXPECT_EQ(find(info.drones[0], "Position").status, Availability::Found);
}

TEST(RecordingInfo, TopicWithoutUsableSamplesIsDifferentFromMissingTopic) {
    FlightData f;
    f.durationSeconds = 5;
    f.sources[""].position = "/fmu/out/vehicle_local_position_v1";  // topic exists...
    f.drones[""].signal("armed").add(0, 1);                         // ...but no valid position
    const auto info = describeRecording(f);
    EXPECT_EQ(find(info.drones[0], "Position").status, Availability::NoSamples);
}

TEST(RecordingInfo, UnusedTopicsAreListed) {
    FlightData f;
    f.durationSeconds = 5;
    f.topics = {{"/fmu/out/vehicle_local_position_v1", "px4_msgs/msg/VehicleLocalPosition", 10},
                {"/camera/image_raw", "sensor_msgs/msg/Image", 100}};
    f.sources[""].position = "/fmu/out/vehicle_local_position_v1";
    f.drones[""].addSample({0, {}, {}});
    const auto info = describeRecording(f);
    ASSERT_EQ(info.unusedTopics.size(), 1u);
    EXPECT_EQ(info.unusedTopics[0].name, "/camera/image_raw");
}

TEST(RecordingInfo, BagWithoutDroneDataGetsAWarning) {
    FlightData f;
    f.durationSeconds = 5;
    const auto info = describeRecording(f);
    EXPECT_TRUE(info.drones.empty());
    ASSERT_FALSE(info.warnings.empty());
    EXPECT_NE(info.warnings[0].find("No drone data"), std::string::npos);
}
