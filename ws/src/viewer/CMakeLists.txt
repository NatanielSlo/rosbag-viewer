cmake_minimum_required(VERSION 3.22)
project(viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

find_package(ament_cmake REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick)
qt_standard_project_setup(REQUIRES 6.5)

# Everything that does not need a window lives in viewer_core (QtCore only),
# so it can be unit-tested without UI or ROS.
add_library(viewer_core STATIC src/appinfo.cpp)
target_include_directories(viewer_core PUBLIC src)
target_link_libraries(viewer_core PUBLIC Qt6::Core)

qt_add_executable(viewer src/main.cpp)
qt_add_qml_module(viewer
    URI Viewer
    VERSION 1.0
    QML_FILES qml/Main.qml)
target_link_libraries(viewer PRIVATE viewer_core Qt6::Gui Qt6::Quick)

install(TARGETS viewer RUNTIME DESTINATION lib/${PROJECT_NAME})

if(BUILD_TESTING)
  find_package(ament_cmake_gtest REQUIRED)
  ament_add_gtest(test_appinfo test/test_appinfo.cpp)
  target_link_libraries(test_appinfo viewer_core)
endif()

ament_package()
