#pragma once
// Reads a rosbag2 (sqlite3 or mcap, compressed or not) into the flight model.
// The public interface only uses standard types, so ROS stays inside this library.
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "model/flightmodel.h"

namespace viewer {

// Everything that goes wrong while opening or reading a bag: a message for the user, never a crash.
class BagError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct TopicInfo {
    std::string name, type;
    uint64_t count = 0;
};

// Which topic each kind of data came from, per drone (empty = not found in the bag).
struct DroneSources {
    std::string position, attitude, status, gps, battery;
};

struct FlightData {
    std::string path;
    int64_t startNs = 0;  // receive time of the first message (wall clock, ns)
    double durationSeconds = 0;
    std::vector<TopicInfo> topics;  // everything in the bag, also what the viewer does not use
    // One track per drone, keyed by topic namespace ("" for a bag without namespaces).
    std::map<std::string, DroneTrack> drones;
    std::map<std::string, DroneSources> sources;  // same keys as `drones`
};

// Time base: the bag's receive time, in seconds since the first message (docs/architecture.md 3.1).
FlightData loadFlight(const std::string& bagPath);

// PX4 nav_state -> a readable flight-mode name.
std::string px4ModeName(int navState);

}  // namespace viewer
