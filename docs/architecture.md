# 3D Mission Viewer: Architecture

Status: **proposal, for review** (2026-11-15). Decisions: [decisions.md](decisions.md).
Work plan: [development-plan.md](development-plan.md). What the first prototype taught us:
[prototype-lessons.md](prototype-lessons.md).

---

## 1. Scope

> **This project does one thing: it shows completed missions in 3D, from rosbag recordings.**
> It does not plan missions, does not connect to a drone and does not show live telemetry.

The team's ground control station (GCS) is split between people:

| Part | Owner | This project? |
|---|---|---|
| Pre-mission software (planning, setup) | other team members | **No** |
| Live telemetry, data link, commands | other team members | **No** |
| **3D visualization of past missions from rosbags** | **this project** | **Yes** |

**Delivery:** a **standalone desktop application**, with its own packaging and releases.
It is not embedded in the GCS.

### Users
All of these are equally important, so the UI has a **clean default view** for everyone and an
**Analysis** area with the details:

| User | Typical use |
|---|---|
| Engineers / developers | Debugging a flight in the office: details, health data, raw values. |
| Pilots | Reviewing a flight right after landing, possibly in the field without internet. |
| Clients, judges, demos | A clear, polished replay of the mission. |

UI language: **English**.

### In scope
- Opening rosbag recordings (sqlite3 `.db3` and `.mcap`, compressed or not, split and damaged
  bags) and choosing them from a folder of recordings.
- **Any bag, as it is.** There are no requirements on what the drone records. The viewer
  shows what the bag contains, hides what it does not, and says so (§3.6).
- **One or more drones per bag** (§3.3).
- **3D scene of the flight:** flight path (coloured by flight mode), drone model with attitude,
  setpoints, LIO path, start/home point.
- **Indoor flights** (no GPS): local 3D scene with grid and compass.
- **Outdoor flights** (GPS): the flight placed on a 3D map with terrain and Google
  Photorealistic 3D Tiles, with a simple offline fallback (§3.5).
- **Playback:** timeline with flight-mode bands and events, play/pause/seek/speed,
  advanced keyboard and mouse navigation of the 3D view.
- **Instruments and telemetry** at the current time: attitude, compass, speeds, rates, battery, etc.
- **Mission analysis:**
  - **planned vs flown path** (§3.7);
  - **detections placed in the world** with the camera's view cone (§3.8);
  - **health timeline** and **pilot RC inputs** (§3.9).

### Out of scope
- Mission planning and pre-flight tools.
- Any live connection to a drone: telemetry, data link, video streaming, commands.
- Recording bags (done on the drone), and requirements on what is recorded.

---

## 2. Technology stack

The same base stack as the rest of the team's GCS (Ubuntu 22.04, ROS 2 Humble, Qt/C++), so
the team can share knowledge, tooling and machines.

| Concern | Choice | Notes |
|---|---|---|
| Platform | **Ubuntu 22.04** | Team infrastructure. Developers on other versions use the Docker devcontainer. |
| ROS | **ROS 2 Humble**, used only to **read bags** | No nodes and no network. Humble's end of life (May 2027) is a team-wide topic, see ADR-004. |
| Language / UI | **C++20, Qt 6 (Qt Quick / QML)**, current LTS (6.8 or newer) | Ubuntu 22.04's Qt is 6.2, so use the Qt installer or `aqtinstall`. Link Qt dynamically (LGPL). |
| Bag reading | **`rosbag2_cpp`** | Reads sqlite3 and mcap, compressed (zstd) or not, plus our message types (`px4_msgs`, `mavros_msgs`, …) when installed. Unknown message types are skipped, not fatal. |
| 3D engine | **Decided by a prototype** (ADR-002) | osgEarth + Cesium Native, or CesiumJS inside Qt WebEngine. Indoor scene possibly Qt Quick 3D. |
| Build | CMake (`ament_cmake`) in a colcon workspace | |
| Tests | GoogleTest, Qt Quick Test, synthetic test bags | |

---

## 3. Structure

```
 rosbag (.db3 / .mcap)
        │
        ▼
 ┌──────────────┐   ┌───────────────┐   ┌──────────────────┐   ┌─────────────────────┐
 │ BagSource    │──▶│ Adapters      │──▶│ Flight model     │──▶│ Views (QML)         │
 │ rosbag2_cpp  │   │ PX4 (NED→ENU) │   │ series + events  │   │ 3D scene (indoor)   │
 │ summary index│   │ MAVROS        │   │ state at time t  │   │ 3D map (GPS)        │
 │ frame lookup │   │ team topics   │   │                  │   │ timeline, overlays  │
 └──────────────┘   └───────────────┘   └──────────────────┘   └─────────────────────┘
        ▲                  ▲                    ▲
   local disk cache   topics.yaml          ReplayClock (play / pause / seek / speed)
```

