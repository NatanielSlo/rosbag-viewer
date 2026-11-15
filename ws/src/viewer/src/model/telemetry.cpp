#include "model/telemetry.h"

#include <cmath>

#include "model/frames.h"

namespace viewer {

Telemetry telemetryAt(const DroneTrack& track, double t, double maxGap) {
    Telemetry out;
    if (const auto pose = track.poseAt(t, maxGap)) {
        out.altitude = pose->position.z;
        out.distanceFromHome = std::hypot(pose->position.x, pose->position.y);
        out.headingRad = headingFromAttitude(pose->attitude);
    }
    auto value = [&](const char* name, bool step = false) -> std::optional<double> {
        const ScalarSeries* s = track.findSignal(name);
        return s ? s->valueAt(t, maxGap, step) : std::nullopt;
    };
    out.groundSpeed = value("ground_speed");
    out.verticalSpeed = value("vertical_speed");
    out.batteryFraction = value("battery_remaining");
    out.batteryVoltage = value("battery_voltage");
    if (const auto armed = value("armed", /*step=*/true)) out.armed = *armed > 0.5;
    if (const ScalarSeries* a = track.findSignal("armed")) {
        const auto first = a->firstTimeAbove(0.5);
        if (first && t >= *first) out.flightTime = t - *first;
    }
    return out;
}

}  // namespace viewer
