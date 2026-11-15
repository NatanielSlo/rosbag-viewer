// Writes a synthetic PX4-style flight as a rosbag2 (sqlite3 or mcap), for tests and demos.
//   make_test_bag --out DIR [--storage sqlite3|mcap] [--shape circle|ellipse|eight]
//                 [--duration 60] [--gps LAT LON ALT_AMSL] [--namespace /drone1]
//                 [--compress] [--split-bytes N] [--flavor px4|mavros|both]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <geometry_msgs/msg/pose_stamped.hpp>
#include <geometry_msgs/msg/twist_stamped.hpp>
#include <mavros_msgs/msg/state.hpp>
#include <px4_msgs/msg/battery_status.hpp>
#include <px4_msgs/msg/vehicle_attitude.hpp>
#include <px4_msgs/msg/vehicle_global_position.hpp>
#include <px4_msgs/msg/vehicle_local_position.hpp>
#include <px4_msgs/msg/vehicle_status.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/time.hpp>
#include <rosbag2_compression/sequential_compression_writer.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <sensor_msgs/msg/battery_state.hpp>
#include <sensor_msgs/msg/nav_sat_fix.hpp>

#include "testing/shapes.h"

using namespace viewer::fixtures;
namespace msg = px4_msgs::msg;

namespace {

struct Options {
    std::string out, storage = "sqlite3", ns;
    Shape shape = Shape::Circle;
    double duration = 60;
    bool gps = false;
    double lat = 48.2655, lon = 11.6710, alt = 480.0;
    bool compress = false;
    bool mavros = false, both = false;
    uint64_t splitBytes = 0;
};

template <class T>
void put(rosbag2_cpp::Writer& w, const T& m, const std::string& topic, int64_t ns) {
    w.write(m, topic, rclcpp::Time(ns, RCL_SYSTEM_TIME));
}

}  // namespace

