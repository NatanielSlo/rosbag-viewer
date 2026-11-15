# Lessons from the prototype viewer

The first rosbag viewer (Electron + Python `rosbags` + three.js/CesiumJS, November 2026)
is kept in git at tag **`prototype-electron`**: `git checkout prototype-electron`.
It read every bag in the IMAV-26 set plus a PX4 SITL GPS flight and had 49 passing tests.
This page keeps what we learned from it, so the new viewer does not have to rediscover it.
The architecture refers to these as L1, L2, …

---

### L1. Large bags on USB sticks are slow to read the first time
Bags are up to several GB and live on a USB stick (`ROSBAGS/IMAV-26`) because laptops lack
space. A full read takes minutes and is sequential; there is no way to speed it up without
an index, and **we never write to the bag medium**.
→ Build a small summary index once, cache it on the local disk, and show progress during the
first read.

### L2. The cache must know when the code or the bag changes
The prototype keyed its cache on bag file size + mtime + a `CACHE_VERSION` constant that had
to be bumped whenever the summary format changed. Forgetting to bump it showed stale data.
→ Derive the cache key from the format version automatically (e.g. a schema hash).

### L3. Never let frame requests pile up
The prototype "froze" because every camera frame re-opened the bag (~0.5 s) while the UI asked
for ~16 frames per second; the queue grew without limit and blocked everything else.
Fix: keep the database connection open, index frame timestamps once, and allow **at most one
outstanding request per camera, latest wins**.
→ Same rule in the new viewer. With the indexed lookup, a frame took ~0.1 ms once warm.

### L4. Topic names differ between bags
The same signal appears under different names (e.g. flight mode on `/flight_mode`,
`/drone/flight_mode` and `/diagnostics/flight_mode`). The prototype used a preference list per
signal: first topic with data wins.
→ Topic map in YAML, with a preference list per signal.

### L5. PX4 (`/fmu/out/*`) specifics
- NED world / FRD body → ENU / FLU: position `(x, y, z)_ENU = (y, x, -z)_NED`; attitude
  `R_ENU = R_ENU←NED · R(q_NED) · R_FRD→FLU`. The prototype's tests check nose direction for
  yaw 0 (north), yaw 90° (east) and pitch up.
- Topic names carry a version suffix that changes between PX4 releases
  (`vehicle_local_position_v1`, `vehicle_status_v1`): match the base name, prefer the highest version.
- Skip `vehicle_local_position` samples until `xy_valid && z_valid` (EKF not converged yet).
- `nav_state` → mode names: 0 MANUAL, 1 ALTCTL, 2 POSCTL, 3 AUTO.MISSION, 4 AUTO.LOITER,
  5 AUTO.RTL, 10 ACRO, 12 DESCEND, 13 TERMINATION, 14 OFFBOARD, 15 STABILIZED,
  17 AUTO.TAKEOFF, 18 AUTO.LAND, 19 AUTO.FOLLOW, 20 AUTO.PRECLAND, 21 ORBIT, 22 VTOL takeoff.
- `px4_msgs` definitions are embedded in mcap bags but **not** in sqlite3 bags.

### L6. Indoor flights have no GPS, and LIO uses its own frame
All IMAV-26 hall flights are GPS-less, so the 3D scene must work in local ENU without a globe.
LIO odometry (`/Odometry`) is in `camera_init`, while MAVROS pose is in `map`: they are not
aligned, so the prototype hid LIO by default. LIO often loses tracking: warn when it covers
less than 60% of the flight (gaps over 1 s count as lost).

### L7. Data quality in the current bags
- Flight-mode strings sometimes carry status text after a newline (`"OFFBOARD\nArming denied: …"`):
  first line = mode, rest = status message.
