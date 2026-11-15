# Ground Control Station: Architecture

Status: **proposal, for team review** (2026-11-15). Decisions are tracked in
[decisions.md](decisions.md), the work plan in [development-plan.md](development-plan.md),
and everything learned from the first prototype in [prototype-lessons.md](prototype-lessons.md).

---

## 1. What we are building

A native ground control station (GCS) for our drones, in the spirit of QGroundControl,
but built around **ROS 2** instead of MAVLink:

- **Live:** monitor (and later command) one or more drones over an IP link (Wi-Fi, mesh, LTE).
- **Replay:** open a recorded rosbag and replay it **through the same pipeline** as live data,
  so every view (map, 3D, instruments, plots, video) works identically for both.
- **Analyse:** telemetry plots, event log, flight summary and export, for engineers.
- **Plan (later):** missions, geofences, and uploading them to the vehicle.

It replaces the Electron/Python rosbag viewer prototype, which is kept for reference under the
git tag `prototype-electron` (see [prototype-lessons.md](prototype-lessons.md)).

### Users
| User | Needs |
|---|---|
| Operator in the field | Clear, calm UI. Status at a glance, reliable warnings, works offline, sunlight-readable. |
| Engineer / developer | Raw values, plots, topic inspection, bag replay, export. |
| Non-technical viewer (demos, clients) | Replay a flight on a map without training. |

A single application with a clean default view and an **Analysis** area for engineers
serves all three.

### Non-goals (for now)
- Replacing the RC link or the autopilot's own failsafes. The GCS is not flight-critical.
- Windows/macOS/Android builds. The architecture must not prevent them, but Ubuntu 22.04 comes first.
- Supporting MAVLink-only vehicles. A MAVLink adapter can be added later behind the same interface.

---

## 2. Technology stack

| Concern | Choice | Notes |
|---|---|---|
| Language | **C++20** | Same language as `rclcpp`, `rosbag2_cpp`, Qt, Cesium Native. |
| Platform | **Ubuntu 22.04** (drones, GCS laptops, CI) | Matches the team's infrastructure. Developers on other Ubuntu versions use the Docker devcontainer. |
| ROS distribution | **ROS 2 Humble** | **End of life May 2027.** See ADR-006: the code must stay buildable on the next LTS, and the migration is planned (development plan, M9). |
| UI | **Qt 6 (Qt Quick / QML)**, current LTS (6.8 or newer) | Ubuntu 22.04's Qt is 6.2 and **has no Qt Location**, so use the Qt online installer or `aqtinstall`. Qt is LGPL: link dynamically. |
| Data link | **ROS 2 over an IP link, bridged with `zenoh-bridge-ros2dds`** | DDS discovery is chatty and handles lossy or wide-area links badly. The bridge keeps default DDS inside each machine and sends only selected topics over Zenoh. It works on Humble and across ROS distributions. `rmw_zenoh` is an option after the migration to a newer distro. |
| Bag reading | **`rosbag2_cpp`** (sqlite3 + mcap) | ROS is required for the live link anyway, so no separate parser. |
| 2D map | **Qt Location** with our own tile provider and offline cache | The proven ground-station approach (QGC does the same). |
| 3D view | **To be decided by a prototype** (see §6 and ADR-004) | Candidates: osgEarth + Cesium Native, Qt Quick 3D + Cesium Native, CesiumJS in Qt WebEngine. |
| Plots | Qt Graphs (evaluate performance first) | Avoid GPL-only plotting libraries (e.g. QCustomPlot) unless the licence is cleared. |
| Live video | **GStreamer** (RTP H.264/H.265) into Qt Quick | Low latency, hardware decoding. QGC uses the same approach. |
| Build | CMake (`ament_cmake`) in a colcon workspace | One build system for ROS and Qt code. |
| Tests | GoogleTest (core), Qt Quick Test (QML), replay fixtures, PX4 SITL in Docker | See §9. |
| Dev environment | Docker devcontainer: Ubuntu 22.04, Humble, Zenoh bridge, Qt, GStreamer | The team already uses a Docker `base_image` for PX4 SITL. GPU and display are passed through for the 3D view. |