### 3.1 BagSource
- Opens a bag with `rosbag2_cpp`. Never writes to the bag's medium (bags live on USB sticks).
- On first open it builds a **summary index**: downsampled pose, attitude, angular rates,
  velocity, battery, GPS, flight-mode changes, arming, status messages, events and
  camera-frame timestamps. The index is cached on the local
  disk, keyed by file size, modification time and index format version. Re-opening is instant (L1, L2).
- Shows progress during the first read: large bags on USB take minutes.
- **Camera frames** are read on demand, with at most one request per camera in flight
  ("latest wins"). Never queue frames (L3).
- Damaged, empty or incomplete bags produce a clear message, never a crash (L7).
- **Time:** the viewer uses the bag's receive time as the common clock, because it is always
  present and monotonic. Header stamps are used for interpolation where a topic's are
  reliable (e.g. LIO, which has latency). The UI shows both the time since the start of the
  recording and the real date and time of day (local time zone).

### 3.2 Adapters
These turn messages into flight-model data. Topic names come from **`topics.yaml`**, with
a preference list per signal, because names differ between bags (L4).
- **PX4** (`/fmu/out/*`): NED/FRD → ENU/FLU conversion, version suffixes, EKF validity flags (L5).
- **MAVROS**: already ENU. Also `mavros_msgs` (`/mavros/state`, `/mavros/statustext/recv`, waypoints).
- **Team topics**: flight mode strings, course state, setpoints, LIO, YOLO detections (L7).

### 3.3 Flight model
- **Several drones per bag:** drones are detected by topic namespace (e.g. `/drone1/mavros/…`,
  `/drone2/fmu/…`; topics without a namespace form one drone). Each drone has its own series,
  events, path, model and telemetry. A bag with one drone looks exactly like before.
- Time series and events for each drone, plus "state at time *t*" (interpolated pose,
  current mode, battery, …) for the views.
- Internally everything is **ENU metres** relative to the start point, plus WGS84 for flights
  with GPS. Altitudes are converted correctly for the 3D map (ellipsoid vs AMSL, L8).
- Depends only on QtCore, so it can be unit-tested without UI or ROS.

### 3.4 Views

**Layout** (QGroundControl-style, professional rather than playful):
- A slim **toolbar**:
  - button for the recording list;
  - recording name, date and time of day;
  - **drone selector** (only when the bag has several drones);
  - status chips for the selected drone (armed/disarmed, flight mode, GPS, battery);
  - warnings counter;
  - settings.
- The **main view** (3D map or local 3D scene) fills the window. Everything else floats on top:
  the instrument panel top-right, camera picture-in-picture top-left, view controls bottom-right.
- The **timeline** is docked at the bottom.
- The **Analysis** panel (Ctrl+J) slides up above the timeline, with tabs:
  - Health (§3.9);
  - RC inputs (§3.9);
  - Plan vs flown (§3.7);
  - Detections (§3.8);
  - Recording info (§3.6).
- **Several drones:** all paths and models are drawn, the selected drone is highlighted, and
  the instruments, camera and chips follow the selected drone.

**Scenes**
- **Indoor 3D scene**: grid, N/E/S/W markers on the ground, path coloured by flight mode, drone
  model with attitude, heading line, setpoint, start/home marker, LIO path (hidden by default, L6).
- **3D map** for GPS flights: terrain + Google Photorealistic 3D Tiles (L9), path coloured by
  mode, drone with attitude, home marker. GPS flights open on the map by default.

**Instruments and telemetry** (all at the current playback time)
- **Attitude indicator** (artificial horizon) showing roll and pitch.
- **Compass rose / heading** in degrees, with the direction to home.
- **Values with units, fixed-width numbers:**
  - altitude (above start, plus AMSL for GPS flights);
  - horizontal speed and vertical speed;
  - distance from home;
  - heading;
  - roll, pitch and yaw rates (body rotation; when recorded, e.g. `/mavros/imu/data`);
  - battery voltage and %;
  - flight time;
  - mission/course step.
- **Status line:** flight mode in words plus the raw mode name, arming state, latest status
  message (e.g. "Arming denied: …").
- **Detections** at the current time; **warnings**: no position, drone never moved, LIO lost
  tracking, GPS lost.
- A value with no data near the current time is shown as "—" (not a stale number).

**Camera**: picture-in-picture synced to time; click to swap it with the main view; switch
between cameras.

**Timeline**
- Flight-mode colour bands, event markers (mode changes, arm/disarm, status messages, warnings).
- Hover shows the time, mode and event. Click or drag to seek; click a marker to jump to it.
- Scroll over the timeline to zoom into a section of the flight.
- Time shown as `m:ss.s` (current / total), playback speed 0.25×–16×.

