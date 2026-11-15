# 3D Mission Viewer

A standalone desktop application that shows **completed drone missions in 3D, from rosbag
recordings**: flight path, attitude and instruments, flight modes, camera images, detections
placed where they were seen, planned vs flown path, system health and pilot inputs. Indoor
flights are shown in a local 3D scene, GPS flights on a photorealistic 3D map. A timeline
replays the mission. It works with any bag, and with one or more drones per bag.

**Scope:** only post-mission 3D visualization from rosbags. Mission planning and live
telemetry are separate parts of the team's ground control station, built by other team members.

**Status:** early development (see [docs/development-plan.md](docs/development-plan.md)).
Step 0 (dev container, app skeleton, tests) is done. Step 1 (3D engine prototypes) is in
progress. In Step 2, the engine-independent core is in place and tested: frame conversions
(NED/FRD to ENU/FLU, WGS84), `ReplayClock` and the per-drone flight model. Bag reading is next.

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Scope and users, technology stack, structure, views and controls, analysis features, 3D engine options, testing, performance targets |
| [docs/development-plan.md](docs/development-plan.md) | Steps 0–8, open questions |
| [docs/decisions.md](docs/decisions.md) | Decision records (ADR-001 …) |
| [docs/prototype-lessons.md](docs/prototype-lessons.md) | What the first prototype taught us, plus the topic inventory of our bags |

**Platform:** Ubuntu 24.04, ROS 2 Jazzy (for reading bags), Qt 6 LTS, C++20. Developed in a
container built on the team's onboarding image `base-image:jazzy` (ADR-010).

**Build and run:** build the team image first (`./build_container.sh` in `OpenHORYZN/base_image`),
then `scripts/dev run`. Other commands: `build`, `test`, `smoke`, `format`, `shell`.

The earlier Electron/Python prototype is kept at the git tag `prototype-electron`.
