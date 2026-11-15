"""Read a ROS 2 bag (sqlite3 or mcap) and turn it into a small, JSON-friendly
"flight" summary for the viewer.

Two kinds of bags are understood:
  * MAVROS bags (/mavros/*): only standard ROS types are decoded, so no
    mavros_msgs definitions are needed. Already ENU.
  * PX4 bags (/fmu/out/*, px4_msgs, mcap): converted from NED/FRD to ENU/FLU
    in px4.py. Message definitions are embedded in the mcap file.
All coordinates handed to the viewer are ENU.
"""
from __future__ import annotations

import bisect
import functools
import hashlib
import json
import os
import re
import sqlite3
import threading
from pathlib import Path
from typing import Any

import yaml

import px4
from rosbags.highlevel import AnyReader
from rosbags.typesys import Stores, get_typestore

TYPESTORE = get_typestore(Stores.ROS2_HUMBLE)

# Topic preference lists: first one that has data wins.
POSE_TOPICS = ["/mavros/local_position/pose", "/mavros/local_position/odom"]
LIO_TOPICS = ["/Odometry"]
MODE_TOPICS = ["/flight_mode", "/drone/flight_mode", "/diagnostics/flight_mode"]
COURSE_TOPICS = ["/drone/course_state"]
SETPOINT_TOPICS = ["/drone/setpoint_pose"]
VELOCITY_TOPICS = ["/mavros/local_position/velocity_local"]
BATTERY_TOPICS = ["/mavros/battery"]
DETECTION_TOPICS = ["/mission_2/yolo/detections"]
NAVSAT_TOPICS = ["/mavros/global_position/global"]  # sensor_msgs/NavSatFix
CAMERA_SUFFIX = "image_raw/compressed"
GEO_HZ = 10.0

POSE_HZ = 20.0
SETPOINT_HZ = 5.0
VELOCITY_HZ = 5.0
LIO_MAX_GAP_S = 1.0          # a gap longer than this counts as "LIO lost"
LIO_MIN_COVERAGE = 0.6       # warn if LIO covers < 60% of the flight
MOVED_MIN_M = 0.3            # below this the drone "did not move"


_DAMAGED = ("The bag file is damaged and cannot be read (usually the "
            "recording was cut off, e.g. power loss).")


class BagError(Exception):
    """A problem with a bag that can be shown to a non-technical user."""


def _open(path: Path) -> AnyReader:
    if not path.exists():
        raise BagError(f"File or folder not found: {path}")
    if path.is_dir() and not (path / "metadata.yaml").exists() \
            and not list(path.glob("*.mcap")):
        raise BagError(
            "This folder has no metadata.yaml, so it does not look like a "
            "complete ROS 2 bag (the recording may have been interrupted)."
        )
    try:
        reader = AnyReader([path], default_typestore=TYPESTORE)
        reader.open()
    except sqlite3.DatabaseError as exc:
        raise BagError(_DAMAGED) from exc
    except Exception as exc:  # rosbags raises several reader error types
        if any(k in str(exc).lower() for k in ("not a database", "malformed", "database disk image")):
            raise BagError(_DAMAGED) from exc
        if "metadata" in str(exc).lower():
            raise BagError(
                "The bag's metadata.yaml is empty or invalid, so the recording "
                "was most likely interrupted before it was finished."
            ) from exc
        raise BagError(f"Could not open the bag: {exc}") from exc
    return reader


def _conn(reader: AnyReader, topics: list[str]):
    """First connection from `topics` that exists and has messages."""
    for topic in topics:
        for c in reader.connections:
            if c.topic == topic and c.msgcount > 0:
                return c
    return None


def _t(reader: AnyReader, ts: int) -> float:
    return round((ts - reader.start_time) / 1e9, 3)


def _pose_of(msg: Any):
    """PoseStamped has .pose; Odometry has .pose.pose."""
    p = msg.pose
    return p.pose if hasattr(p, "pose") else p


