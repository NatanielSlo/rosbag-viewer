# 3D Mission Viewer

Shows **completed drone missions in 3D, from rosbag recordings**: the flight path, the drone's
attitude, flight modes, camera images and detections, either in a local 3D scene (indoor
flights) or on a photorealistic 3D map (GPS flights), with a timeline to replay the mission.

**Scope:** only post-mission 3D visualization from rosbags. Mission planning and live
telemetry are separate parts of the team's ground control station, built by other team members.

**Status:** planning. No code yet.

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Scope, technology stack, structure, 3D engine options, testing |
| [docs/development-plan.md](docs/development-plan.md) | Steps 0–6, open questions |
| [docs/decisions.md](docs/decisions.md) | Decision records (ADR-001 …) |
| [docs/prototype-lessons.md](docs/prototype-lessons.md) | What the first prototype taught us, plus the topic inventory of our bags |

**Platform:** Ubuntu 22.04, ROS 2 Humble (for reading bags), Qt 6 LTS, C++20.

The earlier Electron/Python prototype is kept at the git tag `prototype-electron`.
