// Writes a synthetic PX4-style flight as a rosbag2 (sqlite3 or mcap), for tests and demos.
//   make_test_bag --out DIR [--storage sqlite3|mcap] [--shape circle|ellipse|eight]
//                 [--duration 60] [--gps LAT LON ALT_AMSL] [--namespace /drone1]
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>

#include <px4_msgs/msg/vehicle_attitude.hpp>
#include <px4_msgs/msg/vehicle_global_position.hpp>
#include <px4_msgs/msg/vehicle_local_position.hpp>
#include <px4_msgs/msg/vehicle_status.hpp>
#include <rclcpp/serialization.hpp>
#include <rclcpp/time.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_storage/storage_options.hpp>

#include "testing/shapes.h"

using namespace viewer::testing;
namespace msg = px4_msgs::msg;

namespace {

struct Options {
    std::string out, storage = "sqlite3", ns;
    Shape shape = Shape::Circle;
    double duration = 60;
    bool gps = false;
    double lat = 48.2655, lon = 11.6710, alt = 480.0;
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
    rosbag2_cpp::Writer writer;
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