def _read_pose_series(reader: AnyReader, conn, hz: float) -> dict[str, list]:
    out: dict[str, list] = {k: [] for k in ("t", "x", "y", "z", "qx", "qy", "qz", "qw")}
    min_dt, last = 1.0 / hz, -1e9
    for c, ts, raw in reader.messages(connections=[conn]):
        t = (ts - reader.start_time) / 1e9
        if t - last < min_dt:
            continue
        last = t
        p = _pose_of(reader.deserialize(raw, c.msgtype))
        out["t"].append(round(t, 3))
        for k, v in (("x", p.position.x), ("y", p.position.y), ("z", p.position.z),
                     ("qx", p.orientation.x), ("qy", p.orientation.y),
                     ("qz", p.orientation.z), ("qw", p.orientation.w)):
            out[k].append(round(float(v), 4))
    return out


def _read_navsat(reader: AnyReader, conn, hz: float) -> dict[str, list] | None:
    """MAVROS GPS (NavSatFix). `altitude` there is WGS84 ellipsoid height."""
    out: dict[str, list] = {k: [] for k in ("t", "lat", "lon", "alt_ellipsoid")}
    min_dt, last = 1.0 / hz, -1e9
    for c, ts, raw in reader.messages(connections=[conn]):
        t = (ts - reader.start_time) / 1e9
        if t - last < min_dt:
            continue
        m = reader.deserialize(raw, c.msgtype)
        if m.status.status < 0 or m.latitude != m.latitude or m.longitude != m.longitude:
            continue  # no fix / NaN
        last = t
        out["t"].append(round(t, 3))
        out["lat"].append(round(float(m.latitude), 7))
        out["lon"].append(round(float(m.longitude), 7))
        out["alt_ellipsoid"].append(round(float(m.altitude), 2))
    return out if out["t"] else None


def _lio_coverage(lio_t: list[float], pose_t: list[float]) -> float:
    """Fraction of the pose time-span during which LIO samples keep arriving."""
    span = (pose_t[-1] - pose_t[0]) if len(pose_t) > 1 else 0.0
    if span <= 0 or len(lio_t) < 2:
        return 0.0
    covered = sum(b - a for a, b in zip(lio_t, lio_t[1:]) if b - a <= LIO_MAX_GAP_S)
    return min(covered / span, 1.0)


def _frame_id(reader: AnyReader, conn) -> str:
    for c, _, raw in reader.messages(connections=[conn]):
        return reader.deserialize(raw, c.msgtype).header.frame_id
    return ""


def _read_string_changes(reader: AnyReader, conn) -> list[dict]:
    """Only keep the moments where a String topic changes value."""
    out, prev = [], None
    for c, ts, raw in reader.messages(connections=[conn]):
        val = reader.deserialize(raw, c.msgtype).data
        if val != prev:
            out.append({"t": _t(reader, ts), "value": val})
            prev = val
    return out


def _split_modes(changes: list[dict]) -> tuple[list[dict], list[dict]]:
    """Some bags publish "OFFBOARD\\nArming denied: ..." as the mode string.
    The first line is the flight mode; later lines are status messages."""
    modes, status = [], []
    prev_mode, prev_status = None, None
    for ch in changes:
        first, *rest = ch["value"].split("\n")
        first = first.strip()
        if first != prev_mode:
            modes.append({"t": ch["t"], "value": first})
            prev_mode = first
        text = " ".join(x.strip() for x in rest if x.strip())
        if text and text != prev_status:
            status.append({"t": ch["t"], "value": text})
        prev_status = text or prev_status
    return modes, status


def _read_velocity(reader: AnyReader, conn) -> dict[str, list]:
    out = {"t": [], "speed": [], "vz": []}
    min_dt, last = 1.0 / VELOCITY_HZ, -1e9
    for c, ts, raw in reader.messages(connections=[conn]):
        t = (ts - reader.start_time) / 1e9
        if t - last < min_dt:
            continue
        last = t
        v = reader.deserialize(raw, c.msgtype).twist.linear
        out["t"].append(round(t, 3))
        out["speed"].append(round((v.x ** 2 + v.y ** 2) ** 0.5, 3))
        out["vz"].append(round(float(v.z), 3))
    return out


