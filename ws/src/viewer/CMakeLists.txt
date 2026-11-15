cmake_minimum_required(VERSION 3.22)
project(viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

find_package(ament_cmake REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick WebEngineQuick WebEngineCore)
qt_standard_project_setup(REQUIRES 6.5)

# Everything that does not need a window lives in viewer_core (QtCore only),
# so it can be unit-tested without UI or ROS.
add_library(viewer_core STATIC src/appinfo.cpp src/model/frames.cpp src/model/flightmodel.cpp src/model/telemetry.cpp)
target_include_directories(viewer_core PUBLIC src)
target_link_libraries(viewer_core PUBLIC Qt6::Core)

# Synthetic test bags need ROS (rosbag2_cpp) and px4_msgs; skipped where they are missing.
find_package(rosbag2_cpp QUIET)
find_package(px4_msgs QUIET)
find_package(rclcpp QUIET)
find_package(rosbag2_compression QUIET)
find_package(mavros_msgs QUIET)
find_package(geometry_msgs QUIET)
find_package(sensor_msgs QUIET)
set(HAVE_ROS_BAGS FALSE)
if(rosbag2_cpp_FOUND AND rosbag2_compression_FOUND AND px4_msgs_FOUND AND rclcpp_FOUND
   AND mavros_msgs_FOUND AND geometry_msgs_FOUND AND sensor_msgs_FOUND)
  set(HAVE_ROS_BAGS TRUE)
  add_library(viewer_bag STATIC src/bag/bagsource.cpp src/bag/recordinginfo.cpp)
  target_include_directories(viewer_bag PUBLIC src)
  target_link_libraries(viewer_bag PUBLIC viewer_core)
  ament_target_dependencies(viewer_bag PUBLIC rosbag2_cpp rosbag2_compression px4_msgs mavros_msgs
                           geometry_msgs sensor_msgs rclcpp)

  add_executable(make_test_bag tools/make_test_bag.cpp)
  target_include_directories(make_test_bag PRIVATE src)
  ament_target_dependencies(make_test_bag rosbag2_cpp rosbag2_compression px4_msgs mavros_msgs
                           geometry_msgs sensor_msgs rclcpp)
  add_executable(bag_info tools/bag_info.cpp)
  target_link_libraries(bag_info viewer_bag)
  install(TARGETS make_test_bag bag_info RUNTIME DESTINATION lib/${PROJECT_NAME})
else()
  message(WARNING "rosbag2_cpp/px4_msgs not found: skipping make_test_bag and its test")
endif()

# The application. It needs the bag reader (ROS), so it is built only where that is available.
if(HAVE_ROS_BAGS)
  qt_add_executable(viewer src/main.cpp src/app/replaycontroller.cpp)
  qt_add_qml_module(viewer
      URI Viewer
      VERSION 1.0
      QML_FILES qml/Main.qml)
  # The web files are served from the source tree for now; packaging comes with step 7.
  target_compile_definitions(viewer PRIVATE WEB_ROOT="${CMAKE_CURRENT_SOURCE_DIR}")
  target_link_libraries(viewer PRIVATE viewer_bag Qt6::Gui Qt6::Quick Qt6::WebEngineQuick
                                       Qt6::WebEngineCore)
  install(TARGETS viewer RUNTIME DESTINATION lib/${PROJECT_NAME})
endif()

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  foreach(t appinfo frames replayclock flightmodel telemetry)
    ament_add_gtest(test_${t} test/test_${t}.cpp)
    target_link_libraries(test_${t} viewer_core)
  endforeach()
  if(HAVE_ROS_BAGS)
    ament_add_gtest(test_testbag test/test_testbag.cpp)
    target_compile_definitions(test_testbag PRIVATE MAKE_TEST_BAG="$<TARGET_FILE:make_test_bag>")
    target_link_libraries(test_testbag)
    ament_target_dependencies(test_testbag rosbag2_cpp)
    add_dependencies(test_testbag make_test_bag)

    ament_add_gtest(test_recordinginfo test/test_recordinginfo.cpp)
    target_compile_definitions(test_recordinginfo PRIVATE MAKE_TEST_BAG="$<TARGET_FILE:make_test_bag>")
    target_link_libraries(test_recordinginfo viewer_bag)
    add_dependencies(test_recordinginfo make_test_bag)

    ament_add_gtest(test_bagsource test/test_bagsource.cpp)
    target_compile_definitions(test_bagsource PRIVATE MAKE_TEST_BAG="$<TARGET_FILE:make_test_bag>")
    target_link_libraries(test_bagsource viewer_bag)
    add_dependencies(test_bagsource make_test_bag)
  endif()
endif()

ament_package()