---

## 3. High-level architecture

```
                 ┌──────────────────────── GCS process ─────────────────────────┐
  drone(s)       │                                                              │
 ┌──────────┐    │  Data sources            Adapters           Vehicle model    │
 │ PX4      │    │ ┌───────────────┐   ┌──────────────┐   ┌────────────────┐   │
 │ uXRCE-DDS│    │ │ Ros2Live      │──▶│ PX4 (/fmu/*) │──▶│ Vehicle        │   │
 │ /fmu/*   │ IP │ │ (rclcpp,      │   │ MAVROS       │   │  state (now)   │   │
 │ + zenoh  │════│▶│ +zenoh bridge)│   │ HORYZN topics│   │  time series   │   │
 │ companion│link│ └───────────────┘   │ (detections, │   │  events        │   │
 │ ROS 2    │    │ ┌───────────────┐   │  LIO, course)│   │  health/link   │   │
 └──────────┘    │ │ BagReplay     │──▶│              │──▶│                │   │
                 │ │ (rosbag2_cpp, │   └──────────────┘   └───────┬────────┘   │
  .mcap/.db3 ───▶│ │  ReplayClock) │     topic map (YAML)         │ Qt props / │
                 │ └───────────────┘                              │ signals    │
                 │                                                ▼            │
                 │  ┌─────────────── UI (Qt Quick / QML) ───────────────────┐  │
                 │  │ toolbar · status · instruments · 2D map · 3D view ·   │  │
                 │  │ video · timeline (replay) · plots · event log · plan  │  │
                 │  └───────────────────────────────────────────────────────┘  │
                 │  Commands (live only) ──▶ Ros2Live publishers / services    │
                 └──────────────────────────────────────────────────────────────┘
```

**Rule:** only the data-source layer knows whether data is live or replayed. Adapters,
the vehicle model and the UI must not branch on it, except for the replay timeline
and for disabling commands during replay.

---

## 4. Modules

### 4.1 Data sources (`src/sources/`)
A common interface delivers timestamped messages to the adapters:

```cpp
class DataSource : public QObject {
  // emits: message(topic, type, stamp, serialized bytes), connectionChanged(...)
  virtual bool isLive() const = 0;
};
```

- **`Ros2LiveSource`** runs `rclcpp` on its own executor thread and subscribes using the topic map.
  It reports link health (last message age, rate, estimated latency) per vehicle.
- **`BagReplaySource`** uses `rosbag2_cpp::Reader` with a **`ReplayClock`** (play, pause,
  seek, speed). Opening a bag creates a **summary index** (downsampled series, mode changes,
  frame timestamps), cached on the local disk and keyed by file size and modification time.
  Timeline and plots use the index immediately; the replay itself streams. This keeps large
  bags on USB drives usable (lessons L1, L2).
- Camera frames during replay are fetched **on demand, latest-wins, one request per camera at
  a time** (lesson L3). Never queue frames.

### 4.2 Adapters (`src/adapters/`)
These turn raw messages into vehicle-model updates. One adapter per message family:

- **PX4** (`px4_msgs`, `/fmu/out/*`): convert NED/FRD to **ENU/FLU** once, at the edge.
  Topic version suffixes (`_v1`) vary by PX4 release, so match by base name (lesson L5).
- **MAVROS** (standard ROS types): already ENU.
- **HORYZN topics** (detections, LIO odometry, course state, setpoints): defined in a
  **shared interface package** used by both the drone and the GCS (see §8).

Topic names live in a **YAML topic map**, not in code, with a preference list per signal
(lesson L4).