def _read_battery(reader: AnyReader, conn) -> dict[str, list]:
    out = {"t": [], "voltage": [], "percentage": []}
    for c, ts, raw in reader.messages(connections=[conn]):
        m = reader.deserialize(raw, c.msgtype)
        out["t"].append(_t(reader, ts))
        out["voltage"].append(round(float(m.voltage), 2))
        pct = float(m.percentage)
        # sensor_msgs/BatteryState: 0..1, or NaN when unknown
        out["percentage"].append(None if pct != pct else round(pct * 100, 1))
    return out


def _read_detections(reader: AnyReader, conn) -> dict[str, Any]:
    events, frames = [], 0
    for c, ts, raw in reader.messages(connections=[conn]):
        frames += 1
        try:
            data = json.loads(reader.deserialize(raw, c.msgtype).data)
        except (ValueError, TypeError):
            continue
        if data.get("detections"):
            events.append({"t": _t(reader, ts), "detections": data["detections"]})
    return {"frames": frames, "events": events}


def _read_camera_times(reader: AnyReader, conn) -> list[float]:
    return [_t(reader, ts) for _, ts, _ in reader.messages(connections=[conn])]


def _load_flight(path: Path) -> dict[str, Any]:
    """Parse a bag into the dict the frontend consumes."""
    try:
        return _parse_flight(path)
    except sqlite3.DatabaseError as exc:  # corruption found mid-read
        raise BagError(_DAMAGED) from exc


def _parse_flight(path: Path) -> dict[str, Any]:
    reader = _open(path)
    try:
        if reader.message_count == 0:
            raise BagError("This bag is empty (no messages were recorded).")

        warnings: list[str] = []
        flight: dict[str, Any] = {
            "name": path.name,
            "duration": round((reader.end_time - reader.start_time) / 1e9, 3),
            "start_ns": reader.start_time,
            "frame": "ENU",
        }

        pose_conn = _conn(reader, POSE_TOPICS)
        px4_local = px4.find_connection(reader, px4.LOCAL_BASE)
        flight["pose"] = None
        if pose_conn is not None:
            flight["pose"] = _read_pose_series(reader, pose_conn, POSE_HZ)
            flight["pose_topic"] = pose_conn.topic
            flight["pose_frame"] = _frame_id(reader, pose_conn)
            flight["source"] = "mavros"
        elif px4_local is not None:
            flight["pose"] = px4.read_pose(
                reader, px4_local, px4.find_connection(reader, px4.ATTITUDE_BASE), POSE_HZ)
            flight["pose_topic"] = px4_local.topic
            flight["pose_frame"] = "ENU (converted from PX4 NED)"
            flight["source"] = "px4"
        if not flight["pose"]:
            warnings.append("No position data (local_position) in this bag, so "
                            "the flight path cannot be drawn.")

        # GPS track for the map (lat/lon/alt). None for indoor / GPS-less flights.
        geo_conn = px4.find_connection(reader, px4.GLOBAL_BASE)
        nav_conn = _conn(reader, NAVSAT_TOPICS)
        if geo_conn is not None:
            flight["geo"] = px4.read_geo(reader, geo_conn, GEO_HZ)
        elif nav_conn is not None:
            flight["geo"] = _read_navsat(reader, nav_conn, GEO_HZ)
        else:
            flight["geo"] = None

        if flight["pose"]:
            p = flight["pose"]
            moved = max(max(p["x"]) - min(p["x"]), max(p["y"]) - min(p["y"]),
                        max(p["z"]) - min(p["z"]))
            flight["max_extent_m"] = round(moved, 2)
            if moved < MOVED_MIN_M:
                warnings.append(
                    "The drone barely moved in this recording (position changed "
                    f"by {moved:.2f} m). It probably never took off.")

        lio_conn = _conn(reader, LIO_TOPICS)
        if lio_conn is not None:
            flight["lio"] = _read_pose_series(reader, lio_conn, POSE_HZ)
            flight["lio_frame"] = _frame_id(reader, lio_conn)
            if flight["pose"]:
                cov = _lio_coverage(flight["lio"]["t"], flight["pose"]["t"])
                flight["lio_coverage"] = round(cov, 2)
                if cov < LIO_MIN_COVERAGE:
                    warnings.append(
                        f"LIO odometry only covers {cov:.0%} of the flight "
                        "(it probably lost tracking), so it is hidden by default.")

        c = _conn(reader, MODE_TOPICS)
        status_conn = px4.find_connection(reader, px4.STATUS_BASE)
        if c is not None:
            flight["modes"], flight["status_events"] = _split_modes(_read_string_changes(reader, c))
        elif status_conn is not None:
            flight["modes"], flight["status_events"] = px4.read_modes(reader, status_conn)
        else:
            flight["modes"], flight["status_events"] = [], []
        c = _conn(reader, COURSE_TOPICS)
        flight["course"] = _read_string_changes(reader, c) if c else []

        for key, topics, fn in (
            ("setpoint", SETPOINT_TOPICS, lambda r, c: _read_pose_series(r, c, SETPOINT_HZ)),
            ("velocity", VELOCITY_TOPICS, _read_velocity),
            ("battery", BATTERY_TOPICS, _read_battery),
        ):
            c = _conn(reader, topics)
            flight[key] = fn(reader, c) if c else None

        det = _conn(reader, DETECTION_TOPICS)
        flight["detections"] = _read_detections(reader, det) if det else None

        flight["cameras"] = {
            c.topic: _read_camera_times(reader, c)
            for c in reader.connections
            if c.topic.endswith(CAMERA_SUFFIX) and c.msgcount > 0
        }
        flight["warnings"] = warnings
        return flight
    finally:
        reader.close()


