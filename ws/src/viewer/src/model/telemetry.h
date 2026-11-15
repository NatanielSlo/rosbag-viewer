#pragma once
// The values shown next to the instruments, at one moment of the replay. A value is empty when
// there is no data near that time (the UI shows "—").
#include <optional>

#include "model/flightmodel.h"

namespace viewer {

struct Telemetry {
    std::optional<double> altitude;          // m above the start point (Up)
    std::optional<double> groundSpeed;       // m/s
    std::optional<double> verticalSpeed;     // m/s, climbing is positive
    std::optional<double> distanceFromHome;  // m, horizontal, from the origin of the local frame
    std::optional<double> headingRad;        // radians clockwise from North, [0, 2pi)
    std::optional<double> batteryFraction;   // 0..1
    std::optional<double> batteryVoltage;    // V
    std::optional<bool> armed;
    std::optional<double> flightTime;  // s since the first arming; empty before it
};

Telemetry telemetryAt(const DroneTrack& track, double t, double maxGap = 2.0);

}  // namespace viewer
