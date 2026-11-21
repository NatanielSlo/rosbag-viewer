# 3D Mission Viewer

Desktop app for looking back at drone missions in 3D. You point it at a rosbag and it shows the flight path, attitude and instruments, flight modes, camera images, detections at the spot where they were seen, the planned path next to the flown one, system health and pilot inputs. Indoor flights are drawn in a local 3D scene, and GPS flights on a photorealistic 3D map. Use the timeline to replay the mission.

It works with any bag, with one drone or several.

## Scope

Only post-mission 3D visualization from rosbags. Mission planning and live telemetry are not part of this project.

## Status

Early development.

What works so far:
- dev container
- the engine-independent core: frames, clock, flight model, telemetry
- bag reading for PX4 and MAVROS, including compressed and split bags
- 3D engine: CesiumJS running inside Qt WebEngine
- the viewer can open a GPS bag and replay it on the 3D map, with a telemetry panel, a timeline and keyboard shortcuts

Up next is the local 3D scene for flights without GPS. After that come the instruments (attitude, compass) and the Analysis panel.

## Platform

Ubuntu 24.04, ROS 2 Jazzy (used for reading bags), Qt 6 LTS, C++20. Development happens in a container built on top of the `base-image:jazzy` onboarding image.

## Build and run

1. Build the base image first: run `./build_container.sh` in `OpenHORYZN/base_image`.
2. Start the viewer with `scripts/dev run [bag]`, where `bag` is a rosbag2 folder. You can also skip the argument and open a bag with Ctrl+O.

Other `scripts/dev` commands: `build`, `test`, `smoke`, `format`, `shell`.

The old Electron/Python prototype is still available at the `prototype-electron` tag.

## Credits

The drone model in the 3D view is "CesiumDrone" (simplified material) by Ed Mackey (Analytical Graphics / Cesium), licensed CC-BY 4.0. See `ws/src/viewer/web/models/CREDITS.txt`. It's a placeholder until we have our own model.