# ---------------------------------------------------------------------------
# Camera frames. Opening a bag per request cost ~0.5 s, which made scrubbing
# and playback unusable. For sqlite3 bags we keep one read-only connection per
# db3 file and look the frame up with a single indexed query.
# ---------------------------------------------------------------------------
_db_lock = threading.Lock()
_dbs: dict[str, tuple[sqlite3.Connection, threading.Lock, dict[str, int]]] = {}
_indexes: dict[str, tuple[list[int], list[int]]] = {}


# libyaml's C loader is ~10x faster than the pure-Python one on the big
# metadata.yaml files (hundreds of topics) that rosbag2 writes.
_YamlLoader = getattr(yaml, "CSafeLoader", yaml.SafeLoader)


@functools.lru_cache(maxsize=64)
def _metadata(path: Path, mtime_ns: int) -> dict | None:
    try:
        doc = yaml.load((path / "metadata.yaml").read_text(), Loader=_YamlLoader)
        return doc["rosbag2_bagfile_information"]
    except Exception:
        return None


def _meta(path: Path) -> dict | None:
    try:
        return _metadata(path, (path / "metadata.yaml").stat().st_mtime_ns)
    except OSError:
        return None


def _bag_start_ns(path: Path) -> int | None:
    m = _meta(path)
    try:
        return int(m["starting_time"]["nanoseconds_since_epoch"])
    except Exception:
        return None


def _db_files(path: Path) -> list[Path]:
    return sorted(path.glob("*.db3")) if path.is_dir() else (
        [path] if path.suffix == ".db3" else [])


def _db(file: Path):
    key = str(file)
    with _db_lock:
        entry = _dbs.get(key)
        if entry is None:
            conn = sqlite3.connect(f"file:{file}?mode=ro&immutable=1", uri=True,
                                   check_same_thread=False)
            topics = {name: tid for name, tid in conn.execute("select name, id from topics")}
            entry = _dbs[key] = (conn, threading.Lock(), topics)
        return entry


