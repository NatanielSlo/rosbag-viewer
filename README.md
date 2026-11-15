# 3D Mission Viewer

A standalone desktop application that shows **completed drone missions in 3D, from rosbag
recordings**: flight path, attitude and instruments, flight modes, camera images, detections
placed where they were seen, planned vs flown path, system health and pilot inputs. Indoor
flights are shown in a local 3D scene, GPS flights on a photorealistic 3D map. A timeline
replays the mission. It works with any bag, and with one or more drones per bag.

**Scope:** only post-mission 3D visualization from rosbags. Mission planning and live
telemetry are separate parts of the team's ground control station, built by other team members.

**Status:** planning. No code yet.

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Scope and users, technology stack, structure, views and controls, analysis features, 3D engine options, testing, performance targets |
| [docs/development-plan.md](docs/development-plan.md) | Steps 0–8, open questions |
| [docs/decisions.md](docs/decisions.md) | Decision records (ADR-001 …) |
| [docs/prototype-lessons.md](docs/prototype-lessons.md) | What the first prototype taught us, plus the topic inventory of our bags |

**Platform:** Ubuntu 22.04, ROS 2 Humble (for reading bags), Qt 6 LTS, C++20.

The earlier Electron/Python prototype is kept at the git tag `prototype-electron`.
