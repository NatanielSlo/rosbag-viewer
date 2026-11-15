#pragma once
// Per-drone flight data and "state at time t" for the views.
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "model/frames.h"
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

// A scalar signal over time (speed, battery, armed, ...).
class ScalarSeries {
public:
    void add(double t, double value) { samples_.push_back({t, value}); }
    bool empty() const { return samples_.empty(); }
    size_t size() const { return samples_.size(); }

    // Linear interpolation between samples at most `maxGap` apart, same dropout rule as
    // DroneTrack::poseAt. With `step` the value of the last sample at or before t is used
    // (for flags like "armed"); it is still reported as missing after a gap longer than maxGap.
    std::optional<double> valueAt(double t, double maxGap = 2.0, bool step = false) const;
    // Time of the first sample whose value is above `threshold`.
    std::optional<double> firstTimeAbove(double threshold) const;

private:
    struct S {
        double t, v;
    };
    std::vector<S> samples_;
};

struct GpsSample {
    double t = 0;
    Geodetic position;  // height above the WGS84 ellipsoid (what the 3D map needs, L8)
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

    // Named scalar signals, created on first use. Names used by the PX4 adapter:
    // "ground_speed" (m/s), "vertical_speed" (m/s, up positive), "battery_remaining" (0..1),
    // "battery_voltage" (V), "armed" (0 or 1).
    ScalarSeries& signal(const std::string& name) { return signals_[name]; }
    const ScalarSeries* findSignal(const std::string& name) const {
        const auto it = signals_.find(name);
        return it == signals_.end() ? nullptr : &it->second;
    }
    const std::map<std::string, ScalarSeries>& signals() const { return signals_; }

    void addGps(const GpsSample& g) { gps_.push_back(g); }
    const std::vector<GpsSample>& gps() const { return gps_; }
    // Interpolated GPS position at time t (ellipsoid height), with the same dropout rule as
    // poseAt: nothing when no GPS sample is within `maxGap` seconds.
    std::optional<Geodetic> geoPointAt(double t, double maxGap = 2.0) const;
    // Reference point of the local frame, when the autopilot reports one. Height is AMSL
    // (PX4 `ref_alt`), unlike the GPS samples, which carry ellipsoid heights.
    void setOrigin(Geodetic g) { origin_ = g; }
    const std::optional<Geodetic>& origin() const { return origin_; }

private:
    std::vector<PoseSample> samples_;
    std::vector<ModeChange> modes_;
    std::map<std::string, ScalarSeries> signals_;
    std::vector<GpsSample> gps_;
    std::optional<Geodetic> origin_;
};

}  // namespace viewer
