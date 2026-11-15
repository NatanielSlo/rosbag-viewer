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

**Decision.** Build the viewer in C++20 with Qt 6 (Qt Quick), on the same stack as the rest of
the GCS, so it can run standalone or be embedded.

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

**Decision.** Use `rosbag2_cpp` with ROS 2 Humble installed. All team message packages are then
available, so the viewer gets richer data (`/mavros/state` for armed and mode,
`/mavros/statustext/recv`, waypoints).

**Consequences.** Viewer machines need ROS 2 Humble (the team has it everywhere already).

## ADR-004: Ubuntu 22.04 / ROS 2 Humble
**Status:** Accepted for now; follows the team's migration plan

**Context.** The whole infrastructure is on Ubuntu 22.04 with ROS 2 Humble. Humble reaches end
of life in **May 2027**.

**Decision.** Target Humble / 22.04. Use only stable `rosbag2_cpp` APIs and add a CI build
against the next ROS 2 LTS, so the viewer moves along whenever the team migrates. The
viewer must also keep reading bags recorded with Humble after the team migrates.
