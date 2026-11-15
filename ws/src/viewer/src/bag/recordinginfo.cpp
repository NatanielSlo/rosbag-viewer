#include "bag/recordinginfo.h"

#include <set>

namespace viewer {
namespace {

InfoLine line(const std::string& label, const std::string& topic, size_t count,
              const std::string& missingEffect) {
    InfoLine l;
    l.label = label;
    if (topic.empty()) {
        l.status = Availability::NotInBag;
        l.detail = "not in the bag: " + missingEffect;
    } else if (count == 0) {
        l.status = Availability::NoSamples;
        l.detail = topic + " has no usable samples: " + missingEffect;
    } else {
        l.status = Availability::Found;
        l.detail = topic + ", " + std::to_string(count) + " samples";
    }
    return l;
}

size_t countOf(const DroneTrack& t, const char* signal) {
    const ScalarSeries* s = t.findSignal(signal);
    return s ? s->size() : 0;
}

}  // namespace

RecordingInfo describeRecording(const FlightData& f) {
    RecordingInfo info;
    info.path = f.path;
    info.durationSeconds = f.durationSeconds;

    std::set<std::string> used;
    for (const auto& [ns, track] : f.drones) {
        const DroneSources src = f.sources.count(ns) ? f.sources.at(ns) : DroneSources{};
        for (const auto* t : {&src.position, &src.attitude, &src.status, &src.gps, &src.battery})
            if (!t->empty()) used.insert(*t);

        DroneInfo d;
        d.drone = ns;
        d.lines.push_back(line("Position", src.position, track.samples().size(),
                               "no flight path, 3D scene or instruments"));
        d.lines.push_back(line("Attitude", src.attitude,
                               src.attitude.empty() ? 0 : track.samples().size(),
                               "the drone model stays level and heading is unknown"));
        d.lines.push_back(line("Flight modes", src.status, track.modeChanges().size(),
                               "no mode colours or timeline bands"));
        d.lines.push_back(line("GPS", src.gps, track.gps().size(),
                               "flight is shown in the local 3D scene, not on the 3D map"));
        d.lines.push_back(line("Battery", src.battery, countOf(track, "battery_remaining"),
                               "no battery value or warnings"));
        info.drones.push_back(std::move(d));
    }
    for (const auto& t : f.topics)
        if (!used.count(t.name)) info.unusedTopics.push_back(t);

    if (f.drones.empty())
        info.warnings.push_back(
            "No drone data found. Expected PX4 topics such as /fmu/out/vehicle_local_position.");
    if (f.durationSeconds <= 0)
        info.warnings.push_back("The recording has no duration (empty or a single instant).");
    return info;
}

}  // namespace viewer
