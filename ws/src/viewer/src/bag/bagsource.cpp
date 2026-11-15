#include "bag/bagsource.h"

#include <algorithm>
#include <filesystem>
#include <memory>

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

#include "model/frames.h"

namespace viewer {
namespace {

namespace msg = px4_msgs::msg;

// "/drone1/fmu/out/vehicle_status_v1" -> "/drone1"; "/fmu/out/..." -> "".
std::string namespaceOf(const std::string& topic) {
    const auto pos = topic.find("/fmu/out/");
    return pos == std::string::npos ? std::string() : topic.substr(0, pos);
}

// PX4 versions some topics with a suffix (_v1, _v2 ...). Match the base name with any suffix.
bool isTopic(const std::string& topic, const std::string& base) {
    const auto pos = topic.rfind('/');
    const std::string leaf = pos == std::string::npos ? topic : topic.substr(pos + 1);
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

struct RawDrone {
    std::vector<std::pair<double, Vec3>> positions;  // ENU
    DroneTrack attitudes;                            // attitude only (position unused)
    std::vector<ModeChange> modes;
    std::map<std::string, ScalarSeries> signals;
    std::vector<GpsSample> gps;
    std::optional<Geodetic> origin;
};

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
            if (namespaceOf(topic).empty() && topic.find("/fmu/out/") == std::string::npos)
                continue;
            RawDrone& d = raw[namespaceOf(topic)];

            if (isTopic(topic, "vehicle_local_position")) {
                const auto lp = deserialize<msg::VehicleLocalPosition>(*m);
                if (!lp.xy_valid || !lp.z_valid) continue;  // estimator not valid yet (L5)
                d.positions.push_back({t, nedToEnu({lp.x, lp.y, lp.z})});
                if (lp.v_xy_valid) d.signals["ground_speed"].add(t, std::hypot(lp.vx, lp.vy));
                if (lp.v_z_valid) d.signals["vertical_speed"].add(t, -lp.vz);  // NED down -> up
                if (!d.origin && lp.xy_global && lp.z_global)
                    d.origin = Geodetic{lp.ref_lat, lp.ref_lon, lp.ref_alt};
            } else if (isTopic(topic, "vehicle_attitude")) {
                const auto va = deserialize<msg::VehicleAttitude>(*m);
                d.attitudes.addSample(
                    {t, {}, attitudeFrdNedToFluEnu({va.q[0], va.q[1], va.q[2], va.q[3]})});
            } else if (isTopic(topic, "vehicle_status")) {
                const auto vs = deserialize<msg::VehicleStatus>(*m);
                const std::string mode = px4ModeName(vs.nav_state);
                if (d.modes.empty() || d.modes.back().mode != mode) d.modes.push_back({t, mode});
                d.signals["armed"].add(t,
                                       vs.arming_state == msg::VehicleStatus::ARMING_STATE_ARMED);
            } else if (isTopic(topic, "vehicle_global_position")) {
                const auto gp = deserialize<msg::VehicleGlobalPosition>(*m);
                if (gp.lat_lon_valid && gp.alt_valid)
                    d.gps.push_back({t, {gp.lat, gp.lon, gp.alt_ellipsoid}});
            } else if (isTopic(topic, "battery_status")) {
                const auto bs = deserialize<msg::BatteryStatus>(*m);
                // PX4 marks "unknown" with -1 (remaining) and 0 (voltage)
                if (bs.remaining >= 0.f && bs.remaining <= 1.f)
                    d.signals["battery_remaining"].add(t, bs.remaining);
                if (bs.voltage_v > 0.f) d.signals["battery_voltage"].add(t, bs.voltage_v);
            }
        }
    } catch (const std::exception& e) {
        // A damaged bag still gives what was readable before the damage.
        if (raw.empty()) throw BagError("Cannot read bag " + path + ": " + e.what());
    }

    for (auto& [ns, d] : raw) {
        DroneTrack track;
        for (const auto& [t, p] : d.positions) {
            const auto att = d.attitudes.poseAt(t);  // attitude arrives on its own topic
            track.addSample({t, p, att ? att->attitude : Quat{}});
        }
        for (auto& c : d.modes) track.addModeChange(c.t, std::move(c.mode));
        for (auto& [name, series] : d.signals) track.signal(name) = std::move(series);
        for (const auto& g : d.gps) track.addGps(g);
        if (d.origin) track.setOrigin(*d.origin);
        if (!track.empty() || !track.modeChanges().empty() || !track.signals().empty())
            out.drones.emplace(ns, std::move(track));
    }
    return out;
}

}  // namespace viewer
