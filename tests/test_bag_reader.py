import pytest
from conftest import (SEC, T0, circle, jpeg_msg, make_bag, odom_msg, pose_msg,
                      string_msg)

import bag_reader as br

POSE = "/mavros/local_position/pose"
CAM = "/downward_camera/image_raw/compressed"


def test_reads_pose_modes_and_duration(flight_bag):
    f = br.load_flight(flight_bag)
    assert f["pose_topic"] == POSE
    assert f["pose_frame"] == "map"
    assert len(f["pose"]["t"]) >= 50          # decimated to <= 20 Hz, never empty
    assert f["pose"]["t"] == sorted(f["pose"]["t"])
    assert f["duration"] == pytest.approx(9.9, abs=0.2)
    assert f["warnings"] == []
    assert f["max_extent_m"] == pytest.approx(10.0, abs=0.5)  # circle of radius 5


def test_mode_text_is_split_into_mode_and_status(flight_bag):
    f = br.load_flight(flight_bag)
    assert [m["value"] for m in f["modes"]] == ["POSCTL", "OFFBOARD", "AUTO.LOITER"]
    # the repeated status text must not be reported twice
    assert [s["value"] for s in f["status_events"]] == ["Arming denied: fix health first"]


def test_missing_position_gives_warning_not_crash(tmp_path):
    bag = make_bag(tmp_path / "nopose", {
        "/flight_mode": ("std_msgs/msg/String", [(T0, string_msg("OFFBOARD"))])})
    f = br.load_flight(bag)
    assert f["pose"] is None
    assert any("No position data" in w for w in f["warnings"])


