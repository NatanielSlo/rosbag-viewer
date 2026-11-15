#include "model/flightmodel.h"

#include <algorithm>
#include <cmath>

namespace viewer {

void DroneTrack::addSample(const PoseSample& s) { samples_.push_back(s); }

void DroneTrack::addModeChange(double t, std::string mode) {
    modes_.push_back({t, std::move(mode)});
}

std::optional<Pose> DroneTrack::poseAt(double t, double maxGap) const {
    if (samples_.empty()) return std::nullopt;
    // first sample with time > t
    const auto next = std::upper_bound(samples_.begin(), samples_.end(), t,
                                       [](double v, const PoseSample& s) { return v < s.t; });
    if (next == samples_.begin()) {  // before the first sample
        if (samples_.front().t - t > maxGap) return std::nullopt;
        return Pose{samples_.front().position, samples_.front().attitude};
    }
    const PoseSample& a = *(next - 1);
    if (next == samples_.end()) {  // after the last sample
        if (t - a.t > maxGap) return std::nullopt;
        return Pose{a.position, a.attitude};
    }
    const PoseSample& b = *next;
    if (b.t - a.t <= maxGap) {
        const double u = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0;
        return Pose{lerp(a.position, b.position, u), slerp(a.attitude, b.attitude, u)};
    }
    // Dropout between a and b: hold the nearer sample if it is close enough, else no data.
    const PoseSample& nearest = (t - a.t <= b.t - t) ? a : b;
    if (std::abs(t - nearest.t) > maxGap) return std::nullopt;
    return Pose{nearest.position, nearest.attitude};
}

std::optional<Geodetic> DroneTrack::geoPointAt(double t, double maxGap) const {
    if (gps_.empty()) return std::nullopt;
    const auto next = std::upper_bound(gps_.begin(), gps_.end(), t,
                                       [](double v, const GpsSample& s) { return v < s.t; });
    auto held = [&](const GpsSample& g) -> std::optional<Geodetic> {
        if (std::abs(t - g.t) > maxGap) return std::nullopt;
        return g.position;
    };
    if (next == gps_.begin()) return held(gps_.front());
    const GpsSample& a = *(next - 1);
    if (next == gps_.end()) return held(a);
    const GpsSample& b = *next;
    if (b.t - a.t > maxGap) return held(t - a.t <= b.t - t ? a : b);
    const double u = b.t > a.t ? (t - a.t) / (b.t - a.t) : 0.0;
    return Geodetic{a.position.latDeg + u * (b.position.latDeg - a.position.latDeg),
                    a.position.lonDeg + u * (b.position.lonDeg - a.position.lonDeg),
                    a.position.heightM + u * (b.position.heightM - a.position.heightM)};
}

std::optional<double> ScalarSeries::valueAt(double t, double maxGap, bool step) const {
    if (samples_.empty()) return std::nullopt;
    const auto next = std::upper_bound(samples_.begin(), samples_.end(), t,
                                       [](double v, const S& s) { return v < s.t; });
    if (next == samples_.begin()) {
        return samples_.front().t - t > maxGap ? std::nullopt : std::optional(samples_.front().v);
    }
    const S& a = *(next - 1);
    if (next == samples_.end()) return t - a.t > maxGap ? std::nullopt : std::optional(a.v);
    const S& b = *next;
    if (step) return t - a.t > maxGap ? std::nullopt : std::optional(a.v);
    if (b.t - a.t <= maxGap) {
        const double u = (b.t > a.t) ? (t - a.t) / (b.t - a.t) : 0.0;
        return a.v + (b.v - a.v) * u;
    }
    const S& nearest = (t - a.t <= b.t - t) ? a : b;
    return std::abs(t - nearest.t) > maxGap ? std::nullopt : std::optional(nearest.v);
}

std::optional<double> ScalarSeries::firstTimeAbove(double threshold) const {
    for (const auto& s : samples_)
        if (s.v > threshold) return s.t;
    return std::nullopt;
}

std::optional<std::string> DroneTrack::modeAt(double t) const {
    const auto next = std::upper_bound(modes_.begin(), modes_.end(), t,
                                       [](double v, const ModeChange& m) { return v < m.t; });
    if (next == modes_.begin()) return std::nullopt;
    return (next - 1)->mode;
}

}  // namespace viewer
