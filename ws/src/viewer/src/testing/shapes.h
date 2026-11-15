#pragma once
// Ground-truth flights for tests and for the synthetic bag generator. The shapes are the ones
// from the team onboarding (Task 2): path (x(t), y(t)) in PX4's NED frame, x = North, y = East.
#include <cmath>

namespace viewer::fixtures {

enum class Shape { Circle, Ellipse, Eight };

struct ShapeState {
    double north = 0, east = 0;    // metres
    double vNorth = 0, vEast = 0;  // m/s
    double yaw = 0;                // PX4 yaw: radians clockwise from North, along the velocity
};

inline ShapeState shapeAt(Shape s, double tau, double radius = 3.0, double omega = 0.4) {
    const double w = omega * tau;
    ShapeState r;
    switch (s) {
        case Shape::Circle:
            r.north = radius * std::cos(w);
            r.east = radius * std::sin(w);
            r.vNorth = -radius * omega * std::sin(w);
            r.vEast = radius * omega * std::cos(w);
            break;
        case Shape::Ellipse: {  // semi-axes a = R, b = R / 2
            const double a = radius, b = radius / 2;
            r.north = a * std::cos(w);
            r.east = b * std::sin(w);
            r.vNorth = -a * omega * std::sin(w);
            r.vEast = b * omega * std::cos(w);
            break;
        }
        case Shape::Eight:
            r.north = radius * std::sin(w);
            r.east = (radius / 2) * std::sin(2 * w);
            r.vNorth = radius * omega * std::cos(w);
            r.vEast = radius * omega * std::cos(2 * w);
            break;
    }
    r.yaw = std::atan2(r.vEast, r.vNorth);
    return r;
}

// Timeline of a generated flight (seconds since the start of the bag).
struct FlightPlan {
    double armAt = 1.0;      // MANUAL -> armed + OFFBOARD
    double climbAt = 2.0;    // start climbing
    double shapeAt = 6.0;    // start flying the shape
    double landAt = 0.0;     // set from the duration
    double altitude = 2.0;   // metres above the start point
    double climbRate = 1.0;  // m/s
};

}  // namespace viewer::fixtures
