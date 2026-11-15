# Decisions: 3D Mission Viewer

Short records of decisions and why they were made. Add new ones at the bottom; supersede a
record instead of rewriting it.

Scope reminder: this project only covers **3D visualization of past missions from rosbags**.
Pre-mission software and live telemetry belong to other team members and are not decided here.

Status values: **Accepted**, **Proposed** (needs review), **Open** (needs input or a prototype).

---

## ADR-001: Native Qt/C++ instead of Electron
**Status:** Accepted (2026-11-15)

**Context.** The first viewer was an Electron + Python + three.js/CesiumJS prototype. The rest
of the team's GCS is native, and comparable professional tools (QGroundControl, RViz2,
PlotJuggler) are Qt/C++.

**Decision.** Build the viewer in C++20 with Qt 6 (Qt Quick), on the same base stack as the
rest of the team's GCS.

**Consequences.** The prototype code is not reused. It is kept at tag `prototype-electron`
and its lessons are in [prototype-lessons.md](prototype-lessons.md).

## ADR-002: 3D engine
**Status:** Open, decided by the 3D prototype (development plan, step 1)

**Options.** (A) osgEarth + Cesium Native, (B) CesiumJS in Qt WebEngine, (C) Qt Quick 3D +
Cesium Native (desk study only). Details and criteria: [architecture.md §4](architecture.md#4-3d-engine-options-decided-by-the-prototype-adr-002).

**Prototype results so far (2026-11-15, Intel Iris Xe laptop, Google Photorealistic 3D Tiles via ion).**

| | B: CesiumJS in Qt WebEngine | A: osgEarth + cesium-native |
|---|---|---|
| Build effort | About 150 lines plus the page; no extra build | cesium-native v0.37.0 (3.5 min) + osgEarth 3.8.1 from source; **OpenSceneGraph must also be built from source with GL3** |
| Works in Qt Quick | Yes, with a native Qt overlay on top | Needs OpenGL; Qt Quick 6 always creates a Core context (see below) |
| Frame rate | 48-56 fps with tiles, 60 bare | not measured yet |
| Memory (all processes) | about 460 MB bare, 740-830 MB with tiles after 1 min | not measured yet |
| No internet | Starts; tiles fail; shows a plain globe and a notice | not measured yet |
| Attribution | Shown by Cesium (bottom-left) | Needs `CesiumCreditsNode`; the API takes ownership of a pointer osgEarth's own context still owns, so the node must be leaked on purpose to avoid a double free |

Other findings:
- Ubuntu's OpenSceneGraph (3.6.5) is a GL2 (fixed-function) build. Qt Quick 6 always creates a
  Core-profile OpenGL context, whatever profile is requested, so osgEarth's shaders fail to
  compile there (every shader fails, and Qt's own shaders then fail too). osgEarth's install
  guide says it needs OpenGL 3. The fix is an OSG built with `OPENGL_PROFILE=GL3`.
- cesium-native v0.37.0 (the release osgEarth's own CI uses) compiles with GCC 13 on Ubuntu 24.04.
- osgEarth's `libzip` plugin does not configure on Ubuntu 24.04 (`zipcmp` missing); it is turned off.
- osgEarth loads Google tiles from the example with a Google Maps key. Our rule (one Cesium
  ion token per user) needs the ion asset route (`asset_id` 2275207) instead.

**Known facts (2026-11).**
- osgEarth 3.8 (March 2026) supports Cesium ion and Google 3D Tiles through its `osgEarthCesium` node kit.
- It runs on OpenSceneGraph, which has been in maintenance mode since 2019.
- Pelican Mapping's newer engine, Rocky (VulkanSceneGraph), has no 3D Tiles support yet.
- Ubuntu's osgEarth packages are far too old, so it must be built from source.

## ADR-003: Read bags with `rosbag2_cpp`
**Status:** Proposed

**Context.** The prototype used the Python `rosbags` library, which needs no ROS install, but
it could only decode standard ROS types: sqlite3 bags do not contain the definitions of
`mavros_msgs`, `px4_msgs` or `livox_ros_driver2`.

**Decision.** Use `rosbag2_cpp` with ROS 2 installed (Jazzy, see ADR-010). All team message packages are then
available, so the viewer gets richer data (`/mavros/state` for armed and mode,
`/mavros/statustext/recv`, waypoints).

**Consequences.** Viewer machines need ROS 2 (the team image has it).

## ADR-004: Ubuntu 22.04 / ROS 2 Humble
**Status:** Superseded by ADR-010 (2026-11-15)

**Context.** The whole infrastructure is on Ubuntu 22.04 with ROS 2 Humble. Humble reaches end
of life in **May 2027**.

**Decision.** Target Humble / 22.04. Use only stable `rosbag2_cpp` APIs and add a CI build
against the next ROS 2 LTS, so the viewer moves along whenever the team migrates. The
viewer must also keep reading bags recorded with Humble after the team migrates.

## ADR-005: Standalone application
**Status:** Accepted (2026-11-15)

**Decision.** The viewer is its own desktop application with its own packaging and releases.
It is not embedded in the team's GCS.

**Consequences.** No integration contract with the GCS team is needed, and the viewer keeps
its own release cycle. Visual consistency with the GCS (flight-mode names and colours) is
nice to have, not a requirement.

## ADR-006: The viewer adapts to any bag
**Status:** Accepted (2026-11-15)

**Context.** We considered agreeing a "recording profile" (required topics, format) with the
drone team.

**Decision.** No requirements on what is recorded. The viewer shows what it finds, hides
features without data, and explains in "Recording info" what was and was not found.

**Consequences.** Adapters must be tolerant: topic preference lists, defaults, unknown types
skipped. Some features (e.g. detection placement) degrade gracefully when data is missing.

## ADR-007: Per-user Cesium ion tokens, simple offline fallback
**Status:** Accepted (2026-11-15)

**Decision.**
- Each user enters their own Cesium ion token in Settings. It is stored in the user's config
  or keyring, never in git or the package.
- Without internet or without a token, GPS flights are shown in the local 3D scene instead of
  on the 3D map.
- No tile caching. Offline support is wanted but is not a priority.

## ADR-008: Several drones per bag
**Status:** Accepted (2026-11-15)

**Decision.** Detect drones by topic namespace. Each drone gets its own series, path, model
and telemetry, and a drone selector chooses which one the instruments follow. A bag with
one drone behaves exactly as before.

## ADR-009: English UI
**Status:** Accepted (2026-11-15)

**Decision.** The UI is English only. Texts still go through Qt's `tr()` so a translation can
be added later without code changes.


## ADR-010: Ubuntu 24.04 / ROS 2 Jazzy, built on the team's image
**Status:** Accepted (2026-11-15). Supersedes ADR-004.

**Context.** ADR-004 assumed the team runs Humble on 22.04. The team's onboarding
(`onboarding_tutorial.pdf`) shows otherwise: everything ROS 2, PX4 and Gazebo runs inside the
Docker image built from `OpenHORYZN/base_image`, tagged `base-image:jazzy` (Ubuntu 24.04,
ROS 2 Jazzy, PX4 v1.17). It already contains `rosbag2_cpp`, the mcap and zstd plugins, CMake,
Ninja and colcon, but no Qt, no `mavros_msgs`, and `px4_msgs` only in each user's `/ros2_ws`.

**Decision.** Target Jazzy / 24.04. The viewer's dev container is `FROM base-image:jazzy` and adds
Qt 6.8 (aqtinstall), `mavros_msgs` and its own build of `px4_msgs` (`release/1.17`).

**Consequences.**
- The viewer builds against the same ROS as the drones, with no second ROS to maintain.
- Bags recorded on Humble (sqlite3) must still open: use only stable `rosbag2_cpp` APIs and test
  both storage formats with synthetic bags.
- The team image must be built locally before the viewer image (30-60 min, once).
- **Host OS (to confirm):** the user says the team's laptops, including the dev laptop, run
  Ubuntu 22.04, but the dev laptop reports 24.04.5 (`/etc/os-release`, 2026-11-15). The onboarding
  supports both hosts. Because ROS and the viewer run in the container, the host OS only matters
  for packaging: a native `.deb` built on 24.04 cannot be installed on 22.04, and Jazzy has no
  native 22.04 packages. If hosts are 22.04, ship the viewer as a container image (or build a
  separate Humble variant).
- Ubuntu 24.04 blocks Chromium's sandbox through AppArmor ([prototype-lessons.md](prototype-lessons.md)),
  which matters if the engine prototype picks CesiumJS in Qt WebEngine.