**Advanced controls**
- **Mouse in the 3D view**:
  - orbit, pan and zoom;
  - **click a point on the path to jump to that moment**;
  - hover the path for a tooltip with time, altitude, speed and mode.
- **Camera modes:**
  - free;
  - follow (keeps your chosen angle behind the drone);
  - top-down (north up);
  - zoom to whole flight;
  - reset view.

  All transitions are animated, never jumps.
- **Keyboard** (also shown in a `?` overlay and in the menus):

  | Key | Action |
  |---|---|
  | Space | play / pause |
  | `,` / `.` | previous / next camera frame |
  | J / K / L | rewind / pause / fast-forward (press again = faster) |
  | Shift + `,` / `.` | jump 10 s back / forward |
  | Home / End | start / end of the flight |
  | `+` / `-` | playback speed |
  | Arrows / W A S D | pan the view |
  | Q / E | rotate the view |
  | PageUp / PageDown | zoom in / out |
  | F / T / R / Z | follow / top-down / reset view / zoom to flight |
  | M | 3D map ↔ local 3D scene (GPS flights) |
  | C | swap camera and main view |
  | Ctrl+O | open the recording list |
  | Ctrl+J | open / close the Analysis panel |
  | Tab / Shift+Tab | next / previous drone (bags with several drones) |
  | ? | show all shortcuts |

- Optional **3D-mouse / gamepad** support for the view can be added later (not planned yet).

**UI rules**
- The map attribution (bottom-left) is never covered. Google and Cesium require it.
- The recording list is hidden behind a button and only opens when needed.
- Numbers always show units and use a fixed-width font. Flight-mode colours are the same
  everywhere (path, timeline, chips).
- Every action has a keyboard shortcut and a visible control. Nothing is keyboard-only.