### 4.3 Vehicle model (`src/core/`)
- `Vehicle` (`QObject`) exposes current state as Qt properties: pose (ENU and geo), attitude,
  velocities, flight mode, armed state, GPS fix, battery, mission step, health flags, link state.
- A **time-series store** (ring buffers live, the full index in replay) feeds the plots.
- An **event log** records mode changes, arm/disarm, warnings, detections, link loss and
  operator actions.
- `VehicleManager` handles multiple vehicles, identified by ROS namespace (`/drone1/...`).
- Coordinate frames: everything internal is **ENU in metres relative to a home point**,
  plus WGS84 for vehicles with GPS. Indoor vehicles without GPS stay in local ENU (lesson L6).

This layer depends only on QtCore, not on QML, ROS or rendering, so it can be unit-tested
in isolation.

### 4.4 UI (`qml/`, `src/app/`)
QGroundControl-style layout: a top **toolbar** (vehicle selector, status chips, link state,
warnings), a **full-screen main view** (2D map, 3D view or video), floating **instrument
panel** and **video picture-in-picture**, and the **timeline** at the bottom in replay mode.
Panels can be resized and remember their layout.

UI rules:
- Overlays never cover the map attribution (bottom-left). It is a legal requirement for
  Google and Cesium data.
- Every number has a unit and a fixed-width font. Stale values (no update for N seconds)
  are greyed out, never left looking current.
- Every action has a keyboard shortcut. Arrows/WASD pan the view; `,` `.` J K L and
  Shift seek in time; Space plays/pauses; `?` lists the shortcuts.
- Commands that affect a flying vehicle need explicit confirmation (slide to confirm).

