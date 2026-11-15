#include "bag/bagsource.h"

#include <algorithm>
#include <filesystem>
#include <memory>

#include <cmath>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <mavros_msgs/msg/state.hpp>
#include <px4_msgs/msg/battery_status.hpp>
#include <px4_msgs/msg/vehicle_attitude.hpp>
#include <px4_msgs/msg/vehicle_global_position.hpp>
#include <px4_msgs/msg/vehicle_local_position.hpp>
#include <px4_msgs/msg/vehicle_status.hpp>
#include <rclcpp/serialization.hpp>
#include <rosbag2_compression/sequential_compression_reader.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/metadata_io.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include "model/frames.h"

namespace viewer {
namespace {

namespace msg = px4_msgs::msg;

// Where the data came from. Both can be in one bag; MAVROS wins (it is what the team's bags
// have, and it is already ENU). The order is the preference.
enum Flavor { Mavros = 0, Px4 = 1, kFlavors = 2 };

// "/drone1/fmu/out/x" and "/drone1/mavros/x" -> "/drone1"; no marker -> not a flight topic.
struct TopicKind {
    bool flight = false;
    Flavor flavor = Mavros;
    std::string ns;
    std::string rest;  // part after the marker, e.g. "local_position/pose" or "vehicle_status_v1"
};

TopicKind classify(const std::string& topic) {
    TopicKind k;
    for (const auto& [marker, flavor] :
         {std::pair<const char*, Flavor>{"/mavros/", Mavros}, {"/fmu/out/", Px4}}) {
        const auto pos = topic.find(marker);
        if (pos == std::string::npos) continue;
        k.flight = true;
        k.flavor = flavor;
        k.ns = topic.substr(0, pos);
        k.rest = topic.substr(pos + std::string(marker).size());
        return k;
    }
    return k;
}

// PX4 versions some topics with a suffix (_v1, _v2 ...). Match the base name with any suffix.
bool isVersioned(const std::string& leaf, const std::string& base) {
    if (leaf == base) return true;
    if (leaf.size() > base.size() + 2 && leaf.compare(0, base.size(), base) == 0 &&
        leaf[base.size()] == '_' && leaf[base.size() + 1] == 'v') {
        return std::all_of(leaf.begin() + base.size() + 2, leaf.end(),
                           [](char c) { return std::isdigit(c); });
    }
    return false;
}

template <class T>
T deserialize(const rosbag2_storage::SerializedBagMessage& m) {
    rclcpp::Serialization<T> ser;
    rclcpp::SerializedMessage sm(*m.serialized_data);
    T out;
    ser.deserialize_message(&sm, &out);
    return out;
}

// Everything one flavor produced for one drone.
struct Source {
    std::vector<std::pair<double, Vec3>> positions;  // ENU
    DroneTrack attitudes;                            // attitude only (position unused)
    std::vector<ModeChange> modes;
    ScalarSeries armed, groundSpeed, verticalSpeed, batteryRemaining, batteryVoltage;
    std::vector<GpsSample> gps;
    std::optional<Geodetic> origin;
    DroneSources topics;  // names, set when the topic is seen (even if it yields no samples)
};

struct RawDrone {
    Source src[kFlavors];
};

// MAVROS mode strings use the PX4 names ("AUTO.MISSION"); show the same names as for PX4.
std::string mavrosModeName(const std::string& m) {
    static const std::map<std::string, std::string> names = {
        {"MANUAL", "MANUAL"},
        {"ALTCTL", "ALTITUDE"},
        {"POSCTL", "POSITION"},
        {"AUTO.MISSION", "MISSION"},
        {"AUTO.LOITER", "HOLD"},
        {"AUTO.RTL", "RETURN"},
        {"ACRO", "ACRO"},
        {"OFFBOARD", "OFFBOARD"},
        {"STABILIZED", "STABILIZED"},
        {"AUTO.TAKEOFF", "TAKEOFF"},
        {"AUTO.LAND", "LAND"},
    };
    const auto it = names.find(m);
    return it == names.end() ? m : it->second;
}

void addMode(Source& s, double t, const std::string& mode) {
    if (s.modes.empty() || s.modes.back().mode != mode) s.modes.push_back({t, mode});
}

}  // namespace

std::string px4ModeName(int s) {
    using S = msg::VehicleStatus;
    switch (s) {
        case S::NAVIGATION_STATE_MANUAL:
            return "MANUAL";
        case S::NAVIGATION_STATE_ALTCTL:
            return "ALTITUDE";
        case S::NAVIGATION_STATE_POSCTL:
            return "POSITION";
        case S::NAVIGATION_STATE_AUTO_MISSION:
            return "MISSION";
        case S::NAVIGATION_STATE_AUTO_LOITER:
            return "HOLD";
        case S::NAVIGATION_STATE_AUTO_RTL:
            return "RETURN";
        case S::NAVIGATION_STATE_ACRO:
            return "ACRO";
        case S::NAVIGATION_STATE_OFFBOARD:
            return "OFFBOARD";
        case S::NAVIGATION_STATE_STAB:
            return "STABILIZED";
        case S::NAVIGATION_STATE_AUTO_TAKEOFF:
            return "TAKEOFF";
        case S::NAVIGATION_STATE_AUTO_LAND:
            return "LAND";
        default:
            return "NAV_STATE_" + std::to_string(s);
    }
}

FlightData loadFlight(const std::string& path) {
    namespace fs = std::filesystem;
    if (!fs::exists(path)) throw BagError("Bag not found: " + path);

    rosbag2_storage::StorageOptions so;
    so.uri = path;  // storage_id left empty: detected from metadata.yaml
    // Compressed bags (metadata.yaml names a compression format) need the compression reader.
    bool compressed = false;
    try {
        compressed = !rosbag2_storage::MetadataIo().read_metadata(path).compression_format.empty();
    } catch (const std::exception&) {
        // No readable metadata.yaml: the plain reader below gives the real error message.
    }
    rosbag2_cpp::Reader reader =
        compressed ? rosbag2_cpp::Reader(
                         std::make_unique<rosbag2_compression::SequentialCompressionReader>())
                   : rosbag2_cpp::Reader();
    try {
        reader.open(so, rosbag2_cpp::ConverterOptions{});
    } catch (const std::exception& e) {
        throw BagError("Cannot open bag " + path + ": " + e.what());
    }

    FlightData out;
    out.path = path;
    const auto meta = reader.get_metadata();
    out.startNs = meta.starting_time.time_since_epoch().count();
    out.durationSeconds = std::chrono::duration<double>(meta.duration).count();
    for (const auto& t : meta.topics_with_message_count)
        out.topics.push_back({t.topic_metadata.name, t.topic_metadata.type, t.message_count});

    std::map<std::string, RawDrone> raw;
    try {
        while (reader.has_next()) {
            const auto m = reader.read_next();
            const double t = double(m->recv_timestamp - out.startNs) * 1e-9;
            const std::string& topic = m->topic_name;
            const TopicKind k = classify(topic);
            if (!k.flight) continue;
            Source& d = raw[k.ns].src[k.flavor];

            if (k.flavor == Px4) {
                if (isVersioned(k.rest, "vehicle_local_position")) {
                    d.topics.position = topic;
                    const auto lp = deserialize<msg::VehicleLocalPosition>(*m);
                    if (!lp.xy_valid || !lp.z_valid) continue;  // estimator not valid yet (L5)
                    d.positions.push_back({t, nedToEnu({lp.x, lp.y, lp.z})});
                    if (lp.v_xy_valid) d.groundSpeed.add(t, std::hypot(lp.vx, lp.vy));
                    if (lp.v_z_valid) d.verticalSpeed.add(t, -lp.vz);  // NED down -> up
                    if (!d.origin && lp.xy_global && lp.z_global)
                        d.origin = Geodetic{lp.ref_lat, lp.ref_lon, lp.ref_alt};
                } else if (isVersioned(k.rest, "vehicle_attitude")) {
                    d.topics.attitude = topic;
                    const auto va = deserialize<msg::VehicleAttitude>(*m);
                    d.attitudes.addSample(
                        {t, {}, attitudeFrdNedToFluEnu({va.q[0], va.q[1], va.q[2], va.q[3]})});
                } else if (isVersioned(k.rest, "vehicle_status")) {
                    d.topics.status = topic;
                    const auto vs = deserialize<msg::VehicleStatus>(*m);
                    addMode(d, t, px4ModeName(vs.nav_state));
                    d.armed.add(t, vs.arming_state == msg::VehicleStatus::ARMING_STATE_ARMED);
                } else if (isVersioned(k.rest, "vehicle_global_position")) {
                    d.topics.gps = topic;
                    const auto gp = deserialize<msg::VehicleGlobalPosition>(*m);
                    if (gp.lat_lon_valid && gp.alt_valid)
                        d.gps.push_back({t, {gp.lat, gp.lon, gp.alt_ellipsoid}});
                } else if (isVersioned(k.rest, "battery_status")) {
                    d.topics.battery = topic;
                    const auto bs = deserialize<msg::BatteryStatus>(*m);
                    // PX4 marks "unknown" with -1 (remaining) and 0 (voltage)
                    if (bs.remaining >= 0.f && bs.remaining <= 1.f)
                        d.batteryRemaining.add(t, bs.remaining);
                    if (bs.voltage_v > 0.f) d.batteryVoltage.add(t, bs.voltage_v);
                }
            } else {  // MAVROS: already ENU / FLU
                if (k.rest == "local_position/pose") {
                    d.topics.position = d.topics.attitude = topic;
                    const auto ps = deserialize<geometry_msgs::msg::PoseStamped>(*m);
                    const auto& p = ps.pose.position;
                    const auto& q = ps.pose.orientation;
                    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) continue;
                    d.positions.push_back({t, {p.x, p.y, p.z}});
                    d.attitudes.addSample({t, {}, normalized({q.w, q.x, q.y, q.z})});
                } else if (k.rest == "local_position/velocity_local") {
                    const auto ts = deserialize<geometry_msgs::msg::TwistStamped>(*m);
                    const auto& v = ts.twist.linear;
                    d.groundSpeed.add(t, std::hypot(v.x, v.y));
                    d.verticalSpeed.add(t, v.z);
                } else if (k.rest == "state") {
                    d.topics.status = topic;
                    const auto st = deserialize<mavros_msgs::msg::State>(*m);
                    if (!st.mode.empty()) addMode(d, t, mavrosModeName(st.mode));
                    d.armed.add(t, st.armed);
                } else if (k.rest == "battery") {
                    d.topics.battery = topic;
                    const auto bs = deserialize<sensor_msgs::msg::BatteryState>(*m);
                    if (std::isfinite(bs.percentage) && bs.percentage >= 0.f &&
                        bs.percentage <= 1.f)
                        d.batteryRemaining.add(t, bs.percentage);
                    if (std::isfinite(bs.voltage) && bs.voltage > 0.f)
                        d.batteryVoltage.add(t, bs.voltage);
                } else if (k.rest == "global_position/global") {
                    d.topics.gps = topic;
                    const auto fix = deserialize<sensor_msgs::msg::NavSatFix>(*m);
                    // status < 0 means no fix; the altitude is already above the ellipsoid
                    if (fix.status.status >= 0 && std::isfinite(fix.latitude) &&
                        std::isfinite(fix.longitude) && std::isfinite(fix.altitude))
                        d.gps.push_back({t, {fix.latitude, fix.longitude, fix.altitude}});
                }
            }
        }
    } catch (const std::exception& e) {
        // A damaged bag still gives what was readable before the damage.
        if (raw.empty()) throw BagError("Cannot read bag " + path + ": " + e.what());
    }

