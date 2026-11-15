cmake_minimum_required(VERSION 3.22)
project(engine_osgearth LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)   # osgEarth 3.8 and cesium-native v0.37 headers are C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)

find_package(ament_cmake REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick OpenGL)
qt_standard_project_setup(REQUIRES 6.5)
find_package(OpenSceneGraph COMPONENTS osgDB osgGA osgUtil osgViewer)
find_package(osgEarth QUIET)
if(NOT OPENSCENEGRAPH_FOUND OR NOT osgEarth_FOUND)
  # Only the osgEarth image (.devcontainer/Dockerfile.osgearth) has these libraries.
  message(WARNING "osgEarth not found: skipping prototype engine_osgearth")
  ament_package()
  return()
endif()

qt_add_executable(engine_osgearth src/main.cpp)
qt_add_qml_module(engine_osgearth URI Proto VERSION 1.0
    QML_FILES qml/Main.qml
    SOURCES src/osgearthitem.h src/osgearthitem.cpp)
target_compile_definitions(engine_osgearth PRIVATE
    EARTH_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_include_directories(engine_osgearth PRIVATE src
    ${OPENSCENEGRAPH_INCLUDE_DIRS} /opt/cesium-native/include)
target_link_libraries(engine_osgearth PRIVATE
    Qt6::Gui Qt6::Quick Qt6::OpenGL ${OPENSCENEGRAPH_LIBRARIES}
    osgEarth::osgEarth osgEarth::osgEarthCesium)

install(TARGETS engine_osgearth RUNTIME DESTINATION lib/${PROJECT_NAME})
ament_package()