def test_drone_that_never_moved_is_flagged(tmp_path):
    still = [(T0 + i * SEC // 10, pose_msg(T0 + i * SEC // 10, 1.0, 1.0, 0.0)) for i in range(30)]
    f = br.load_flight(make_bag(tmp_path / "still", {POSE: ("geometry_msgs/msg/PoseStamped", still)}))
    assert any("barely moved" in w for w in f["warnings"])


def test_lio_that_lost_tracking_is_flagged(tmp_path):
    poses = circle(300, hz=10)                 # 30 s of position
    lio = [(t, odom_msg(t, 0, 0, 0)) for t, _ in poses[:30]]  # LIO only for the first 3 s
    f = br.load_flight(make_bag(tmp_path / "lio", {
        POSE: ("geometry_msgs/msg/PoseStamped", poses),
        "/Odometry": ("nav_msgs/msg/Odometry", lio)}))
    assert f["lio_coverage"] < 0.2
    assert any("LIO" in w for w in f["warnings"])


def test_lio_at_a_lower_rate_is_not_a_false_alarm(tmp_path):
    poses = circle(300, hz=30)                 # 30 Hz position ...
    lio = [(t, odom_msg(t, 0, 0, 0)) for t, _ in poses[::3]]  # ... 10 Hz LIO, same span
    f = br.load_flight(make_bag(tmp_path / "lio_ok", {
        POSE: ("geometry_msgs/msg/PoseStamped", poses),
        "/Odometry": ("nav_msgs/msg/Odometry", lio)}))
    assert f["lio_coverage"] > 0.9
    assert not any("LIO" in w for w in f["warnings"])


# ---------------- error handling: always a readable BagError ----------------

def test_missing_path(tmp_path):
    with pytest.raises(br.BagError, match="not found"):
        br.load_flight(tmp_path / "nope")


def test_empty_bag(tmp_path):
    bag = make_bag(tmp_path / "empty", {POSE: ("geometry_msgs/msg/PoseStamped", [])})
    with pytest.raises(br.BagError, match="empty"):
        br.load_flight(bag)


def test_folder_without_metadata(tmp_path):
    (tmp_path / "half").mkdir()
    (tmp_path / "half" / "half_0.db3").write_bytes(b"")
    with pytest.raises(br.BagError, match="metadata"):
        br.load_flight(tmp_path / "half")


def test_empty_metadata_yaml(flight_bag):
    (flight_bag / "metadata.yaml").write_text("")
    with pytest.raises(br.BagError, match="metadata.yaml is empty"):
        br.load_flight(flight_bag)


def test_corrupted_database(flight_bag):
    db = next(flight_bag.glob("*.db3"))
    db.write_bytes(b"this is not a sqlite database" * 100)
    with pytest.raises(br.BagError, match="damaged"):
        br.load_flight(flight_bag)


def test_summary_never_raises_for_broken_bags(tmp_path, flight_bag):
    (flight_bag / "metadata.yaml").write_text("")
    s = br.bag_summary(flight_bag)
    assert s["ok"] is False and "metadata" in s["error"]


def test_summary_of_good_bag(flight_bag):
    s = br.bag_summary(flight_bag)
    assert s["ok"] and s["has_pose"] and s["messages"] == 104


# ---------------- camera frames ----------------

@pytest.fixture
def camera_bag(tmp_path):
    frames = [(T0 + i * SEC, jpeg_msg(T0 + i * SEC, bytes([i]) * 50 + b"\xff\xd9")) for i in range(5)]
    return make_bag(tmp_path / "cam", {
        POSE: ("geometry_msgs/msg/PoseStamped", circle(50)),
        CAM: ("sensor_msgs/msg/CompressedImage", frames)})


def test_frame_lookup_picks_frame_at_or_after_time(camera_bag):
    assert br.load_camera_frame(camera_bag, CAM, 0.0)[0] == 0
    assert br.load_camera_frame(camera_bag, CAM, 2.0)[0] == 2
    assert br.load_camera_frame(camera_bag, CAM, 2.4)[0] == 3   # next one at/after t


def test_frame_lookup_clamps_out_of_range_times(camera_bag):
    assert br.load_camera_frame(camera_bag, CAM, -10)[0] == 0
    assert br.load_camera_frame(camera_bag, CAM, 999)[0] == 4


def test_frame_unknown_or_non_camera_topic_returns_none(camera_bag):
    assert br.load_camera_frame(camera_bag, "/nope", 1.0) is None
    assert br.load_camera_frame(camera_bag, POSE, 1.0) is None


def test_fast_path_matches_generic_reader(camera_bag):
    for t in (0.0, 1.7, 3.0, 50.0):
        assert br._frame_sqlite(camera_bag, CAM, t) == \
            br._frame_generic(camera_bag, CAM, t) or t == 50.0  # generic has no "past end" rule


def test_flight_lists_camera_timestamps(camera_bag):
    f = br.load_flight(camera_bag)
    assert f["cameras"][CAM] == [0.0, 1.0, 2.0, 3.0, 4.0]


# ---------------- caching ----------------

def test_flight_is_cached_on_disk_and_reused(flight_bag, monkeypatch):
    br.load_flight(flight_bag)
    assert len(list(br.CACHE_DIR.glob("*.json"))) == 1
    br._flight_cached.cache_clear()                      # simulate a new process
    monkeypatch.setattr(br, "_load_flight", lambda p: pytest.fail("parsed again"))
    assert br.load_flight(flight_bag)["pose_topic"] == POSE


def test_cache_is_invalidated_when_the_bag_changes(flight_bag):
    br.load_flight(flight_bag)
    (flight_bag / "metadata.yaml").write_text((flight_bag / "metadata.yaml").read_text() + "\n# touched\n")
    br._flight_cached.cache_clear()
    br.load_flight(flight_bag)
    assert len(list(br.CACHE_DIR.glob("*.json"))) == 2


def test_corrupt_cache_file_is_ignored(flight_bag):
    br.load_flight(flight_bag)
    next(br.CACHE_DIR.glob("*.json")).write_text("{ not json")
    br._flight_cached.cache_clear()
    assert br.load_flight(flight_bag)["pose_topic"] == POSE


def test_split_bag_serves_frames_from_every_file(tmp_path):
    """bag_0.db3 holds frames 0-4, bag_1.db3 frames 5-9; frames from the
    second file must be reachable, not frozen on the last frame of the first."""
    first = [(T0 + i * SEC, jpeg_msg(T0 + i * SEC, bytes([i]) * 10)) for i in range(5)]
    second = [(T0 + i * SEC, jpeg_msg(T0 + i * SEC, bytes([i]) * 10)) for i in range(5, 10)]
    bag = make_bag(tmp_path / "split", {CAM: ("sensor_msgs/msg/CompressedImage", first)})
    other = make_bag(tmp_path / "other", {CAM: ("sensor_msgs/msg/CompressedImage", second)})
    (bag / "split.db3").rename(bag / "split_0.db3")
    (other / "other.db3").rename(bag / "split_1.db3")
    assert br._frame_sqlite(bag, CAM, 2.0)[0] == 2
    assert br._frame_sqlite(bag, CAM, 4.5)[0] == 5     # crosses into the second file
    assert br._frame_sqlite(bag, CAM, 7.0)[0] == 7
    assert br._frame_sqlite(bag, CAM, 99.0)[0] == 9    # past the end -> last frame overall


def test_summary_survives_metadata_with_missing_keys(flight_bag):
    meta = flight_bag / "metadata.yaml"
    meta.write_text(meta.read_text().replace("duration:", "duration_renamed:", 1))
    s = br.bag_summary(flight_bag)   # must not raise, whatever the verdict
    assert s["name"] == flight_bag.name


def test_errors_are_cached_so_damaged_bags_are_not_re_read(flight_bag, monkeypatch):
    (flight_bag / "metadata.yaml").write_text("")
    calls = []
    real = br._load_flight
    monkeypatch.setattr(br, "_load_flight", lambda p: calls.append(p) or real(p))
    for _ in range(3):
        with pytest.raises(br.BagError):
            br.load_flight(flight_bag)
    assert len(calls) == 1


def test_summary_has_date_and_size_for_the_library(flight_bag):
    s = br.bag_summary(flight_bag)
    assert s["start_ns"] == T0
    assert s["size"] > 0
