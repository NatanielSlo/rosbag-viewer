"""Synthetic rosbag2 (sqlite3) builder, so tests need no real recordings."""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np
import pytest
from rosbags.rosbag2 import Writer
from rosbags.typesys import Stores, get_typestore

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "backend"))
import bag_reader  # noqa: E402

TS = get_typestore(Stores.ROS2_HUMBLE)
T = TS.types
SEC = 1_000_000_000
T0 = 1_700_000_000 * SEC


def _header(t_ns: int, frame: str = "map"):
    return T["std_msgs/msg/Header"](
        stamp=T["builtin_interfaces/msg/Time"](sec=t_ns // SEC, nanosec=t_ns % SEC),
        frame_id=frame)


def pose_msg(t_ns: int, x: float, y: float, z: float):
    return T["geometry_msgs/msg/PoseStamped"](
        header=_header(t_ns),
        pose=T["geometry_msgs/msg/Pose"](
            position=T["geometry_msgs/msg/Point"](x=x, y=y, z=z),
            orientation=T["geometry_msgs/msg/Quaternion"](x=0.0, y=0.0, z=0.0, w=1.0)))


def odom_msg(t_ns: int, x: float, y: float, z: float):
    cov = np.zeros(36)
    p = pose_msg(t_ns, x, y, z)
    return T["nav_msgs/msg/Odometry"](
        header=_header(t_ns, "camera_init"), child_frame_id="body",
        pose=T["geometry_msgs/msg/PoseWithCovariance"](pose=p.pose, covariance=cov),
        twist=T["geometry_msgs/msg/TwistWithCovariance"](
            twist=T["geometry_msgs/msg/Twist"](
                linear=T["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=0.0),
                angular=T["geometry_msgs/msg/Vector3"](x=0.0, y=0.0, z=0.0)),
            covariance=cov))


def string_msg(text: str):
    return T["std_msgs/msg/String"](data=text)


def jpeg_msg(t_ns: int, payload: bytes):
    return T["sensor_msgs/msg/CompressedImage"](
        header=_header(t_ns, "cam"), format="jpeg",
        data=np.frombuffer(payload, dtype=np.uint8))


def make_bag(path: Path, topics: dict[str, tuple[str, list[tuple[int, object]]]]) -> Path:
    """topics: {topic: (msgtype, [(t_ns, msg), ...])}"""
    with Writer(path, version=9) as w:
        conns = {name: w.add_connection(name, mt, typestore=TS)
                 for name, (mt, _) in topics.items()}
        events = sorted(((t, name, m) for name, (_, msgs) in topics.items() for t, m in msgs),
                        key=lambda e: e[0])
        for t, name, m in events:
            w.write(conns[name], t, TS.serialize_cdr(m, conns[name].msgtype))
    return path


def circle(n: int = 100, hz: float = 10.0, radius: float = 5.0):
    """n pose samples flying a circle at 2 m altitude, `hz` Hz."""
    out = []
    for i in range(n):
        t = T0 + int(i / hz * SEC)
        a = i / n * 2 * np.pi
        out.append((t, pose_msg(t, radius * np.cos(a), radius * np.sin(a), 2.0)))
    return out


@pytest.fixture(autouse=True)
def _isolated_cache(tmp_path, monkeypatch):
    """Never touch the user's real ~/.cache during tests."""
    monkeypatch.setattr(bag_reader, "CACHE_DIR", tmp_path / "_cache")
    bag_reader._flight_cached.cache_clear()
    bag_reader._frame_cached.cache_clear()
    bag_reader._metadata.cache_clear()
    bag_reader._dbs.clear()
    bag_reader._indexes.clear()
    yield


@pytest.fixture
def flight_bag(tmp_path):
    poses = circle(100)
    modes = [(T0, string_msg("POSCTL")),
             (T0 + 3 * SEC, string_msg("OFFBOARD\nArming denied: fix health first")),
             (T0 + 3 * SEC + 1, string_msg("OFFBOARD\nArming denied: fix health first")),
             (T0 + 6 * SEC, string_msg("AUTO.LOITER"))]
    return make_bag(tmp_path / "flight", {
        "/mavros/local_position/pose": ("geometry_msgs/msg/PoseStamped", poses),
        "/flight_mode": ("std_msgs/msg/String", modes),
    })