### 4.5 Maps (`src/maps/`)
- **2D:** Qt Location with a custom tile provider and an **offline tile cache** (pre-download
  an area before going to the field). Open data such as OSM or national orthophoto is cached;
  **Google tiles may not be cached** (Google's terms).
- **3D:** photorealistic terrain and buildings (Google Photorealistic 3D Tiles via Cesium ion),
  flight path, vehicle model, home and setpoints. The engine is decided by the prototype (§6).
- **Indoor scene:** a local ENU grid, compass and floor plan, without a globe. Needed for
  hall flights, which have no GPS.

### 4.6 Video (`src/video/`)
- **Live:** GStreamer pipeline (RTP H.264/H.265 over UDP) into a Qt Quick video item, with
  hardware decoding where available.
- **Replay:** `sensor_msgs/CompressedImage` frames from the bag, decoded with Qt.

### 4.7 Commands (`src/commands/`, later milestone)
Arm/disarm, mode changes, takeoff, land, RTL, then missions. These are sent as `px4_msgs`
(`VehicleCommand`, etc.) or as ROS services/actions on the companion computer. Every command
is logged in the event log and needs an acknowledgement and a timeout. The design needs a
safety review before implementation (see the development plan).

---

## 5. Threading

| Thread | Work |
|---|---|
| GUI thread | QML, vehicle model property updates (throttled to ~30 Hz for display) |
| ROS executor thread | `rclcpp` callbacks, deserialisation, adapters |
| Replay thread | `rosbag2_cpp` reading, `ReplayClock` |
| Index / cache worker | building bag summaries, offline tile downloads |
| GStreamer threads | video decoding |

Data crosses into the GUI thread only through queued signals or lock-free snapshots.
No ROS or bag call may ever block the GUI thread (lesson L3).

---

## 6. 3D view: options and prototype criteria

| Option | Pros | Cons |
|---|---|---|
| **A. osgEarth + Cesium Native** | Mature GIS (terrain, imagery, offline caches), supports Google 3D Tiles today | OpenSceneGraph is in maintenance mode and OpenGL-only; embedding it in Qt Quick 6 means forcing the OpenGL backend; Ubuntu's packages are osgEarth 2.10 at best (far too old, no Cesium Native), so it has to be built from source, together with an OpenSceneGraph built for OpenGL 3; Cesium Native's Linux build is undocumented |
| **B. Qt Quick 3D + Cesium Native** | Modern Qt rendering stack, one scene graph | We write the glTF upload, tile lifecycle, camera and precision handling (large job) |
| **C. CesiumJS in Qt WebEngine** | Full Cesium features within days | A web component inside a native app; a second rendering stack |

The prototype (1–2 weeks) builds the same thing with option A, and with C as the baseline:
a Qt 6 window, Google 3D tiles at a test site, a replayed flight path and moving vehicle,
attribution shown, built on Ubuntu 22.04.

It measures:
- build effort;
- frame rate on the Intel Iris Xe laptop;
- memory use;
- how cleanly it integrates with Qt Quick;
- behaviour without internet.

The result is recorded in ADR-004.

---

## 7. Data link

- **Transport:** each drone and the GCS run `zenoh-bridge-ros2dds`. Inside a machine, nodes
  use the default DDS (with `ROS_LOCALHOST_ONLY` or a domain ID so that DDS discovery never
  goes over the radio). Between machines, only the topics listed in the bridge configuration
  (allow-list) go over Zenoh. The GCS bridge listens on a fixed endpoint and drones connect
  to it; there is no multicast discovery over the radio link.
- **Namespaces:** each drone's bridge adds its namespace (`/drone1/...`), so several drones
  with identical software do not collide on the GCS.
- **Bandwidth budget:** the GCS subscribes only to what it displays, and high-rate topics are
  downsampled on the drone (a dedicated `gcs_bridge` node publishing GCS-rate topics). Video
  goes over RTP, not ROS.
- **Link health** is a first-class signal: message age per topic, rate, and a heartbeat
  topic both ways. The UI must make link loss obvious within one second.
- **Safety:** the RC link (e.g. ELRS) and PX4's own failsafes stay independent of the GCS.
  This is still an open question for the team (see the development plan).

---

## 8. Interfaces between drone and GCS

Create a shared ROS 2 interface package (working name `horyzn_interfaces`). It is versioned
and used by both the drone software and the GCS, and contains:

- message definitions for our own topics (detections with bounding boxes, course and mission
  state, heartbeat, GCS-rate telemetry);
- the canonical topic names, with a namespace per vehicle;
- the QoS profiles for each topic (reliable or best-effort, depth).

This replaces the JSON-in-`std_msgs/String` detections and free-form mode strings seen in
the current bags (lesson L7).

---

## 9. Testing

- **Core unit tests** (GoogleTest): frame conversions, adapters, vehicle model, replay clock,
  summary index.
- **Replay fixtures:** small synthetic bags generated in tests (the prototype's Python
  generator in `tests/conftest.py` at tag `prototype-electron` shows how), plus a few real
  short bags kept in a test-data store (not in git).
- **QML tests** (Qt Quick Test): instruments, timeline and shortcuts.
- **Integration:** PX4 SITL + Gazebo in Docker publishing over uXRCE-DDS → Zenoh bridge →
  the GCS. CI checks that telemetry arrives and that link loss is detected.
- **Field checklist** before each release: sunlight readability, offline map, link drop and
  recovery, long session (memory growth).

---

## 10. Proposed repository layout

```
gcs/                      ament_cmake package (the application)
  CMakeLists.txt  package.xml
  src/core/               vehicle model, time series, events (QtCore only)
  src/sources/            Ros2LiveSource, BagReplaySource, ReplayClock, summary index
  src/adapters/           px4/, mavros/, horyzn/
  src/maps/               tile provider + offline cache, 3D integration
  src/video/              GStreamer integration
  src/commands/           (later)
  src/app/                main(), QML type registration, settings
  qml/                    UI
  config/topics.yaml      topic map
  tests/
horyzn_interfaces/        shared msg/srv/action definitions (or a separate repo)
docker/                   devcontainer and SITL images
docs/                     this documentation
```
