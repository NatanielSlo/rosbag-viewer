# 3D Mission Viewer

A standalone desktop application that shows **completed drone missions in 3D, from rosbag
recordings**: flight path, attitude and instruments, flight modes, camera images, detections
placed where they were seen, planned vs flown path, system health and pilot inputs. Indoor
flights are shown in a local 3D scene, GPS flights on a photorealistic 3D map. A timeline
replays the mission. It works with any bag, and with one or more drones per bag.

**Scope:** only post-mission 3D visualization from rosbags. Mission planning and live
telemetry are separate parts of the team's ground control station, built by other team members.

**Status:** early development (see [docs/development-plan.md](docs/development-plan.md)).
Done: dev container, the engine-independent core (frames, clock, flight model, telemetry), bag
reading (PX4 and MAVROS, compressed and split bags), and the 3D engine choice (CesiumJS in Qt
WebEngine, ADR-002). The viewer app now opens a GPS bag and replays it on the 3D map with a
telemetry panel, timeline and the keyboard map. Next: the local 3D scene for flights without GPS,
then the instruments (attitude, compass) and the Analysis panel.

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Scope and users, technology stack, structure, views and controls, analysis features, 3D engine options, testing, performance targets |
| [docs/development-plan.md](docs/development-plan.md) | Steps 0–8, open questions |
| [docs/decisions.md](docs/decisions.md) | Decision records (ADR-001 …) |
| [docs/prototype-lessons.md](docs/prototype-lessons.md) | What the first prototype taught us, plus the topic inventory of our bags |

**Platform:** Ubuntu 24.04, ROS 2 Jazzy (for reading bags), Qt 6 LTS, C++20. Developed in a
container built on the team's onboarding image `base-image:jazzy` (ADR-010).

**Build and run:** build the team image first (`./build_container.sh` in `OpenHORYZN/base_image`),
then `scripts/dev run [bag]` (a rosbag2 folder; or Ctrl+O in the app). Other commands: `build`, `test`, `smoke`, `format`, `shell`.

The earlier Electron/Python prototype is kept at the git tag `prototype-electron`.

**Credits:** the drone model in the 3D view is "CesiumDrone" (simplified material) by Ed Mackey (Analytical Graphics /
Cesium), CC-BY 4.0 (`ws/src/viewer/web/models/CREDITS.txt`). It is a placeholder until the team has
its own model.
