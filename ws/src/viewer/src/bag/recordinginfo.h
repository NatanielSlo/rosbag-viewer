#pragma once
// "Recording info": what the viewer found in a bag and what it did not, per drone.
// The viewer accepts any bag, so this is how the user learns why a feature is missing.
#include <string>
#include <vector>

#include "bag/bagsource.h"

namespace viewer {

enum class Availability {
    Found,      // data is there
    NoSamples,  // the topic is in the bag, but no usable sample (e.g. estimator never valid)
    NotInBag,   // no such topic
};

struct InfoLine {
    std::string label;  // "Position", "GPS", ...
    Availability status = Availability::NotInBag;
    std::string detail;  // topic and count, or what is missing and what it disables
};

struct DroneInfo {
    std::string drone;  // namespace, "" for a bag without one
    std::vector<InfoLine> lines;
};

struct RecordingInfo {
    std::string path;
    double durationSeconds = 0;
    std::vector<DroneInfo> drones;
    std::vector<TopicInfo> unusedTopics;  // in the bag, not used by the viewer
    std::vector<std::string> warnings;    // whole-recording problems
};

RecordingInfo describeRecording(const FlightData& flight);

}  // namespace viewer
