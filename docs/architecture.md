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

### In scope
- Opening rosbag recordings (sqlite3 `.db3` and `.mcap`, including split and damaged bags)
  and choosing them from a folder of recordings.
- **3D scene of the flight:** flight path (coloured by flight mode), drone model with attitude,
  setpoints, LIO path, start/home point.
- **Indoor flights** (no GPS): local 3D scene with grid and compass.
- **Outdoor flights** (GPS): the flight placed on a 3D map with terrain and Google
  Photorealistic 3D Tiles.
- **Playback:** timeline with flight-mode bands and events, play/pause/seek/speed,
  keyboard and mouse navigation of the 3D view.
- What is needed to understand the 3D picture at a given moment: flight mode, altitude, speed,
  battery, onboard camera image, detections.

### Out of scope
- Mission planning and pre-flight tools.
- Any live connection to a drone: telemetry, data link, video streaming, commands.
- Recording bags (done on the drone).

### To agree with the GCS team
- **How the viewer is delivered**: as a standalone application, as a module embedded in the
  GCS, or both. The design below keeps the viewer self-contained (a library plus a thin app),
  so both options stay possible.
- Shared look and feel (colours, fonts, flight-mode names and colours) with the rest of the GCS.

---

## 2. Technology stack

The same stack as the rest of the GCS, so the viewer can be embedded in it.

| Concern | Choice | Notes |
|---|---|---|
| Platform | **Ubuntu 22.04** | Team infrastructure. Developers on other versions use the Docker devcontainer. |
| ROS | **ROS 2 Humble**, used only to **read bags** | No nodes and no network. Humble's end of life (May 2027) is a team-wide topic, see ADR-004. |
| Language / UI | **C++20, Qt 6 (Qt Quick / QML)**, current LTS (6.8 or newer) | Ubuntu 22.04's Qt is 6.2, so use the Qt installer or `aqtinstall`. Link Qt dynamically (LGPL). |
| Bag reading | **`rosbag2_cpp`** | Reads sqlite3 and mcap, plus our message types (`px4_msgs`, `mavros_msgs`, …) when installed. |
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

### 3.2 Adapters
These turn messages into flight-model data. Topic names come from **`topics.yaml`**, with
a preference list per signal, because names differ between bags (L4).
- **PX4** (`/fmu/out/*`): NED/FRD → ENU/FLU conversion, version suffixes, EKF validity flags (L5).
- **MAVROS**: already ENU. Also `mavros_msgs` (`/mavros/state`, `/mavros/statustext/recv`, waypoints).
- **Team topics**: flight mode strings, course state, setpoints, LIO, YOLO detections (L7).

### 3.3 Flight model
- Time series and events for the whole flight, plus "state at time *t*" (interpolated pose,
  current mode, battery, …) for the views.
- Internally everything is **ENU metres** relative to the start point, plus WGS84 for flights
  with GPS. Altitudes are converted correctly for the 3D map (ellipsoid vs AMSL, L8).
- Depends only on QtCore, so it can be unit-tested without UI or ROS.

### 3.4 Views

**Layout** (QGroundControl-style, professional rather than playful):
- A slim **toolbar**: button for the recording list, recording name and date, status chips
  (armed/disarmed, flight mode, GPS, battery), warnings counter.
- The **main view** (3D map or local 3D scene) fills the window. Everything else floats on top:
  the instrument panel top-right, camera picture-in-picture top-left, view controls bottom-right.
- The **timeline** is docked at the bottom.

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
  | ? | show all shortcuts |

- Optional **3D-mouse / gamepad** support for the view can be added later (not planned yet).

**UI rules**
- The map attribution (bottom-left) is never covered. Google and Cesium require it.
- The recording list is hidden behind a button and only opens when needed.
- Numbers always show units and use a fixed-width font. Flight-mode colours are the same
  everywhere (path, timeline, chips).
- Every action has a keyboard shortcut and a visible control. Nothing is keyboard-only.

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
- QML tests for the timeline and keyboard shortcuts.

---

## 7. Proposed repository layout

```
viewer/                   ament_cmake package
  src/bag/                BagSource, summary index + cache, frame lookup
  src/adapters/           px4/, mavros/, team/
  src/model/              flight model, ReplayClock
  src/views/              3D scene / map integration
  src/app/                main(), QML registration, settings
  qml/                    UI
  config/topics.yaml
  tests/
docker/                   devcontainer (Ubuntu 22.04, Humble, Qt)
docs/
```