def _topic_index(conn, lock, tid: int, key: str):
    """(sorted timestamps, row ids) for one topic; built once, then reused."""
    idx = _indexes.get(key)
    if idx is None:
        with lock:
            rows = conn.execute("select timestamp, id from messages where topic_id=? "
                                "order by timestamp", (tid,)).fetchall()
        idx = _indexes[key] = ([r[0] for r in rows], [r[1] for r in rows])
    return idx


def _frame_sqlite(path: Path, topic: str, t: float) -> bytes | None:
    start_ns = _bag_start_ns(path)
    files = _db_files(path)
    if start_ns is None or not files:
        raise LookupError  # not a plain sqlite3 bag: use the generic path
    want = start_ns + int(max(t, 0.0) * 1e9)
    # A split bag (bag_0.db3, bag_1.db3, ...) spreads one topic over several
    # files. Order the pieces by time (not by name: "_10" sorts before "_2")
    # and use the first one that still has a frame at/after `want`.
    parts = []
    for f in files:
        conn, lock, topics = _db(f)
        tid = topics.get(topic)
        if tid is None:
            continue
        stamps, ids = _topic_index(conn, lock, tid, f"{f}|{topic}")
        if stamps:
            parts.append((stamps, ids, conn, lock))
    if not parts:
        return None
    parts.sort(key=lambda p: p[0][0])
    stamps, ids, conn, lock = next((p for p in parts if p[0][-1] >= want), parts[-1])
    i = min(bisect.bisect_left(stamps, want), len(stamps) - 1)  # past the end -> last frame
    with lock:
        row = conn.execute("select data from messages where id=?", (ids[i],)).fetchone()
    return bytes(TYPESTORE.deserialize_cdr(
        bytes(row[0]), "sensor_msgs/msg/CompressedImage").data)


def _frame_generic(path: Path, topic: str, t: float) -> bytes | None:
    reader = _open(path)
    try:
        conn = next((c for c in reader.connections
                     if c.topic == topic and c.msgcount > 0), None)
        if conn is None:
            return None
        start = reader.start_time + int(max(t, 0.0) * 1e9)
        last = None
        for c, ts, raw in reader.messages(connections=[conn]):
            last = raw
            if ts >= start:
                break
        return None if last is None else \
            bytes(reader.deserialize(last, conn.msgtype).data)
    finally:
        reader.close()


@functools.lru_cache(maxsize=256)
def _frame_cached(path: str, topic: str, t_ms: int) -> bytes | None:
    p = Path(path)
    if not topic.endswith(CAMERA_SUFFIX):
        return None
    t = t_ms / 1000.0
    try:
        return _frame_sqlite(p, topic, t)
    except (LookupError, sqlite3.DatabaseError):
        return _frame_generic(p, topic, t)


def load_camera_frame(path: str | Path, topic: str, t: float) -> bytes | None:
    """JPEG bytes of the camera frame at/after time `t` seconds (cached)."""
    return _frame_cached(str(path), topic, int(round(t * 1000)))


# Parsing a big bag from a slow disk (e.g. a USB stick) can take minutes, but
# the result is only a few hundred KB. Keep it on the local disk so every
# later open is instant and the bag itself is not touched (except for frames).
CACHE_VERSION = 2  # bump whenever the shape of the flight dict changes
CACHE_DIR = Path(os.environ.get("ROSBAG_VIEWER_CACHE",
                                Path.home() / ".cache" / "rosbag-viewer" / "flights"))


def _cache_path(path: Path) -> Path | None:
    try:
        parts = [f"v{CACHE_VERSION}", str(path.resolve())]
        files = ([path] if path.is_file() else
                 [path / "metadata.yaml", *sorted(path.glob("*.db3")), *sorted(path.glob("*.mcap"))])
        for f in files:
            st = f.stat()
            parts.append(f"{f.name}:{st.st_size}:{st.st_mtime_ns}")
        return CACHE_DIR / (hashlib.sha1("|".join(parts).encode()).hexdigest() + ".json")
    except OSError:
        return None


def _read_disk_cache(path: Path) -> dict[str, Any] | None:
    c = _cache_path(path)
    try:
        return json.loads(c.read_text()) if c and c.exists() else None
    except (OSError, ValueError):
        return None


