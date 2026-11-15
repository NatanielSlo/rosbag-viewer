import math
from types import SimpleNamespace

import numpy as np
import pytest

import px4

R2 = math.sqrt(0.5)


def rotate(q_xyzw, v):
    x, y, z, w = q_xyzw
    return px4.quat_to_mat(w, x, y, z) @ np.array(v, dtype=float)


# ---- orientation: FRD body in NED  ->  FLU body in ENU --------------------

def test_level_and_facing_north_means_nose_points_north():
    q = px4.ned_frd_to_enu_flu((1, 0, 0, 0))            # identity in PX4's frames
    assert rotate(q, [1, 0, 0]) == pytest.approx([0, 1, 0], abs=1e-9)    # forward -> North
    assert rotate(q, [0, 1, 0]) == pytest.approx([-1, 0, 0], abs=1e-9)   # left    -> West
    assert rotate(q, [0, 0, 1]) == pytest.approx([0, 0, 1], abs=1e-9)    # up      -> Up


def test_yaw_90_degrees_clockwise_faces_east():
    # PX4 yaw is clockwise from north: +90 deg is a rotation about NED's Down axis
    q = px4.ned_frd_to_enu_flu((R2, 0, 0, R2))
    assert rotate(q, [1, 0, 0]) == pytest.approx([1, 0, 0], abs=1e-9)    # forward -> East
    assert rotate(q, [0, 1, 0]) == pytest.approx([0, 1, 0], abs=1e-9)    # left    -> North


def test_pitching_the_nose_up_raises_the_nose():
    # nose-up pitch is a positive rotation about FRD's Y (right) axis
    a = math.radians(30)
    q = px4.ned_frd_to_enu_flu((math.cos(a / 2), 0, math.sin(a / 2), 0))
    fwd = rotate(q, [1, 0, 0])
    assert fwd[2] == pytest.approx(math.sin(a), abs=1e-9)                # points up
    assert fwd[1] == pytest.approx(math.cos(a), abs=1e-9)                # still mostly north


def test_matrix_quaternion_round_trip():
    rng = np.random.default_rng(1)
    for _ in range(50):
        q = rng.normal(size=4)
        q /= np.linalg.norm(q)
        x, y, z, w = px4.mat_to_quat(px4.quat_to_mat(*q))
        m1, m2 = px4.quat_to_mat(*q), px4.quat_to_mat(w, x, y, z)
        assert np.allclose(m1, m2, atol=1e-9)


def test_zero_quaternion_does_not_crash():
    assert px4.ned_frd_to_enu_flu((0, 0, 0, 0)) is not None


# ---- topic discovery -------------------------------------------------------

def reader_with(*topics):
    return SimpleNamespace(connections=[SimpleNamespace(topic=t, msgcount=n) for t, n in topics])


def test_find_connection_prefers_highest_version():
    r = reader_with(("/fmu/out/vehicle_status", 5), ("/fmu/out/vehicle_status_v1", 5),
                    ("/fmu/out/vehicle_status_v2", 5))
    assert px4.find_connection(r, px4.STATUS_BASE).topic.endswith("_v2")


def test_find_connection_skips_empty_and_lookalikes():
    r = reader_with(("/fmu/out/vehicle_status_v2", 0), ("/fmu/out/vehicle_status_v1", 3),
                    ("/fmu/out/vehicle_status_flags", 9))
    assert px4.find_connection(r, px4.STATUS_BASE).topic == "/fmu/out/vehicle_status_v1"
    assert px4.find_connection(reader_with(("/other", 1)), px4.STATUS_BASE) is None


def test_nav_state_names_match_the_viewer_labels():
    assert px4.NAV_STATES[14] == "OFFBOARD"
    assert px4.NAV_STATES[4] == "AUTO.LOITER"
    assert px4.NAV_STATES[3] == "AUTO.MISSION"
