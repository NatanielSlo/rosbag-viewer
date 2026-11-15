// Prints what the viewer finds in a bag: bag_info PATH
#include <cstdio>

#include "bag/bagsource.h"
#include "bag/recordinginfo.h"
#include "model/frames.h"
#include "model/telemetry.h"

using namespace viewer;

static const char* label(Availability a) {
    return a == Availability::Found       ? "found    "
           : a == Availability::NoSamples ? "no samples"
                                          : "not in bag";
}

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: bag_info PATH\n");
        return 2;
    }
    try {
        const FlightData f = loadFlight(argv[1]);
        const RecordingInfo info = describeRecording(f);
        std::printf("%s\n  duration %.1f s, %zu topics\n", info.path.c_str(), info.durationSeconds,
                    f.topics.size());
        for (const auto& w : info.warnings) std::printf("  WARNING: %s\n", w.c_str());
        for (const auto& d : info.drones) {
            const DroneTrack& track = f.drones.at(d.drone);
            std::printf("\nDrone \"%s\"\n", d.drone.c_str());
            for (const auto& l : d.lines)
                std::printf("  %-13s %s  %s\n", l.label.c_str(), label(l.status), l.detail.c_str());
            std::printf("  modes:");
            for (const auto& m : track.modeChanges()) std::printf(" %.1fs=%s", m.t, m.mode.c_str());
            std::printf("\n");
            if (!track.empty()) {
                for (double frac : {0.25, 0.5, 0.75}) {
                    const double t =
                        track.startTime() + frac * (track.endTime() - track.startTime());
                    const Telemetry v = telemetryAt(track, t);
                    auto num = [](const std::optional<double>& x, double scale = 1.0) {
                        return x ? std::to_string(*x * scale).substr(0, 6) : std::string("—");
                    };
                    std::printf(
                        "  t=%6.1fs alt %s m  speed %s m/s  vspeed %s m/s  dist %s m  heading %s "
                        "deg  batt %s%%  mode %s\n",
                        t, num(v.altitude).c_str(), num(v.groundSpeed).c_str(),
                        num(v.verticalSpeed).c_str(), num(v.distanceFromHome).c_str(),
                        num(v.headingRad, 180.0 / kPi).c_str(),
                        num(v.batteryFraction, 100.0).c_str(),
                        track.modeAt(t).value_or("—").c_str());
                }
            }
            if (!track.gps().empty()) {
                const auto& g = track.gps().front().position;
                std::printf("  first GPS fix: %.6f, %.6f, ellipsoid height %.1f m", g.latDeg,
                            g.lonDeg, g.heightM);
                if (track.origin())
                    std::printf("   (local frame origin AMSL %.1f m)", track.origin()->heightM);
                std::printf("\n");
            }
        }
        if (!info.unusedTopics.empty()) {
            std::printf("\nNot used by the viewer:\n");
            for (const auto& t : info.unusedTopics)
                std::printf("  %-50s %-45s %llu\n", t.name.c_str(), t.type.c_str(),
                            (unsigned long long)t.count);
        }
    } catch (const BagError& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    return 0;
}