### 3.5 Offline fallback and the Cesium ion token
The goal is a **simple** fallback, nothing fancy:
- **Indoor flights** never need internet.
- **GPS flights** use the 3D map (terrain + Google 3D Tiles) when there is internet **and** a
  token. Otherwise they are shown in the **local 3D scene**: lat/lon converted to metres around
  the home point, on a plain grid with N/E/S/W markers. A small notice says why ("Offline:
  showing local view" / "No Cesium ion token set"). There is no tile caching, which also keeps
  the viewer within Google's terms.
- **Cesium ion token: one per user.** Each user creates their own Cesium ion account, enters the
  token once in **Settings**, and the viewer checks it straight away. It is stored in the user's
  own config (system keyring if available, otherwise a user-only readable file), never in git and
  never in the package.

### 3.6 Any bag, as it is
There are **no requirements on what the drone records.** The viewer works with whatever is in
the bag:
- every feature declares which signals it needs; features without data are hidden, not shown
  empty (e.g. no camera panel when there are no images, no map when there is no GPS);
- **Recording info** (Analysis panel) lists, per drone, what was found and what was not, so
  users understand why something is missing. It shows: position, attitude, GPS, flight
  modes, battery, cameras, detections, mission plan, health topics, RC inputs, with the topics
  used for each;
- topic names are matched through `topics.yaml` preference lists plus sensible defaults
  (`/mavros/*`, `/fmu/out/*`, common camera suffixes), and users can add mappings without
  rebuilding.

### 3.7 Planned vs flown path
Shows what the drone **was supposed to do** next to what it **did**, from data in the bag. It
is read-only: planning itself is done by another part of the team's software.
- **Plan sources** (whatever is present):
  - mission waypoints (`/mavros/mission/waypoints`);
  - geofence (`/mavros/geofence/fences`);
  - rally points (`/mavros/rallypoint/rallypoints`);
  - the current planned segment (`/drone/current_segment_path`);
  - setpoints (`/drone/setpoint_pose`, `/mavros/setpoint_raw/*`).
- **Display:** planned path as a dashed line with numbered waypoints, geofence as a translucent
  volume, rally points as markers. If the plan changes during the flight, the version that
  was active at the current time is shown.
- **Deviation:**
  - distance from the planned path over time (a band on the timeline and a plot in the
    Analysis panel);
  - max / mean deviation;
  - when each waypoint was reached (within its acceptance radius), or that it was missed;
  - geofence breaches as events.

### 3.8 Detections in the world
Turns "the camera saw something" into "**it was here**".
- **Camera view cone** in 3D: the camera's field of view at the current time, from `camera_info`
  (intrinsics) and `/tf_static` (where the camera sits on the drone), on top of the drone pose.
- **Placing detections:** for each detection, a ray from the camera through the box centre is
  intersected with the ground (indoor: floor plane; GPS flights: terrain height or the
  takeoff ground level). Repeated detections of the same class close together are merged into
  one marker, with a count, a confidence and the first/last time seen.
- Markers are clickable: jump to the moment of the best detection and show that camera frame.
- **List** in the Analysis panel (class, position, time, confidence), exportable to CSV.
- **Needs:** the detection message must carry a box (pixel coordinates) and the camera it came
  from. The current JSON format is to be checked. If boxes are missing, detections stay a
  text list and markers are placed under the drone, marked as approximate.
- **Accuracy:** placement depends on pose accuracy, time sync and the ground assumption. The
  uncertainty is shown (marker size) instead of being hidden.

### 3.9 Health timeline and RC inputs
- **Health timeline:** extra bands under the mode bands, one per subsystem present in the bag:
  - LIO: `/diagnostics/lio/status`, `…/hz`, `…/latency_ms`, `…/pos_diff`;
  - VIO rate: `/diagnostics/vio_hz`;
  - GPS fix;
  - EKF / estimator flags: `/mavros/estimator_status`, PX4 equivalents;
  - battery level.

  Each band is green / amber / red by thresholds in `topics.yaml`, with hover details. Moments
  where the drone "lost itself" stand out at a glance.
- **RC inputs:** from `/mavros/rc/in` (PX4: `input_rc`): two stick widgets plus switch positions
  at the current time, and an "RC active" band on the timeline. This shows whether a manoeuvre
  came from the pilot or the autopilot. The channel mapping (which channel is roll, throttle, mode
  switch) is configurable, with Mode 2 as the default.

---

## 4. 3D engine options (decided by the prototype, ADR-002)

| Option | Pros | Cons |
|---|---|---|
| **A. osgEarth + Cesium Native** | Native C++, mature terrain/GIS, Google 3D Tiles supported (osgEarth 3.5+) | OpenSceneGraph is in maintenance mode and OpenGL-only; embedding in Qt Quick 6 needs the OpenGL backend; must be built from source (Ubuntu packages are far too old) |
| **B. CesiumJS in Qt WebEngine** | Full Cesium features, quick to build (the prototype already did it in a browser) | A web component inside a native app, with a second rendering stack |
| **C. Qt Quick 3D + Cesium Native** | Modern Qt stack, one scene graph | We would write the 3D Tiles rendering ourselves (large job); desk study only |

The prototype builds the same demo with A and B: a Qt 6 window on Ubuntu 22.04, Google 3D
Tiles at a test site, a replayed GPS flight with a moving drone, attribution visible.

It measures:
- build effort;
- frame rate on an Intel Iris Xe laptop;
- memory use;
- how cleanly it integrates with Qt Quick;
- behaviour without internet.

---

## 5. Threading
- **GUI thread:** QML and view updates, at most ~60 Hz.
- **Index worker:** builds the summary index on first open and reports progress.
- **Frame worker:** camera-frame lookups (latest wins).

The GUI thread never touches the bag directly.

---

## 6. Testing
- Unit tests (GoogleTest): frame conversions, adapters, flight model, replay clock, index cache.
- **Synthetic test bags** generated by the tests (as the prototype did; see `tests/conftest.py`
  at tag `prototype-electron`), covering empty, damaged, split, indoor, GPS and PX4 bags.
- A small set of real bags in a shared test-data folder (not in git) for a manual check
  before each release: every IMAV-26 bag and the PX4 SITL GPS flight.
- Synthetic bags also cover: compressed bags, several drones, a mission plan with deviation,
  detections with boxes, health topics and RC input.
- QML tests for the timeline and keyboard shortcuts.

---

## 7. Proposed repository layout

```
viewer/                   ament_cmake package (standalone application)
  src/bag/                BagSource, summary index + cache, frame lookup
  src/adapters/           px4/, mavros/, team/
  src/model/              flight model (per drone), ReplayClock
  src/analysis/           plan vs flown, detection placement, health evaluation
  src/views/              3D scene / map integration
  src/app/                main(), QML registration, settings (incl. ion token)
  qml/                    UI
  config/topics.yaml
  tests/
docker/                   devcontainer (Ubuntu 22.04, Humble, Qt)
docs/
```

---

## 8. Performance targets (proposed)

These are starting values. Confirm or adjust them in step 2, once the first measurements on
real bags exist.

| What | Target |
|---|---|
| Largest bag supported | 20 GB, flights up to 1 hour |
| First open of a bag from USB 3 | progress visible within 1 s; a 10 GB bag ready in under 2 minutes (image payloads are not read while indexing) |
| Re-opening a bag (cached index) | under 1 s |
| Seek → scene, instruments and camera frame updated | under 200 ms |
| Frame rate on an Intel Iris Xe laptop | 60 fps in the local 3D scene, 30 fps or more on the 3D map |
| Memory for a 1-hour flight | under 2 GB |
| Camera playback at 1× | the recorded rate, up to 30 fps |
