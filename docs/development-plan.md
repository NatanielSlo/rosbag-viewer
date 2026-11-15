# 3D Mission Viewer: Development plan

Status: **proposal, for review** (2026-11-15). Architecture: [architecture.md](architecture.md).

> Scope: **3D visualization of past missions from rosbags only.** Pre-mission software and
> live telemetry are built by other team members and are not part of this plan.

Durations are rough estimates for one developer, for planning only. Re-estimate after step 1.

---

## Steps

### 0. Foundations (1 week)
- Docker devcontainer: Ubuntu 22.04, ROS 2 Humble (+ `px4_msgs`, `mavros_msgs`), Qt 6 LTS,
  GPU and display passthrough.
- Colcon workspace with an empty `viewer` app (window + QML), `clang-format`, CI building and
  running tests on every push.
- **Done when:** a fresh checkout builds and runs with one command, following the README.

### 1. 3D engine prototype, time-boxed (1–2 weeks)
- The same demo with osgEarth + Cesium Native and with CesiumJS in Qt WebEngine (ADR-002).
- **Done when:** the engine is chosen and recorded in ADR-002.

### 2. Bag reading and flight model (2–3 weeks)
- `BagSource` (sqlite3, mcap, split bags), summary index with an on-disk cache, progress on
  first open, clear errors for broken bags.
- Adapters: PX4, MAVROS, team topics; `topics.yaml`.
- Flight model and `ReplayClock`. Unit tests with synthetic bags.
- **Done when:** every IMAV-26 bag and the PX4 SITL bag load, or fail with a clear message.

### 3. Indoor 3D scene, playback and controls (3–4 weeks)
- Layout: toolbar with status chips, full-window view, floating panels, docked timeline.
- Scene: grid, N/E/S/W markers, path coloured by mode, drone with attitude, heading line,
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
- **Done when:** the PX4 SITL GPS flight sits at the right place and height, and a real outdoor
  flight has been checked as well.

### 5. Camera and detections (1–2 weeks)
- Camera picture-in-picture synced to time (click to swap with the main view), frame stepping.
- Detections shown on the image / in the scene. The detection format needs agreeing with whoever
  owns that topic: today it is JSON in `std_msgs/String`.

### 6. Recording library and finishing (1–2 weeks)
- Recording list behind a button: folder choice, search, sort by date, date/duration/size,
  drag and drop.
- Settings (units, map layers), remembered window/panel layout, keyboard shortcut overlay.
- Packaging for Ubuntu 22.04 (`.deb`), release notes.

### Later, only if agreed
- Telemetry plots synced to the timeline, flight summary, export (CSV/GPX/screenshot).
- Lidar point cloud (`/livox/lidar`), planned vs flown path, comparing two flights.
- Recovering bags with a missing `metadata.yaml`.

---

## Open questions
1. **Delivery:** standalone app, a module inside the team's GCS, or both? (Affects packaging
   and how the UI is structured.)
2. **Shared look:** colours, fonts and flight-mode names shared with the rest of the GCS.
3. **Test data:** where to keep a shared set of real bags (the IMAV-26 set is on a USB stick
   today), and at least one real outdoor GPS flight.
4. **Google 3D Tiles:** Cesium ion plan and pricing for team use.
5. **Viewer machines:** which laptops/GPUs must run it smoothly (baseline: Intel Iris Xe).