- YOLO detections are JSON inside `std_msgs/String` (`/mission_2/yolo/detections`).
- Interrupted recordings: an empty or missing `metadata.yaml` (e.g. `wp_follower_…21_50_42`,
  a full 92 s flight whose data is still in the `.db3`); truncated / corrupted `.db3`;
  bags with zero messages. All of these must produce a clear message, never a crash. Rebuilding
  metadata from the database is possible and would recover such flights.
- Some bags have no position at all (camera-only recordings), and some have the drone never
  taking off. The prototype warned when the position changed by less than 0.3 m.
- Bags split over several `.db3` files: order the pieces by time, not by name (`_10` sorts before `_2`).

### L8. Altitude reference
Cesium (and 3D Tiles) use height above the **WGS84 ellipsoid**; PX4 gives both `alt` (AMSL)
and `alt_ellipsoid`; MAVROS `NavSatFix.altitude` is ellipsoid height. Around Munich the
difference is about 47 m. In PX4 SITL both values are identical, so SITL does **not** test this.
The prototype offered "put takeoff on the ground": sample terrain height at the takeoff point
and shift the whole flight so it starts on the ground.

### L9. Google Photorealistic 3D Tiles
Streamed through **Cesium ion asset `2275207`** with only an ion token. The Google Maps key is
never needed on the client. Tiles **may not be cached** for offline use (Google's terms), and the
attribution must stay visible (bottom-left). Pricing and limits of the ion plan still need
checking for production use.

### L10. Environment pitfalls
- Ubuntu 24.04 blocks unprivileged user namespaces through AppArmor, so Chromium-based apps
  (Electron, and **Qt WebEngine** too, which matters for 3D engine option B) need either a root-owned
  SUID `chrome-sandbox` or an AppArmor profile. Ubuntu 22.04 is not affected.
- `pkill -f server.py` also kills the shell that runs it. Find processes by port (`ss -ltnp`).
- Headless Firefox hangs on the dev laptop. Use Playwright (or Qt's own test tools) for screenshots.

### L12. Engine prototypes: things that cost time (Step 1)
- **Qt WebEngine in a container:** Chromium's sandbox needs privileges a container does not have,
  so dev runs use `QTWEBENGINE_CHROMIUM_FLAGS=--no-sandbox` (set by `scripts/dev`, dev only).
- **CesiumJS under Qt:** `file://` pages break Cesium's web workers. Serve the page through a
  custom URL scheme (`app://`, `QWebEngineUrlSchemeHandler`), which also works offline.
- **CesiumJS `Viewer` option `globe`:** it takes a `Globe` object or `false`, not `true`. Passing
  `true` fails with "Error constructing CesiumWidget ... reading 'addEventListener'". Leave it
  `undefined` for the default globe.
- **Qt Quick 6 + OpenGL libraries:** Qt Quick must be forced to its OpenGL backend
  (`QQuickWindow::setGraphicsApi`), and it still creates a Core profile context. Anything that
  needs the compatibility profile (a GL2-style OpenSceneGraph) cannot render into it.
- **Seeing what the app draws:** from a container there is no screenshot tool, so the prototypes
  accept `PROTO_SCREENSHOT=/path.png` and save the window with `grabToImage`. Do not open test
  windows on the user's screen without telling them: a stock `osgearth_viewer` window left on
  the screen looked like a bug to the user.
- **Shared build folders:** CMake caches whether it found a library. Use separate build and
  install folders per image (`scripts/dev` uses `build_osg`/`install_osg` for the osgEarth image).
- **Background jobs:** a background command's exit code is the last command's (`tail` hides a
  failed build). Check the log for the real result.
- **Qt + CMake:** `qt_standard_project_setup(REQUIRES 6.5)` is needed, or the QML module is
  registered under the old resource prefix and `loadFromModule` cannot find it. A QML module's
  C++ headers must be on the target's include path.
- **Floating point:** the heading of a drone facing North came out as 360° instead of 0°
  (`-1e-16` plus `2pi`). Angle wrapping needs a test at the boundary.

### L11. Recording a PX4 SITL flight with GPS (works)
1. `export PX4_HOME_LAT=48.2655 PX4_HOME_LON=11.6710 PX4_HOME_ALT=480` (TUM Garching), then
   `make px4_sitl gz_x500` in the Docker `base_image`.
2. QGroundControl: plan waypoints at different heights → Upload → Takeoff → Start Mission.
3. Before takeoff: `ros2 bag record -o tum_test /fmu/out/vehicle_global_position
   /fmu/out/vehicle_local_position_v1 /fmu/out/vehicle_attitude /fmu/out/vehicle_status_v1`.
   Stop with Ctrl+C only after landing, otherwise no `metadata.yaml` is written.
   Check `ros2 topic list | grep fmu` first: a missed `vehicle_status` means no flight modes.
4. The bag lands in `~/Repos/` (= `/ros2_ws` in the container). Jazzy records **mcap** by default,
   Humble records **sqlite3** by default.

---

## What the team onboarding adds (onboarding_tutorial.pdf)

Facts from the team's "Software and Autonomy Onboarding Exercises" that matter to the viewer:

- **One environment for everyone:** the Docker image `base-image:jazzy` (from
  `OpenHORYZN/base_image`, `./build_container.sh`, 30-60 min). Containers run as `root`;
  `~/Repos` on the laptop is `/ros2_ws` in the container; QGroundControl runs on the laptop.
  Supported hosts: Ubuntu 22.04 and 24.04.
- **PX4 v1.17** with the uXRCE-DDS agent (`MicroXRCEAgent udp4 -p 8888`). Topics from the drone
  are `/fmu/out/*`; commands go to `/fmu/in/*`.
- **Versioned topic names**, e.g. `/fmu/out/vehicle_local_position_v1`, `/fmu/out/vehicle_status_v1`.
  The adapters must accept the suffix and check the exact name (confirms L4/L5).
- **QoS:** PX4 publishes best-effort. This matters for recording and for any bag-replay test.
- **Frames:** PX4 uses NED (world) and FRD (body). ROS uses ENU/FLU. NED to ENU is
  `(x, y, z) = (y, x, -z)` and `yaw_ENU = pi/2 - yaw_NED`. Heading is clockwise from North.
- **Timestamps** in PX4 messages are in microseconds (`timestamp` field).
- Offboard flights send `OffboardControlMode` and `TrajectorySetpoint` at more than 2 Hz, so
  setpoints in `/fmu/in/trajectory_setpoint` are present in bags that record them. Useful for
  the "setpoint" overlay and for telling OFFBOARD segments from manual ones.
- Test flights for the viewer can be produced with the onboarding's `shape_control` exercise
  (circle, ellipse, figure-eight in SITL), which gives known paths to check the 3D scene against.

---

## Topic inventory of the team's bags

From 19 bags (IMAV-26 set + PX4 SITL), counting how many bags contain each topic with data.
Useful when writing the adapters and `topics.yaml`.

| Bags | Topic | Type |
|---:|---|---|
| 19 | `/rosout` | rcl_interfaces/msg/Log |
| 18 | `/diagnostics` | diagnostic_msgs/msg/DiagnosticArray |
| 18 | `/diagnostics/battery_cell_voltage` | std_msgs/msg/Float32 |
| 18 | `/diagnostics/flight_mode`, `/drone/flight_mode`, `/flight_mode` | std_msgs/msg/String |
| 18 | `/diagnostics/vio_hz` | std_msgs/msg/Float32 |
| 18 | `/simulation_obstacles` | visualization_msgs/msg/MarkerArray |
| 18 | `/tf`, `/tf_static` | tf2_msgs/msg/TFMessage |
| 17 | `/mavros/local_position/pose` | geometry_msgs/msg/PoseStamped |
| 17 | `/mavros/local_position/odom` | nav_msgs/msg/Odometry |
| 17 | `/mavros/local_position/velocity_local`, `…/velocity_body` | geometry_msgs/msg/TwistStamped |
| 17 | `/mavros/battery` | sensor_msgs/msg/BatteryState |
| 17 | `/mavros/state` | mavros_msgs/msg/State |
| 17 | `/mavros/extended_state`, `/mavros/sys_status`, `/mavros/status_event`, `/mavros/estimator_status`, `/mavros/vfr_hud`, `/mavros/altitude` | mavros_msgs |
| 17 | `/mavros/mission/waypoints`, `/mavros/geofence/fences`, `/mavros/rallypoint/rallypoints` | mavros_msgs/msg/WaypointList |
| 17 | `/mavros/imu/*` (data, data_raw, mag, static_pressure, temperature_imu) | sensor_msgs |
| 17 | `/mavros/setpoint_raw/target_local`, `…/target_attitude` | mavros_msgs |
| 17 | `/mavros/rc/out` (8 bags also `/mavros/rc/in`) | mavros_msgs |
| 17 | `/mavros/odometry/in` (16: `/mavros/odometry/out`) | nav_msgs/msg/Odometry |
| 17 | `/uas1/mavlink_source`, `/uas1/mavlink_sink` | mavros_msgs/msg/Mavlink |
| 16 | `/Odometry` (LIO) | nav_msgs/msg/Odometry |
| 16 | `/livox/imu` | sensor_msgs/msg/Imu |
| 13 | `/livox/lidar` | livox_ros_driver2/msg/CustomMsg |
| 12 | `/diagnostics/lio/{hz,latency_ms,pos_diff,vel_diff}` | std_msgs/msg/Float32 |
| 12 | `/diagnostics/lio/status` | std_msgs/msg/String |
| 12 | `/downward_camera/image_raw/compressed` (7: raw `image_raw`) + `camera_info` | sensor_msgs |
| 10 | `/forward_camera/image_raw/compressed` (5: raw `image_raw`) + `camera_info` | sensor_msgs |
| 10 | `/drone/course_state` | std_msgs/msg/String |
| 10 | `/drone/setpoint` | mavros_msgs/msg/PositionTarget |
| 10 | `/drone/setpoint_pose` | geometry_msgs/msg/PoseStamped |
| 9 | `/mavros/setpoint_raw/local` | mavros_msgs/msg/PositionTarget |
| 6 | `/mavros/statustext/recv` | mavros_msgs/msg/StatusText |
| 3 | `/drone/current_segment_path` | nav_msgs/msg/Path |
| 3 | `/mission_2/yolo/detections` | std_msgs/msg/String (JSON) |
| 3 | `/mission_2/yolo/annotated(/compressed)` | sensor_msgs |
| 3 | `/mavros/time_reference` | sensor_msgs/msg/TimeReference |
| 2 | `/fmu/out/vehicle_attitude`, `vehicle_global_position`, `vehicle_local_position_v1` | px4_msgs |
| 2 | `/parameter_events` | rcl_interfaces/msg/ParameterEvent |
| 1 | `/mavros/param/event` | mavros_msgs/msg/ParamEvent |

**Consequence for the viewer:** MAVROS is the main source (the 17 flight bags), PX4 `/fmu/out` is
the exception (2 bags, both SITL or test flights). Adapters were first written for PX4 only and
would have shown nothing for most real bags; always check this table before choosing what to build.
The two GPS-capable bags are PX4; the MAVROS bags have no `/mavros/global_position/global`, so real
outdoor MAVROS flights still need a test bag (open question 2 in the plan).

Notes: the prototype read only standard types, because sqlite3 bags do not embed `mavros_msgs` /
`livox_ros_driver2` definitions. The new viewer reads bags with `rosbag2_cpp` and ROS installed
(ADR-003), so it can decode them, which gives it much richer data than the prototype had (`/mavros/state` for armed/mode,
`/mavros/statustext/recv`, `/mavros/sys_status`, waypoints and geofence).
