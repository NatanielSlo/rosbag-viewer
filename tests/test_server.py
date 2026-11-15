import pytest
from conftest import SEC, T0, circle, jpeg_msg, make_bag
from fastapi.testclient import TestClient

import server

CAM = "/downward_camera/image_raw/compressed"


@pytest.fixture
def bags_dir(tmp_path):
    root = tmp_path / "bags"
    root.mkdir()
    frames = [(T0 + i * SEC, jpeg_msg(T0 + i * SEC, bytes([i]) * 20)) for i in range(3)]
    make_bag(root / "good", {
        "/mavros/local_position/pose": ("geometry_msgs/msg/PoseStamped", circle(40)),
        CAM: ("sensor_msgs/msg/CompressedImage", frames)})
    broken = make_bag(root / "broken", {
        "/mavros/local_position/pose": ("geometry_msgs/msg/PoseStamped", circle(10))})
    (broken / "metadata.yaml").write_text("")
    return root


def local_client(bags_dir):
    """The app only answers requests addressed to the loopback host."""
    return TestClient(server.make_app(bags_dir), base_url="http://127.0.0.1:8765")


@pytest.fixture
def client(bags_dir):
    return local_client(bags_dir)


def test_lists_good_and_broken_bags_without_failing(client, bags_dir):
    r = client.get("/api/bags")
    assert r.status_code == 200
    by_name = {b["name"]: b for b in r.json()["bags"]}
    assert by_name["good"]["ok"] and by_name["good"]["has_pose"]
    assert not by_name["broken"]["ok"] and "metadata" in by_name["broken"]["error"]


def test_flight_endpoint(client, bags_dir):
    r = client.get("/api/flight", params={"path": str(bags_dir / "good")})
    assert r.status_code == 200
    assert r.json()["pose_topic"] == "/mavros/local_position/pose"


def test_broken_bag_is_a_readable_422_not_a_500(client, bags_dir):
    r = client.get("/api/flight", params={"path": str(bags_dir / "broken")})
    assert r.status_code == 422
    assert "metadata.yaml" in r.json()["error"]


def test_paths_outside_the_bags_folder_are_refused(client, tmp_path):
    for bad in ("/etc", str(tmp_path), str(tmp_path / "bags" / ".." / ".." / "etc")):
        assert client.get("/api/flight", params={"path": bad}).status_code == 403
        assert client.get("/api/frame", params={"path": bad, "topic": CAM, "t": 0}).status_code == 403


def test_frame_endpoint_returns_jpeg_bytes(client, bags_dir):
    r = client.get("/api/frame", params={"path": str(bags_dir / "good"), "topic": CAM, "t": 1.0})
    assert r.status_code == 200
    assert r.headers["content-type"] == "image/jpeg"
    assert r.content == bytes([1]) * 20


def test_frame_for_unknown_topic_is_404(client, bags_dir):
    r = client.get("/api/frame", params={"path": str(bags_dir / "good"), "topic": "/nope", "t": 0})
    assert r.status_code == 404


def test_token_is_enforced_when_configured(bags_dir, monkeypatch):
    monkeypatch.setenv("VIEWER_TOKEN", "s3cret")
    c = local_client(bags_dir)
    assert c.get("/api/bags").status_code == 401
    assert c.get("/api/bags", headers={"X-Viewer-Token": "wrong"}).status_code == 401
    assert c.get("/api/bags", headers={"X-Viewer-Token": "s3cret"}).status_code == 200


def test_frontend_is_served(client):
    assert client.get("/").status_code == 200
    assert client.get("/vendor/three/three.module.js").status_code == 200


def test_config_hands_out_the_ion_token_but_never_the_google_key(bags_dir, monkeypatch):
    monkeypatch.setenv("CESIUM_ION_TOKEN", "ion-token-123")
    monkeypatch.setenv("GOOGLE_MAPS_API_KEY", "google-key-456")
    r = local_client(bags_dir).get("/api/config")
    assert r.status_code == 200
    assert r.json()["cesium_ion_token"] == "ion-token-123"
    assert "google-key-456" not in r.text
    assert r.json()["cesium_base"].startswith(("https://", "/vendor/"))


def test_config_requires_the_token_when_configured(bags_dir, monkeypatch):
    monkeypatch.setenv("VIEWER_TOKEN", "s3cret")
    c = local_client(bags_dir)
    assert c.get("/api/config").status_code == 401


def test_env_file_parser(tmp_path):
    f = tmp_path / ".env.local"
    f.write_text("# comment\nA=1\nB = \"two\"\n\nbad line\nC='3'\n")
    assert server.load_env_file(f) == {"A": "1", "B": "two", "C": "3"}
    assert server.load_env_file(tmp_path / "missing") == {}


def test_app_files_are_revalidated_but_frames_stay_cacheable(client, bags_dir):
    for path in ("/", "/main.js", "/style.css"):
        assert client.get(path).headers["cache-control"] == "no-cache"
    r = client.get("/api/frame", params={"path": str(bags_dir / "good"), "topic": CAM, "t": 1.0})
    assert "max-age" in r.headers["cache-control"]


def test_frames_accept_the_token_as_a_query_parameter(bags_dir, monkeypatch):
    """<img src> cannot send headers, so frame URLs carry ?token=."""
    monkeypatch.setenv("VIEWER_TOKEN", "s3cret")
    c = local_client(bags_dir)
    params = {"path": str(bags_dir / "good"), "topic": CAM, "t": 1.0}
    assert c.get("/api/frame", params=params).status_code == 401
    assert c.get("/api/frame", params={**params, "token": "wrong"}).status_code == 401
    assert c.get("/api/frame", params={**params, "token": "s3cret"}).status_code == 200


def test_requests_for_a_foreign_host_are_refused(bags_dir):
    """DNS rebinding: evil.example re-pointed at 127.0.0.1 still sends its own Host."""
    c = TestClient(server.make_app(bags_dir), base_url="http://evil.example")
    assert c.get("/api/config").status_code == 400
    assert c.get("/api/bags").status_code == 400
    assert local_client(bags_dir).get("/api/bags").status_code == 200


def test_library_and_open_are_app_only(client, bags_dir, tmp_path):
    """Without a token (plain browser), the readable folders cannot be changed."""
    assert client.post("/api/library", json={"path": str(tmp_path)}).status_code == 403
    assert client.post("/api/open", json={"path": str(tmp_path)}).status_code == 403


def test_app_can_switch_library_and_open_outside_recordings(bags_dir, tmp_path, monkeypatch):
    monkeypatch.setenv("VIEWER_TOKEN", "s3cret")
    c = local_client(bags_dir)
    h = {"X-Viewer-Token": "s3cret"}
    other = tmp_path / "elsewhere"
    other.mkdir()
    lone = make_bag(other / "lone", {
        "/mavros/local_position/pose": ("geometry_msgs/msg/PoseStamped", circle(20))})
    # outside the library: refused until the user opens it
    assert c.get("/api/flight", params={"path": str(lone)}, headers=h).status_code == 403
    r = c.post("/api/open", json={"path": str(lone / "metadata.yaml")}, headers=h)
    assert r.status_code == 200 and r.json()["ok"] and r.json()["path"] == str(lone)
    assert c.get("/api/flight", params={"path": str(lone)}, headers=h).status_code == 200
    # switching the library lists the new folder
    r = c.post("/api/library", json={"path": str(other)}, headers=h)
    assert [b["name"] for b in r.json()["bags"]] == ["lone"]
    assert c.post("/api/library", json={"path": str(other / "nope")}, headers=h).status_code == 404
