# HORYZN Ground Control Station

A native ground control station for our drones, built around **ROS 2**: live monitoring
(and later commanding) over an IP link, plus replay and analysis of recorded rosbags through
the same pipeline.

**Status:** planning. There is no code yet; the architecture is under team review.

| Document | Contents |
|---|---|
| [docs/architecture.md](docs/architecture.md) | Goals, technology stack, modules, data link, testing, repo layout |
| [docs/development-plan.md](docs/development-plan.md) | Milestones M0–M9, first sprint, open questions for the team |
| [docs/decisions.md](docs/decisions.md) | Architecture decision records (ADR-001 …) |
| [docs/prototype-lessons.md](docs/prototype-lessons.md) | What the first viewer prototype taught us, plus the topic inventory of our bags |

**Target platform:** Ubuntu 22.04, ROS 2 Humble, Qt 6 LTS, C++20
(see ADR-006 about Humble's end of life in May 2027).

The previous Electron/Python rosbag viewer prototype is available at the git tag
`prototype-electron` (`git checkout prototype-electron`).
