# Build and run environment for the 3D Mission Viewer.
# Built on the team's onboarding image (OpenHORYZN/base_image: Ubuntu 24.04, ROS 2 Jazzy,
# PX4 + Gazebo), so the viewer builds against the same ROS as the drones. Build that image
# first with ./build_container.sh in the base_image repository (it is tagged base-image:jazzy).
ARG BASE_IMAGE=base-image:jazzy
FROM ${BASE_IMAGE}

ARG QT_VERSION=6.8.3
# px4_msgs branch matching the drones' PX4 version (the onboarding uses PX4 v1.17).
ARG PX4_MSGS_REF=release/1.17
ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential cmake ninja-build git clang-format python3-pip \
      ros-jazzy-mavros-msgs ros-jazzy-ament-cmake-gtest ros-jazzy-ament-lint-auto \
      libgl1-mesa-dev libegl1 libxkbcommon-x11-0 libxcb-cursor0 libxcb-icccm4 \
      libxcb-image0 libxcb-keysyms1 libxcb-render-util0 libxcb-xinerama0 \
      libxcb-xkb1 libxkbcommon0 libfontconfig1 libdbus-1-3 libvulkan1 \
      mesa-vulkan-drivers libgl1-mesa-dri mesa-utils xvfb \
      libnss3 libasound2t64 libxcomposite1 libxdamage1 libxrandr2 libxtst6 libxkbfile1 \
      libdrm2 libgbm1 libxshmfence1 libxfixes3 libxcursor1 libxi6 libpulse0 libcups2t64 \
 && rm -rf /var/lib/apt/lists/*

# Qt 6 LTS. Ubuntu 24.04 ships 6.4, so install 6.8 with aqtinstall. Linked dynamically (LGPL).
RUN pip3 install --no-cache-dir --break-system-packages aqtinstall \
 && aqt install-qt linux desktop ${QT_VERSION} linux_gcc_64 -O /opt/Qt -m qtquick3d qtwebengine qtwebchannel qtpositioning \
 && pip3 uninstall -y --break-system-packages aqtinstall
ENV QT_ROOT=/opt/Qt/${QT_VERSION}/gcc_64
ENV CMAKE_PREFIX_PATH=${QT_ROOT}
ENV PATH=${QT_ROOT}/bin:${PATH}
ENV LD_LIBRARY_PATH=${QT_ROOT}/lib

# px4_msgs is not in apt. In the team image it lives in the user's /ros2_ws, so build our own copy.
RUN mkdir -p /opt/px4_ws/src && cd /opt/px4_ws/src \
 && git clone --depth 1 --branch ${PX4_MSGS_REF} https://github.com/PX4/px4_msgs.git \
 && cd /opt/px4_ws && . /opt/ros/jazzy/setup.sh \
 && colcon build --cmake-args -DCMAKE_BUILD_TYPE=Release \
 && chmod -R a+rX /opt/px4_ws

# Like the team image, the container runs as root. scripts/dev starts it with your own uid
# instead, so files created in the bind mount are not owned by root.
WORKDIR /workspace
