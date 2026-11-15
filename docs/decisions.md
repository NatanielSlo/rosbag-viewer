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
- Ubuntu 24.04 blocks Chromium's sandbox through AppArmor ([prototype-lessons.md](prototype-lessons.md)),
  which matters if the engine prototype picks CesiumJS in Qt WebEngine.
