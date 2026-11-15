# 3D Mission Viewer: Development plan

Status: **proposal, for review** (2026-11-15). Architecture: [architecture.md](architecture.md).

> Scope: **3D visualization of past missions from rosbags only**, as a standalone application.
> Pre-mission software and live telemetry are built by other team members and are not part
> of this plan.

Durations are rough estimates for one developer, for planning only. Re-estimate after step 1.

---

## Steps

### 0. Foundations (1 week)
- Docker dev container on the team's `base-image:jazzy` (Ubuntu 24.04, ROS 2 Jazzy) plus
  `px4_msgs` (branch `release/1.17`), `mavros_msgs`, Qt 6.8 LTS,
  GPU and display passthrough.
- Colcon workspace with an empty `viewer` app (window + QML), `clang-format`, CI building and
  running tests on every push.
- **Done when:** a fresh checkout builds and runs with one command, following the README.

### 1. 3D engine prototype, time-boxed (1–2 weeks)
**Status (2026-11-15): in progress.** Prototype B is measured. Prototype A builds and is being
debugged (findings in [decisions.md, ADR-002](decisions.md#adr-002-3d-engine) and
[prototype-lessons.md](prototype-lessons.md)). Code: `ws/src/prototypes/`.
- The same demo with osgEarth + Cesium Native and with CesiumJS in Qt WebEngine (ADR-002).
- **Done when:** the engine is chosen and recorded in ADR-002.

### 2. Bag reading and flight model (2–3 weeks)
**Status (2026-11-15): started.** Done and unit-tested (`ws/src/viewer/src/model/`): frame
conversions and WGS84/ENU (`frames`), `ReplayClock`, `DroneTrack` with interpolation, dropout
rule and flight-mode lookup, and the synthetic bag generator `make_test_bag` (PX4 topics, circle /
ellipse / figure-eight from the onboarding, sqlite3 or mcap, optional GPS and namespace), with a
test that reads its bags back with `rosbag2_cpp`. To do: `BagSource`, adapters and `topics.yaml`,
drone detection by namespace, "Recording info", compressed and split test bags, performance
measurements.
- `BagSource` (sqlite3, mcap, compressed, split bags), summary index with an on-disk cache,
  progress on first open, clear errors for broken bags.
- Adapters: PX4, MAVROS, team topics; `topics.yaml` with defaults. Unknown types are skipped.
- Flight model **per drone** (drones detected by namespace), `ReplayClock`, time handling
  (receive time as the clock, real date and time of day).
- "Recording info": what was found and what was not, per drone.
- Unit tests with synthetic bags (including compressed and multi-drone bags).
- Measure against the performance targets ([architecture.md §8](architecture.md#8-performance-targets-proposed))
  and adjust them.
- **Done when:** every IMAV-26 bag and the PX4 SITL bag load, or fail with a clear message.

### 3. Indoor 3D scene, playback and controls (3–4 weeks)
- Layout: toolbar with status chips and drone selector, full-window view, floating panels,
  docked timeline, Analysis panel frame.
- Scene: grid, N/E/S/W markers, path coloured by mode, drone(s) with attitude, heading line,
  setpoint, LIO path, start/home marker.
- Timeline: mode bands, event markers, hover info, click/drag seek, jump to marker, zoom.
- **Advanced controls** ([architecture.md §3.4](architecture.md#34-views)): click the path
  to seek, path tooltips, free/follow/top-down/zoom-to-flight/reset with animated transitions,
  the full keyboard map and the `?` overlay.
- **Done when:** the IMAV-26 hall flights replay smoothly and every control in the shortcut
  table works.

### 3b. Instruments and telemetry (1–2 weeks)
- Attitude indicator, compass rose with heading and direction to home.
- Telemetry values: altitude, horizontal/vertical speed, distance from home, heading,
  roll/pitch/yaw rates, battery, flight time, mission step. Status line and warnings.
- "—" for values with no data near the current time.
- **Done when:** the instruments match the 3D scene (e.g. a 90° yaw turn shows on the compass,
  the horizon and the drone model alike), checked on PX4 and MAVROS bags.

### 4. 3D map for GPS flights (2–3 weeks)
- Terrain + Google Photorealistic 3D Tiles (via Cesium ion), flight path and drone on the map,
  attribution visible, correct altitude (ellipsoid vs AMSL, "put takeoff on the ground" option).
- GPS flights open on the map by default; one key/button switches to the local 3D scene.
- Settings: per-user Cesium ion token with a "test token" button, stored in the keyring or
  user config.
- **Offline fallback:** without internet or token, GPS flights are shown in the local 3D scene
  with a short notice.
- **Done when:** the PX4 SITL GPS flight sits at the right place and height, a real outdoor
  flight has been checked as well, and unplugging the network falls back cleanly.

### 5. Camera, view cone and detections in the world (2–3 weeks)
- Camera picture-in-picture synced to time (click to swap with the main view), frame stepping,
  camera switching.
- Camera view cone in 3D (`camera_info` + `/tf_static`).
- Detections placed in the world: ray through the box centre onto floor or terrain, merged
  markers with count, confidence and time, click to jump, list + CSV export
  ([architecture.md §3.8](architecture.md#38-detections-in-the-world)).
- Check the current detection format first; without boxes, fall back to approximate markers
  under the drone.
- **Done when:** detections from the IMAV-26 baby-detector bags appear where the object was,
  within the stated uncertainty.

### 6. Planned vs flown path (1–2 weeks)
- Waypoints, geofence, rally points, current segment path and setpoints from the bag; the plan
  version active at the current time.
- Deviation over time (timeline band + plot), max/mean deviation, waypoints reached/missed,
  geofence breaches as events.
- **Done when:** a mission with a plan in the bag shows its plan and deviation, and a bag
  without a plan simply hides the feature.

### 7. Health timeline and RC inputs (1–2 weeks)
- Health bands for LIO, VIO, GPS, EKF and battery, with thresholds in `topics.yaml` and hover details.
- RC stick and switch widgets, "RC active" band, configurable channel mapping (Mode 2 default).
- **Done when:** a known LIO dropout in an IMAV-26 bag is visible at a glance, and manual vs
  autonomous segments can be told apart from the RC band.

### 8. Recording library and finishing (1–2 weeks)
- Recording list behind a button: folder choice, search, sort by date, date/duration/size,
  drag and drop.
- Settings (units, map layers, ion token), remembered window/panel layout.
- Packaging: container image vs `.deb`, decided once the hosts' Ubuntu version is confirmed (ADR-010), short user guide, release notes.

### Later
- Telemetry plots for any signal, flight summary, export (video of the replay, PDF/HTML report,
  CSV/GPX, screenshot), bookmarks and links to a moment.
- Comparing several flights, lidar point cloud (`/livox/lidar`), measuring tool, topic inspector.
- Recovering bags with a missing `metadata.yaml`.
- 3D-mouse / gamepad navigation.

---

## Open questions
1. **Detection format:** the message wrapper is known (checked 2026-11-15):
   `{"frame_id": "downward_camera_optical_frame", "stamp": {...}, "count": N, "detections": [...]}`,
   so the **source camera is known**. But every detection list in the IMAV-26 bags is empty,
   so the fields of a single detection (bounding box? class? confidence?) are unknown. They are
   defined in `drone_autonomy/mission_2/yolo_node.py`, whose source is not on the laptop or the
   USB stick. Ask the author, and record one bag with real detections for testing (step 5).
2. **Test data:** where to keep a shared set of real bags (the IMAV-26 set is on a USB stick
   today), and at least one real outdoor GPS flight and one multi-drone bag.
3. **Performance targets:** confirm or adjust the proposed numbers after step 2.
4. **Viewer machines:** which laptops/GPUs must run it smoothly (baseline: Intel Iris Xe).
5. **Host Ubuntu version and distribution:** the team says 22.04, the dev laptop reports 24.04. Decides between a container image and a `.deb` (ADR-010).
   Distribution: how teammates install and update it (`.deb` from a shared location,
   an apt repository, …).