    // For every kind of data take the first flavor (in preference order) that has any.
    auto pick = [](RawDrone& d, auto has) -> Source* {
        for (auto& s : d.src)
            if (has(s)) return &s;
        return nullptr;
    };
    for (auto& [ns, d] : raw) {
        DroneTrack track;
        DroneSources names;
        Source* pos = pick(d, [](const Source& s) { return !s.positions.empty(); });
        if (pos) {
            Source* att = pick(d, [](const Source& s) { return !s.attitudes.empty(); });
            if (att == nullptr || att->attitudes.empty()) att = nullptr;
            for (const auto& [t, p] : pos->positions) {
                // attitude may come on its own topic, possibly from the other flavor
                const auto a = att ? att->attitudes.poseAt(t) : std::nullopt;
                track.addSample({t, p, a ? a->attitude : Quat{}});
            }
            if (pos->origin) track.setOrigin(*pos->origin);
        }
        // names of topics that were seen, even when no usable samples came out of them
        for (const auto& s : d.src) {
            if (names.position.empty()) names.position = s.topics.position;
            if (names.attitude.empty()) names.attitude = s.topics.attitude;
            if (names.status.empty()) names.status = s.topics.status;
            if (names.gps.empty()) names.gps = s.topics.gps;
            if (names.battery.empty()) names.battery = s.topics.battery;
        }
        if (Source* s = pick(d, [](const Source& s) { return !s.modes.empty(); }))
            for (auto& c : s->modes) track.addModeChange(c.t, std::move(c.mode));
        if (Source* s = pick(d, [](const Source& s) { return !s.armed.empty(); }))
            track.signal("armed") = std::move(s->armed);
        if (Source* s = pick(d, [](const Source& s) { return !s.groundSpeed.empty(); }))
            track.signal("ground_speed") = std::move(s->groundSpeed);
        if (Source* s = pick(d, [](const Source& s) { return !s.verticalSpeed.empty(); }))
            track.signal("vertical_speed") = std::move(s->verticalSpeed);
        if (Source* s = pick(d, [](const Source& s) { return !s.batteryRemaining.empty(); }))
            track.signal("battery_remaining") = std::move(s->batteryRemaining);
        if (Source* s = pick(d, [](const Source& s) { return !s.batteryVoltage.empty(); }))
            track.signal("battery_voltage") = std::move(s->batteryVoltage);
        if (Source* s = pick(d, [](const Source& s) { return !s.gps.empty(); }))
            for (const auto& g : s->gps) track.addGps(g);
        if (!track.origin()) {
            if (Source* s = pick(d, [](const Source& s) { return s.origin.has_value(); }))
                track.setOrigin(*s->origin);
        }
        if (!track.empty() || !track.modeChanges().empty() || !track.allSignals().empty()) {
            out.drones.emplace(ns, std::move(track));
            out.sources.emplace(ns, names);
        }
    }
    return out;
}

}  // namespace viewer