int main(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() {
            return i + 1 < argc ? argv[++i]
                                : (std::fprintf(stderr, "missing value for %s\n", a.c_str()),
                                   std::exit(2), "");
        };
        if (a == "--out")
            o.out = next();
        else if (a == "--storage")
            o.storage = next();
        else if (a == "--namespace")
            o.ns = next();
        else if (a == "--flavor") {
            const std::string f = next();
            o.mavros = f == "mavros" || f == "both";
            o.both = f == "both";
        } else if (a == "--compress")
            o.compress = true;
        else if (a == "--split-bytes")
            o.splitBytes = std::strtoull(next(), nullptr, 10);
        else if (a == "--duration")
            o.duration = std::atof(next());
        else if (a == "--shape") {
            const std::string s = next();
            o.shape = s == "ellipse" ? Shape::Ellipse : s == "eight" ? Shape::Eight : Shape::Circle;
        } else if (a == "--gps") {
            o.gps = true;
            o.lat = std::atof(next());
            o.lon = std::atof(next());
            o.alt = std::atof(next());
        } else {
            std::fprintf(stderr, "unknown option %s\n", a.c_str());
            return 2;
        }
    }
    if (o.out.empty()) {
        std::fprintf(stderr, "usage: make_test_bag --out DIR [options]\n");
        return 2;
    }

    FlightPlan plan;
    plan.landAt = o.duration - 5.0;

    rosbag2_storage::StorageOptions so;
    so.uri = o.out;
    so.storage_id = o.storage;
    so.max_bagfile_size = o.splitBytes;  // 0 = one file
    std::unique_ptr<rosbag2_cpp::Writer> writerPtr;
    if (o.compress) {
        rosbag2_compression::CompressionOptions co{"zstd",
                                                   rosbag2_compression::CompressionMode::FILE};
        writerPtr = std::make_unique<rosbag2_cpp::Writer>(
            std::make_unique<rosbag2_compression::SequentialCompressionWriter>(co));
    } else {
        writerPtr = std::make_unique<rosbag2_cpp::Writer>();
    }
    rosbag2_cpp::Writer& writer = *writerPtr;
    writer.open(so);

    const int64_t t0 = 1'780'000'000LL * 1'000'000'000LL;  // arbitrary fixed start (2026)
    const ShapeState start = shapeAt(o.shape, 0);
    const double deg = 180.0 / M_PI;

    for (int step = 0; step * 0.02 <= o.duration; ++step) {  // 50 Hz
        const double t = step * 0.02;
        const int64_t ns = t0 + int64_t(step) * 20'000'000LL;
        const uint64_t us = uint64_t(step) * 20'000ULL;

        // position and attitude truth
        double alt = 0, north = start.north, east = start.east, vN = 0, vE = 0, vD = 0,
               yaw = shapeAt(o.shape, 0).yaw;
        if (t >= plan.climbAt) {
            const double up = std::min(plan.altitude, (t - plan.climbAt) * plan.climbRate);
            alt = up;
            vD = (up < plan.altitude && t < plan.landAt) ? -plan.climbRate : 0;
        }
        if (t >= plan.shapeAt && t < plan.landAt) {
            const ShapeState s = shapeAt(o.shape, t - plan.shapeAt);
            north = s.north;
            east = s.east;
            vN = s.vNorth;
            vE = s.vEast;
            yaw = s.yaw;
        } else if (t >= plan.landAt) {  // hold position and descend
            const ShapeState s = shapeAt(o.shape, plan.landAt - plan.shapeAt);
            north = s.north;
            east = s.east;
            yaw = s.yaw;
            alt = std::max(0.0, plan.altitude - (t - plan.landAt) * 0.5);
            vD = alt > 0 ? 0.5 : 0;
        }

        if (o.mavros) {  // MAVROS flavor: ENU / FLU, standard ROS message types
            const rclcpp::Time stamp(ns, RCL_SYSTEM_TIME);
            const double yawEnu = M_PI / 2 - yaw;  // PX4 yaw is clockwise from North
            geometry_msgs::msg::PoseStamped ps;
            ps.header.stamp = stamp;
            ps.header.frame_id = "map";
            ps.pose.position.x = east;
            ps.pose.position.y = north;
            ps.pose.position.z = alt;
            ps.pose.orientation.w = std::cos(yawEnu / 2);
            ps.pose.orientation.z = std::sin(yawEnu / 2);
            put(writer, ps, o.ns + "/mavros/local_position/pose", ns);

            geometry_msgs::msg::TwistStamped ts;
            ts.header.stamp = stamp;
            ts.twist.linear.x = vE;
            ts.twist.linear.y = vN;
            ts.twist.linear.z = -vD;
            put(writer, ts, o.ns + "/mavros/local_position/velocity_local", ns);

            if (o.gps && step % 5 == 0) {  // 10 Hz
                sensor_msgs::msg::NavSatFix fix;
                fix.header.stamp = stamp;
                fix.status.status = sensor_msgs::msg::NavSatStatus::STATUS_FIX;
                fix.latitude = o.lat + north / 111'320.0;
                fix.longitude = o.lon + east / (111'320.0 * std::cos(o.lat / deg));
                fix.altitude = o.alt + 47.0 + alt;  // MAVROS reports ellipsoid height (L8)
                put(writer, fix, o.ns + "/mavros/global_position/global", ns);
            }
            if (step % 50 == 0) {
                sensor_msgs::msg::BatteryState bs;
                bs.header.stamp = stamp;
                bs.percentage = float(1.0 - 0.2 * t / o.duration);
                bs.voltage = float(16.8 - 1.2 * t / o.duration);
                put(writer, bs, o.ns + "/mavros/battery", ns);
            }
            if (step % 25 == 0) {
                mavros_msgs::msg::State st;
                st.header.stamp = stamp;
                st.connected = true;
                st.armed = t >= plan.armAt && !(t > o.duration - 0.1);
                st.mode = t < plan.armAt ? "MANUAL" : t < plan.landAt ? "OFFBOARD" : "AUTO.LAND";
                put(writer, st, o.ns + "/mavros/state", ns);
            }
            if (!o.both) continue;  // "both": also write the PX4 topics below
        }

        msg::VehicleLocalPosition lp;
        lp.timestamp = lp.timestamp_sample = us;
        lp.xy_valid = lp.z_valid = lp.v_xy_valid = lp.v_z_valid = true;
        lp.x = float(north);
        lp.y = float(east);
        lp.z = float(-alt);
        lp.vx = float(vN);
        lp.vy = float(vE);
        lp.vz = float(vD);
        lp.heading = float(yaw);
        lp.xy_global = lp.z_global = o.gps;
        if (o.gps) {
            lp.ref_lat = o.lat;
            lp.ref_lon = o.lon;
            lp.ref_alt = float(o.alt);
        }
        put(writer, lp, o.ns + "/fmu/out/vehicle_local_position_v1", ns);

        msg::VehicleAttitude va;  // yaw only: rotation about Down; FRD -> NED
        va.timestamp = va.timestamp_sample = us;
        va.q = {float(std::cos(yaw / 2)), 0.f, 0.f, float(std::sin(yaw / 2))};
        put(writer, va, o.ns + "/fmu/out/vehicle_attitude", ns);

        if (o.gps && step % 5 == 0) {  // 10 Hz
            msg::VehicleGlobalPosition gp;
            gp.timestamp = gp.timestamp_sample = us;
            gp.lat = o.lat + north / 111'320.0 * 1.0;
            gp.lon = o.lon + east / (111'320.0 * std::cos(o.lat / deg));
            gp.alt = float(o.alt + alt);
            gp.alt_ellipsoid =
                float(o.alt + 47.0 + alt);  // Munich: ellipsoid is ~47 m above AMSL (L8)
            gp.lat_lon_valid = gp.alt_valid = true;
            put(writer, gp, o.ns + "/fmu/out/vehicle_global_position", ns);
        }
        if (step % 50 == 0) {  // 1 Hz: 100% -> 80% and 16.8 -> 15.6 V over the flight
            msg::BatteryStatus bs;
            bs.timestamp = us;
            bs.connected = true;
            bs.remaining = float(1.0 - 0.2 * t / o.duration);
            bs.voltage_v = float(16.8 - 1.2 * t / o.duration);
            put(writer, bs, o.ns + "/fmu/out/battery_status_v1", ns);
        }
        if (step % 25 == 0) {  // 2 Hz
            msg::VehicleStatus vs;
            vs.timestamp = us;
            const bool armed = t >= plan.armAt && alt + t * 0 >= 0 && !(t > o.duration - 0.1);
            vs.arming_state = armed ? msg::VehicleStatus::ARMING_STATE_ARMED
                                    : msg::VehicleStatus::ARMING_STATE_DISARMED;
            vs.nav_state = t < plan.armAt    ? msg::VehicleStatus::NAVIGATION_STATE_MANUAL
                           : t < plan.landAt ? msg::VehicleStatus::NAVIGATION_STATE_OFFBOARD
                                             : msg::VehicleStatus::NAVIGATION_STATE_AUTO_LAND;
            put(writer, vs, o.ns + "/fmu/out/vehicle_status_v1", ns);
        }
    }
    std::printf("wrote %s (%s, %.0f s)\n", o.out.c_str(), o.storage.c_str(), o.duration);
    return 0;
}
