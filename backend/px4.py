"""Adapter for bags recorded straight from PX4's uXRCE-DDS topics (/fmu/out/*,
px4_msgs). PX4 uses NED world / FRD body frames; the viewer uses ENU / FLU
(the MAVROS/ROS convention), so everything is converted here.

Message definitions come embedded in mcap bags, so px4_msgs does not need to
be installed. (sqlite3 bags do not embed them; those are not supported here.)
"""
from __future__ import annotations

import re
from typing import Any

import numpy as np

# Topic names carry a version suffix that changes between PX4 releases
# (vehicle_local_position_v1, vehicle_status_v1, ...). Match the base name
# with or without "_v<N>".
LOCAL_BASE = "/fmu/out/vehicle_local_position"
ATTITUDE_BASE = "/fmu/out/vehicle_attitude"
GLOBAL_BASE = "/fmu/out/vehicle_global_position"
STATUS_BASE = "/fmu/out/vehicle_status"

# px4_msgs/msg/VehicleStatus NAVIGATION_STATE_*  ->  names used by the viewer
NAV_STATES = {
    0: "MANUAL", 1: "ALTCTL", 2: "POSCTL", 3: "AUTO.MISSION", 4: "AUTO.LOITER",
    5: "AUTO.RTL", 6: "POSCTL", 10: "ACRO", 12: "DESCEND", 13: "TERMINATION",
    14: "OFFBOARD", 15: "STABILIZED", 17: "AUTO.TAKEOFF", 18: "AUTO.LAND",
    19: "AUTO.FOLLOW", 20: "AUTO.PRECLAND", 21: "ORBIT", 22: "AUTO.TAKEOFF",
}
ARMING_ARMED = 2  # VehicleStatus.ARMING_STATE_ARMED

R_ENU_NED = np.array([[0, 1, 0], [1, 0, 0], [0, 0, -1]], dtype=float)  # (N,E,D) -> (E,N,U)
R_FRD_FLU = np.diag([1.0, -1.0, -1.0])                                  # body: FRD <-> FLU


def find_connection(reader, base: str):
    """Connection named `base` or `base_v<N>` (highest version) that has data."""
    pat = re.compile(re.escape(base) + r"(_v(\d+))?$")
    hits = [(int(m.group(2) or 0), c) for c in reader.connections
            if (m := pat.match(c.topic)) and c.msgcount > 0]
    return max(hits, key=lambda h: h[0])[1] if hits else None


# ----------------------------- rotation maths ------------------------------

def quat_to_mat(w: float, x: float, y: float, z: float) -> np.ndarray:
    n = w * w + x * x + y * y + z * z
    if n < 1e-12:
        return np.eye(3)
    s = 2.0 / n
    return np.array([
        [1 - s * (y * y + z * z), s * (x * y - z * w), s * (x * z + y * w)],
        [s * (x * y + z * w), 1 - s * (x * x + z * z), s * (y * z - x * w)],
        [s * (x * z - y * w), s * (y * z + x * w), 1 - s * (x * x + y * y)]])


def mat_to_quat(R: np.ndarray) -> tuple[float, float, float, float]:
    """Rotation matrix -> (x, y, z, w), the order three.js / ROS use."""
    t = np.trace(R)
    if t > 0:
        s = np.sqrt(t + 1.0) * 2
        w, x, y, z = 0.25 * s, (R[2, 1] - R[1, 2]) / s, (R[0, 2] - R[2, 0]) / s, (R[1, 0] - R[0, 1]) / s
    elif R[0, 0] > R[1, 1] and R[0, 0] > R[2, 2]:
        s = np.sqrt(1.0 + R[0, 0] - R[1, 1] - R[2, 2]) * 2
        w, x, y, z = (R[2, 1] - R[1, 2]) / s, 0.25 * s, (R[0, 1] + R[1, 0]) / s, (R[0, 2] + R[2, 0]) / s
    elif R[1, 1] > R[2, 2]:
        s = np.sqrt(1.0 + R[1, 1] - R[0, 0] - R[2, 2]) * 2
        w, x, y, z = (R[0, 2] - R[2, 0]) / s, (R[0, 1] + R[1, 0]) / s, 0.25 * s, (R[1, 2] + R[2, 1]) / s
    else:
        s = np.sqrt(1.0 + R[2, 2] - R[0, 0] - R[1, 1]) * 2
        w, x, y, z = (R[1, 0] - R[0, 1]) / s, (R[0, 2] + R[2, 0]) / s, (R[1, 2] + R[2, 1]) / s, 0.25 * s
    return float(x), float(y), float(z), float(w)


