# Ground Control Station: Development plan

Status: **proposal, for team review** (2026-11-15). Architecture: [architecture.md](architecture.md).
Decisions: [decisions.md](decisions.md).

The order is chosen so that the application is **useful early**: replay and analysis of real
flights first (no risk to aircraft), then live monitoring, and only then commands and mission
planning. Durations are rough estimates for **one to two developers** and are there for
planning only. Re-estimate after M0 and M1.

---

## Milestones

### M0: Foundations (1–2 weeks)
- Repository, branch rules, code review, `clang-format` / `clang-tidy`, commit conventions.
- Docker devcontainer: Ubuntu 22.04, ROS 2 Humble, Qt 6 LTS (installer / `aqtinstall`),
  GStreamer, Zenoh bridge, with GPU and display passthrough. Developers on other Ubuntu
  versions (e.g. 24.04 laptops) work inside it.
- Colcon workspace with an empty `gcs` app (window + QML) and an empty `horyzn_interfaces` package.
- CI: build, unit tests and formatting on every pull request, plus a second build job against
  the next ROS 2 LTS (ADR-006).
- **Done when:** a new developer gets a running empty app with one command, following the README.

### M1: 3D prototype, time-boxed (1–2 weeks, parallel with M0/M2)
- Build the same demo with osgEarth + Cesium Native, and with CesiumJS in Qt WebEngine as the
  baseline. Option B (Qt Quick 3D + Cesium Native) only gets a desk study of its effort.
- Measure build effort, frame rate on an Intel Iris Xe laptop, memory, Qt Quick integration,
  offline behaviour and attribution display.
- **Done when:** ADR-004 is decided and recorded.

### M2: Replay core (3–4 weeks)
- `BagReplaySource` (`rosbag2_cpp`, sqlite3 + mcap, split bags), `ReplayClock`
  (play, pause, seek, speed), summary index with an on-disk cache.
- Adapters: PX4 (`/fmu/out/*`, NED→ENU), MAVROS, current team topics. Topic map in YAML.
- Vehicle model, event log, time-series store.
- UI: toolbar with status chips, recording library (folder, search, sort, drag and drop),
  timeline with mode bands and event markers, telemetry panel, keyboard shortcuts.
- Indoor 3D scene (local ENU, grid, path coloured by mode, vehicle, setpoint).
- Camera replay (picture-in-picture, latest-wins frame loading).
- **Done when:** every bag in the IMAV-26 set and the PX4 SITL bag replays correctly, including
  broken and empty bags with clear messages. This is feature parity with the prototype viewer.

### M3: Maps (3–4 weeks)
- 2D map (Qt Location) with a custom tile provider, offline area download and cache, flight
  path, home, vehicle, attribution.
- 3D globe view using the engine chosen in M1: Google Photorealistic 3D Tiles via Cesium ion,
  terrain, the flight placed correctly in altitude (ellipsoid vs AMSL, lesson L8).
- **Done when:** a GPS flight is shown at the right place and height on both maps, and the
  2D map works with the network unplugged in a cached area.

### M4: Analysis (2–3 weeks)
- Plots synced to the timeline (altitude, speeds, battery, setpoint error, LIO diagnostics).
- Flight summary (date, duration, distance, max altitude/speed, battery used).
- Detections shown on the camera image (requires the message definition from M5a).
- Export: CSV, GPX/KML, screenshot, link to a moment.

### M5a: Interfaces (1–2 weeks, before M5)
- `horyzn_interfaces`: detections, course/mission state, heartbeat, GCS-rate telemetry,
  topic names, QoS profiles, Zenoh bridge allow-list (ADR-005).
- Drone-side `gcs_bridge` node that publishes GCS-rate topics. This work is shared with the
  drone software team.

### M5: Live monitoring (4–6 weeks)
- `Ros2LiveSource` (`rclcpp` executor thread), Zenoh bridge configuration on the GCS and drones.
- Link health: message age, rate, heartbeat both ways, clear link-loss indication within 1 s.
- Multiple vehicles (namespaces), vehicle selector.
- Recording on the GCS side (mcap) so every session can be replayed.
- **Done when:** a PX4 SITL drone and a real drone on the bench are monitored over Wi-Fi, and
  pulling the link is detected and shown.

### M6: Live video (2–3 weeks)
- GStreamer RTP H.264/H.265 receive, hardware decoding, latency measurement,
  picture-in-picture and full-screen.

### M7: Commands and safety (4+ weeks, needs a safety review first)
- Written hazard analysis: what the GCS may command, confirmation rules, behaviour on link loss,
  how commands interact with RC and PX4 failsafes.
- Arm/disarm, mode change, takeoff, land, RTL, with acknowledgement, timeout and event logging.
- **Done when:** the commands pass the SITL test suite and a bench test, and the safety review
  signs off.

### M8: Mission planning (6+ weeks)
- Waypoints, survey patterns, geofence, rally points, upload/download to the vehicle via the
  companion computer, pre-flight checks.

### M9: ROS 2 / Ubuntu migration (date set by the team, before May 2027 if possible)
- Move drones, GCS and CI to the next ROS 2 LTS and Ubuntu LTS (ADR-006).

### Continuous: packaging and releases
- `.deb` package for Ubuntu 22.04 (Qt deployed alongside, ROS runtime as a dependency or
  bundled: decide in M0), versioned releases, changelog, field test checklist
  ([architecture.md §9](architecture.md#9-testing)).

---

## Suggested first sprint (two weeks)
1. M0: devcontainer, empty app and CI.
2. M1: osgEarth build in the devcontainer (the riskiest item, start it on day one).
3. M2 start: `BagReplaySource` + `ReplayClock` + PX4/MAVROS adapters with unit tests,
   using small synthetic bags.

---

## Open questions for the team
1. **ROS/Ubuntu migration date** (ADR-006): Humble's end of life is May 2027.
2. **RC and failsafe link:** does a separate RC link (e.g. ELRS) stay for manual control and
   failsafe? The plan assumes yes and that the GCS is never flight-critical.
3. **GCS hardware:** which laptops, which GPU (the 3D view needs a decent integrated GPU at
   least), and whether tablets are needed later.
4. **Other platforms:** is Windows needed? This affects packaging and the 3D engine choice.
5. **Product licence:** proprietary or open source? This affects which libraries are allowed
   (GPL-only libraries are out if proprietary; Qt must then be dynamically linked under LGPL).
6. **Google 3D Tiles:** Cesium ion plan and pricing for production use.
7. **Product name** (the docs say "GCS" for now) and repository hosting / CI service.
8. **Who owns `horyzn_interfaces`:** the drone team, the GCS team, or both.
