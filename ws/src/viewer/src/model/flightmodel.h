#pragma once
// Per-drone flight data and "state at time t" for the views.
#include <optional>
#include <string>
#include <vector>

#include "model/geometry.h"

namespace viewer {

struct PoseSample {
    double t = 0;   // seconds since the start of the recording
    Vec3 position;  // ENU metres relative to the start point
    Quat attitude;  // FLU body -> ENU world
};

struct Pose {
    Vec3 position;
    Quat attitude;
};

struct ModeChange {
    double t = 0;
    std::string mode;  // e.g. "OFFBOARD", "MISSION", "MANUAL"
};

class DroneTrack {
public:
    // Samples must be added in time order.
    void addSample(const PoseSample& s);
    void addModeChange(double t, std::string mode);

    bool empty() const { return samples_.empty(); }
    const std::vector<PoseSample>& samples() const { return samples_; }
    double startTime() const { return samples_.empty() ? 0 : samples_.front().t; }
    double endTime() const { return samples_.empty() ? 0 : samples_.back().t; }

    // Interpolated pose at time t. Returns nothing when there is no data near t: the nearest
    // sample is further than `maxGap` seconds away. (The UI shows "—" for such values.)
    // Between two samples more than `maxGap` apart the nearest sample is held instead of
    // inventing a straight line across a data dropout.
    std::optional<Pose> poseAt(double t, double maxGap = 2.0) const;

    // Flight mode in force at time t (the last change at or before t).
    std::optional<std::string> modeAt(double t) const;
    const std::vector<ModeChange>& modeChanges() const { return modes_; }

private:
    std::vector<PoseSample> samples_;
    std::vector<ModeChange> modes_;
};

}  // namespace viewer