def ned_frd_to_enu_flu(q_wxyz) -> tuple[float, float, float, float]:
    """PX4 attitude quaternion (w,x,y,z; FRD body in NED) -> (x,y,z,w) of the
    FLU body in ENU."""
    R = R_ENU_NED @ quat_to_mat(*[float(v) for v in q_wxyz]) @ R_FRD_FLU
    return mat_to_quat(R)


# ------------------------------- readers -----------------------------------

def read_pose(reader, local_conn, att_conn, hz: float) -> dict[str, list] | None:
    """Pose series in ENU from vehicle_local_position (+ vehicle_attitude)."""
    att_t: np.ndarray | None = None
    att_q: list = []
    if att_conn is not None:
        ts, qs = [], []
        for c, t, raw in reader.messages(connections=[att_conn]):
            ts.append(t)
            qs.append(reader.deserialize(raw, c.msgtype).q)
        att_t, att_q = np.asarray(ts), qs

    out: dict[str, list] = {k: [] for k in ("t", "x", "y", "z", "qx", "qy", "qz", "qw")}
    min_dt, last = 1.0 / hz, -1e9
    for c, ts_ns, raw in reader.messages(connections=[local_conn]):
        t = (ts_ns - reader.start_time) / 1e9
        if t - last < min_dt:
            continue
        m = reader.deserialize(raw, c.msgtype)
        if not (m.xy_valid and m.z_valid):   # EKF not converged yet: garbage values
            continue
        last = t
        if att_t is not None and len(att_t):
            i = int(np.clip(np.searchsorted(att_t, ts_ns), 0, len(att_t) - 1))
            qx, qy, qz, qw = ned_frd_to_enu_flu(att_q[i])
        else:
            qx, qy, qz, qw = ned_frd_to_enu_flu(_yaw_only(m.heading))
        out["t"].append(round(t, 3))
        # NED (N,E,D) -> ENU (E,N,U)
        for k, v in (("x", m.y), ("y", m.x), ("z", -m.z),
                     ("qx", qx), ("qy", qy), ("qz", qz), ("qw", qw)):
            out[k].append(round(float(v), 4))
    return out if out["t"] else None


def _yaw_only(heading: float):
    """Fallback orientation from heading alone (rad, clockwise from north)."""
    return (np.cos(heading / 2), 0.0, 0.0, np.sin(heading / 2))


def read_geo(reader, conn, hz: float) -> dict[str, list] | None:
    """lat/lon plus both altitudes: `alt` is above mean sea level,
    `alt_ellipsoid` above the WGS84 ellipsoid (what Cesium uses)."""
    out: dict[str, list] = {k: [] for k in ("t", "lat", "lon", "alt", "alt_ellipsoid")}
    min_dt, last = 1.0 / hz, -1e9
    for c, ts, raw in reader.messages(connections=[conn]):
        t = (ts - reader.start_time) / 1e9
        if t - last < min_dt:
            continue
        m = reader.deserialize(raw, c.msgtype)
        if not m.lat_lon_valid:
            continue
        last = t
        out["t"].append(round(t, 3))
        out["lat"].append(round(float(m.lat), 7))
        out["lon"].append(round(float(m.lon), 7))
        out["alt"].append(round(float(m.alt), 2))
        out["alt_ellipsoid"].append(round(float(m.alt_ellipsoid), 2))
    return out if out["t"] else None


def read_modes(reader, conn) -> tuple[list[dict], list[dict]]:
    """(mode changes, arm/disarm events) from vehicle_status."""
    modes: list[dict] = []
    events: list[dict] = []
    prev_mode = prev_armed = None
    for c, ts, raw in reader.messages(connections=[conn]):
        m = reader.deserialize(raw, c.msgtype)
        t = round((ts - reader.start_time) / 1e9, 3)
        mode = NAV_STATES.get(int(m.nav_state), f"STATE_{int(m.nav_state)}")
        if mode != prev_mode:
            modes.append({"t": t, "value": mode})
            prev_mode = mode
        armed = int(m.arming_state) == ARMING_ARMED
        if armed != prev_armed:
            events.append({"t": t, "value": "Armed" if armed else "Disarmed"})
            prev_armed = armed
    return modes, events
