// Plumbing test: the generated bags can be read back with rosbag2_cpp, for both storage formats.
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <map>
#include <string>

#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_options.hpp>

namespace fs = std::filesystem;

namespace {
std::map<std::string, int> countMessages(const std::string& dir, const std::string& storage) {
    rosbag2_storage::StorageOptions so;
    so.uri = dir;
    so.storage_id = storage;
    rosbag2_cpp::Reader reader;
    reader.open(so, rosbag2_cpp::ConverterOptions{});
    std::map<std::string, int> counts;
    while (reader.has_next()) counts[reader.read_next()->topic_name]++;
    return counts;
}
}  // namespace

class TestBag : public ::testing::TestWithParam<std::string> {};

TEST_P(TestBag, GeneratedBagCanBeReadBack) {
    const std::string storage = GetParam();
    const fs::path dir = fs::temp_directory_path() / ("viewer_testbag_" + storage);
    fs::remove_all(dir);
    const std::string cmd = std::string(MAKE_TEST_BAG) + " --out " + dir.string() + " --storage " +
                            storage + " --duration 20 --gps 48.2655 11.6710 480 > /dev/null";
    ASSERT_EQ(std::system(cmd.c_str()), 0);

    const auto counts = countMessages(dir.string(), storage);
    // 20 s at 50 Hz = 1001 messages, GPS at 10 Hz = 201, status at 2 Hz = 41
    EXPECT_EQ(counts.at("/fmu/out/vehicle_local_position_v1"), 1001);
    EXPECT_EQ(counts.at("/fmu/out/vehicle_attitude"), 1001);
    EXPECT_EQ(counts.at("/fmu/out/vehicle_global_position"), 201);
    EXPECT_EQ(counts.at("/fmu/out/vehicle_status_v1"), 41);
    fs::remove_all(dir);
}

INSTANTIATE_TEST_SUITE_P(Storage, TestBag, ::testing::Values("sqlite3", "mcap"));