def _write_disk_cache(path: Path, flight: dict[str, Any]) -> None:
    c = _cache_path(path)
    if c is None:
        return
    try:
        c.parent.mkdir(parents=True, exist_ok=True)
        tmp = c.with_suffix(".tmp")
        tmp.write_text(json.dumps(flight, separators=(",", ":")))
        tmp.replace(c)  # atomic: never leave a half-written cache file
    except OSError:
        pass  # cache is an optimisation only


@functools.lru_cache(maxsize=16)
def _flight_cached(path: str, mtime_ns: int) -> dict[str, Any] | BagError:
    """Errors are cached too (returned, since lru_cache skips exceptions), so
    clicking a big damaged bag again does not re-read it from the USB stick."""
    p = Path(path)
    flight = _read_disk_cache(p)
    if flight is None:
        try:
            flight = _load_flight(p)
        except BagError as exc:
            return exc
        _write_disk_cache(p, flight)
    return flight


def load_flight(path: str | Path) -> dict[str, Any]:
    """Cached: re-opening the same bag is instant (invalidated if it changes)."""
    p = Path(path)
    meta = p / "metadata.yaml" if p.is_dir() else p
    try:
        mtime = meta.stat().st_mtime_ns
    except OSError:
        mtime = 0
    result = _flight_cached(str(p), mtime)
    if isinstance(result, BagError):
        raise BagError(str(result))
    return result


def _has_pose(topics: set[str]) -> bool:
    return any(t in topics for t in POSE_TOPICS) or \
        any(re.fullmatch(re.escape(px4.LOCAL_BASE) + r"(_v\d+)?", t) for t in topics)


def _has_gps(topics: set[str]) -> bool:
    return any(t in topics for t in NAVSAT_TOPICS) or \
        any(re.fullmatch(re.escape(px4.GLOBAL_BASE) + r"(_v\d+)?", t) for t in topics)


def _size(path: Path) -> int:
    """Bytes on disk (stat only, cheap even on a USB stick)."""
    try:
        if path.is_file():
            return path.stat().st_size
        return sum(f.stat().st_size for f in path.iterdir() if f.is_file())
    except OSError:
        return 0


def bag_summary(path: str | Path) -> dict[str, Any]:
    """Cheap listing entry. Reads metadata.yaml only (no bag is opened)."""
    path = Path(path)
    m = _meta(path) if path.is_dir() else None
    if isinstance(m, dict) and m.get("storage_identifier") == "sqlite3":
        try:
            topics = {t["topic_metadata"]["name"] for t in m["topics_with_message_count"]
                      if t["message_count"] > 0}
            n = int(m["message_count"])
            return {
                "name": path.name, "path": str(path), "ok": n > 0,
                "start_ns": int(m["starting_time"]["nanoseconds_since_epoch"]),
                "size": _size(path),
                "duration": round(m["duration"]["nanoseconds"] / 1e9, 1),
                "messages": n, "has_pose": _has_pose(topics), "has_gps": _has_gps(topics),
                "error": None if n else "Empty bag",
            }
        except (KeyError, TypeError, ValueError):
            pass  # unusual metadata.yaml: fall through to the full reader
    try:  # mcap, split bags, missing/broken metadata: let the full reader judge
        reader = _open(path)
    except BagError as exc:
        return {"name": path.name, "path": str(path), "ok": False, "error": str(exc),
                "size": _size(path)}
    try:
        topics = {c.topic for c in reader.connections if c.msgcount > 0}
        return {
            "name": path.name, "path": str(path), "ok": reader.message_count > 0,
            "start_ns": reader.start_time, "size": _size(path),
            "duration": round((reader.end_time - reader.start_time) / 1e9, 1),
            "messages": reader.message_count,
            "has_pose": _has_pose(topics), "has_gps": _has_gps(topics),
            "error": None if reader.message_count else "Empty bag",
        }
    finally:
        reader.close()
