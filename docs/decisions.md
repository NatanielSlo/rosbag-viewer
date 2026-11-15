# Architecture decisions

Short records of decisions and why they were made. Add new ones at the bottom and never
rewrite an accepted record: supersede it with a new one instead.

Status values: **Accepted**, **Proposed** (needs team review), **Open** (needs input or a prototype).

---

## ADR-001: Native Qt/C++ application instead of Electron
**Status:** Accepted (2026-11-15)

**Context.** The first rosbag viewer was an Electron + Python + three.js/CesiumJS prototype.
The team is a professional drone company and the long-term goal is a full ground control
station with a live link. Comparable professional tools (QGroundControl, Auterion Mission
Control, Mission Planner) are native, and so are the ROS tools (RViz2, rqt, PlotJuggler: Qt).

**Decision.** Build the GCS as a native C++20 / Qt 6 (Qt Quick) application. The replay viewer
becomes the GCS's replay mode, not a separate app.

**Consequences.** We give up the prototype's code (it is kept at tag `prototype-electron`
and its lessons are in [prototype-lessons.md](prototype-lessons.md)). Development is slower
than with web technologies, but there is one stack, one runtime and native performance, and the
code integrates directly with `rclcpp` and `rosbag2_cpp`.

## ADR-002: Build our own GCS instead of forking QGroundControl
**Status:** Accepted (2026-11-15)

**Context.** QGC (Qt 6, Apache 2.0 / GPLv3) already has mission planning, parameters, video,
offline maps and multi-vehicle support, but it is built around MAVLink and is a large codebase.

**Decision.** Write our own GCS designed around ROS 2 from day one.

**Consequences.** Full control over architecture and UX, but every GCS feature has to be built.
Where QGC's Apache-2.0 code solves a self-contained problem (e.g. tile caching), we may reuse it
under that licence, with attribution. The development plan orders features so that replay and
monitoring are useful long before command and mission planning exist.

## ADR-003: ROS 2 over an IP link, bridged with Zenoh
**Status:** Proposed

**Context.** The data link will be Wi-Fi, mesh or LTE (IP). Plain DDS over such links
suffers from discovery traffic, multicast and poor behaviour on lossy links. The drones run
ROS 2 Humble.

**Decision.** Run `zenoh-bridge-ros2dds` on each drone and on the GCS. DDS stays local
to each machine; only allow-listed topics cross the link over Zenoh, under a per-drone
namespace. Video goes over RTP/GStreamer, not ROS.

**Consequences.** Works on Humble and across ROS distributions, which also helps during the
migration (ADR-006). It adds a process per machine and a bridge configuration that must be
versioned together with the interface package (ADR-005).

## ADR-004: 3D view engine
**Status:** Open, decided by the 3D prototype (development plan M1)

**Options.** (A) osgEarth + Cesium Native, (B) Qt Quick 3D + Cesium Native, (C) CesiumJS in
Qt WebEngine. Details and the measurement criteria are in [architecture.md §6](architecture.md#6-3d-view-options-and-prototype-criteria).

**Known facts (2026-11).** osgEarth 3.8 (March 2026) supports Cesium ion and Google 3D Tiles
through its `osgEarthCesium` node kit. It runs on OpenSceneGraph, which has been in maintenance
mode since 2019. Pelican Mapping's newer engine Rocky (VulkanSceneGraph) has no 3D Tiles
support yet. Ubuntu packages osgEarth 2.10 at best, so it must be built from source.

## ADR-005: Shared interface package between drone and GCS
**Status:** Proposed

**Context.** Current bags carry detections as JSON inside `std_msgs/String` and flight modes
as free text (sometimes with status messages appended). Topic names differ between bags.

**Decision.** Create a versioned ROS 2 package (working name `horyzn_interfaces`) with our
message definitions, canonical topic names, QoS profiles and the Zenoh bridge allow-list.
Both the drone software and the GCS depend on it.

## ADR-006: Ubuntu 22.04 / ROS 2 Humble now, with a planned migration
**Status:** Open, needs a team decision

**Context.** The whole infrastructure is on Ubuntu 22.04 with ROS 2 Humble. **Humble reaches
end of life in May 2027**, and Ubuntu 22.04's standard support ends around the same time.
A new GCS started in November 2026 will spend most of its life after that date.

**Proposal.**
1. Target Humble / 22.04 now, because that is what the drones run.
2. Keep the GCS code distribution-agnostic: use only stable `rclcpp`/`rosbag2_cpp` APIs and
   build a second CI job against the next LTS from the start.
3. Plan the fleet migration (drones + GCS) to the next ROS 2 LTS / Ubuntu LTS as its own
   milestone (development plan M9). The Zenoh bridge (ADR-003) lets mixed-distribution fleets
   interoperate during the transition.

**Needs from the team.** The target date for the migration, and whether drones and GCS laptops
migrate together.
